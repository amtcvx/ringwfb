/*
gcc -g packet_mmap_nothread.c -o packet_mmap_nothread
*/

#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/mman.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>

#define PAY_MTU	1500

/******************************************************************************/
#ifndef likely
# define likely(x)              __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
# define unlikely(x)            __builtin_expect(!!(x), 0)
#endif
static sig_atomic_t sigint = 0;
static void sighandler(int num) { sigint = 1; }

struct block_desc_t {
  uint32_t version;
  uint32_t offset_to_priv;
  struct tpacket_hdr_v1 h1;
};

/******************************************************************************/
void main(int argc, char **argv) {

  signal(SIGINT, sighandler);

  if (argc != 2) exit(-1);
  printf("(%s)\n",argv[1]);

  int sockfd;
  if (-1 == (sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)))) exit(-1);
  int tpver = TPACKET_V3;
  if (-1 == (setsockopt(sockfd, SOL_PACKET, PACKET_VERSION,  &tpver, sizeof(tpver)))) exit(-1);
  int on = 1;
  if (-1 == (setsockopt(sockfd, SOL_PACKET, PACKET_QDISC_BYPASS, &on, sizeof(on)))) exit(-1);

  /*---------------------------------------------------------------------*/
  unsigned int block_size = 1<<12, frame_size = 1<<11, block_nr = 1;

  unsigned int frame_nr = (block_size * block_nr) / frame_size;
  struct tpacket_req3 packet_req[2] = { [0 ... 1] = {
    .tp_block_size = block_size,
    .tp_frame_size = frame_size,
    .tp_block_nr = block_nr,
    .tp_frame_nr = frame_nr
  }};

  printf("block_size(%d) frame_size(%d) frame_nr(%d)\n",block_size,frame_size,frame_nr);

  if (-1 == setsockopt (sockfd, SOL_PACKET, PACKET_RX_RING, &packet_req[0], sizeof(struct tpacket_req3))) exit(-1); // FIRST
  if (-1 == setsockopt (sockfd, SOL_PACKET, PACKET_TX_RING, &packet_req[1], sizeof(struct tpacket_req3))) exit(-1); // SECOND

  size_t map_size = block_size * block_nr;
  uint8_t *map[2]; if (MAP_FAILED == (map[0] = mmap(0, map_size * 2, PROT_READ|PROT_WRITE, MAP_SHARED, sockfd, 0))) exit(-1);
  map[1] = map[0] + map_size;

  /*---------------------------------------------------------------------*/
 // const int c_packet_sz = 200;
  int i=0;
  //for(int i=0; i < block_nr; i++ ) {
    struct tpacket3_hdr * tx_header = ((struct tpacket3_hdr *)((void *)map[1] + (block_size*i)));

    struct block_desc_t *tx_pbd = (struct block_desc_t *) tx_header;
    tx_pbd->h1.block_status = TP_STATUS_AVAILABLE;

    #define my_TPACKET_ALIGN(x)     (((x)+(uint64_t)(TPACKET_ALIGNMENT-1))&~((uint64_t)(TPACKET_ALIGNMENT-1)))
    char * pkt_ptr = ((void*) tx_header) + my_TPACKET_ALIGN(sizeof(struct tpacket3_hdr));

    for(int j=0; j < PAY_MTU; j++ ) pkt_ptr[j] = j; 
    tx_header->tp_len = (uint32_t)PAY_MTU;
    tx_header->tp_next_offset = 0;
    tx_header->tp_status = TP_STATUS_SEND_REQUEST; // TP_STATUS_KERNEL
  //}

  /*---------------------------------------------------------------------*/
  struct sockaddr_ll sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sll_family = AF_PACKET;
  sockaddr.sll_protocol = htons(ETH_P_ALL);
  sockaddr.sll_ifindex = if_nametoindex(argv[1]);
  if (-1 == (bind(sockfd, (const struct sockaddr*)&sockaddr, sizeof(sockaddr)))) exit(-1);

  struct pollfd pfd = {
    .fd = sockfd,
    .events = POLLIN | POLLERR, // no passthrough POLLOUT
    .revents = 0
  };

  unsigned int rx_block_nr = 0; 
  int total_pkts = 0, ec_send, total_bytes = 0;
  bool tosend = false;

  struct timespec ts;
  uint64_t curms,  stoms, intms = 1000;
  clock_gettime(CLOCK_MONOTONIC, &ts); stoms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

  while (likely(!sigint)) {
    clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
    poll(&pfd, 1, stoms > curms ? stoms - curms : 0);
    clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

    if (curms >= stoms) { // SYNCHRONOUS
      stoms = curms + intms - ((curms - stoms) % intms);
      printf("TIC\n"); fflush(stdout);

      tosend = true;
    }

    if (pfd.revents & POLLIN) {
      struct tpacket3_hdr * rx_header = ((struct tpacket3_hdr *)((void *)map[0] + (block_size * rx_block_nr)));
      struct block_desc_t *rx_pbd = (struct block_desc_t *) rx_header;
      if ((rx_header->tp_status & TP_STATUS_USER) == 0) { 
        rx_pbd->h1.block_status = TP_STATUS_KERNEL;
        printf("RECV\n"); fflush(stdout);
        rx_block_nr = (rx_block_nr + 1) % block_nr;
      }
    }

    if (tosend) {
      tosend = false;
      ec_send = sendto( sockfd, NULL, 0, MSG_DONTWAIT, NULL, sizeof(struct sockaddr_ll) );
      printf("sendto (%d)\n",ec_send);
    }
  }

  struct tpacket_stats_v3 stats;
  socklen_t len = sizeof(stats);
  if (getsockopt(sockfd, SOL_PACKET, PACKET_STATISTICS, &stats, &len) < 0) exit(-1);
  printf("\nReceived %u packets, %u dropped, freeze_q_cnt: %u\n",
    stats.tp_packets, stats.tp_drops, stats.tp_freeze_q_cnt);

  munmap(map[0], map_size);
  close(sockfd);
}
