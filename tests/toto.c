/*
gcc -g toto.c -o toto
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

/******************************************************************************/
#ifndef likely
# define likely(x)              __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
# define unlikely(x)            __builtin_expect(!!(x), 0)
#endif
static sig_atomic_t sigint = 0;
static void sighandler(int num) { sigint = 1; }

/******************************************************************************/
void main(int argc, char **argv) {

  signal(SIGINT, sighandler);

  if (argc != 2) exit(-1);
  int c_packet_sz = getpagesize();
  printf("(%s)(%d)\n",argv[1],c_packet_sz);

  int sockfd;
  if (-1 == (sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)))) exit(-1);

  int tpver = TPACKET_V3;
  if (-1 == (setsockopt(sockfd, SOL_PACKET, PACKET_VERSION,  &tpver, sizeof(tpver)))) exit(-1);
/*
  int discard = 1;
  if (-1 == (setsockopt(sockfd, SOL_PACKET, PACKET_LOSS, &discard, sizeof(discard)))) exit(-1);
*/
  int on = 1;
  if (-1 == (setsockopt(sockfd, SOL_PACKET, PACKET_QDISC_BYPASS, &on, sizeof(on)))) exit(-1);

  /*---------------------------------------------------------------------*/
  struct tpacket_req3 tx_packet_req = {
    .tp_block_size = 1<<22,
    .tp_frame_size = 1<<11,
    .tp_block_nr = 64
  };
  tx_packet_req.tp_frame_nr = (tx_packet_req.tp_block_size * tx_packet_req.tp_block_nr)/tx_packet_req.tp_frame_size;

  struct tpacket_req3 rx_packet_req = {
    .tp_block_size = 1<<22,
    .tp_frame_size = 1<<11,
    .tp_block_nr = 64
  };
  rx_packet_req.tp_frame_nr = (rx_packet_req.tp_block_size * rx_packet_req.tp_block_nr)/rx_packet_req.tp_frame_size;

  if (-1 == setsockopt (sockfd, SOL_PACKET, PACKET_RX_RING, &rx_packet_req, sizeof(rx_packet_req))) exit(-1); // FIRST
  if (-1 == setsockopt (sockfd, SOL_PACKET, PACKET_TX_RING, &tx_packet_req, sizeof(tx_packet_req))) exit(-1); // SECOND

  // https://docs.kernel.org/networking/packet_mmap.html
  size_t size = tx_packet_req.tp_block_size * tx_packet_req.tp_block_nr;
  uint8_t *rxmap; if (MAP_FAILED == (rxmap = mmap(0, size * 2, PROT_READ|PROT_WRITE, MAP_SHARED, sockfd, 0))) exit(-1);
  uint8_t *txmap = rxmap + size;


  /*---------------------------------------------------------------------*/
  int tx_len = 4;
  int i = 0;
  struct tpacket3_hdr *tx_header = ((struct tpacket3_hdr *)((void *)txmap + (tx_packet_req.tp_block_size*i)));

  tx_header->tp_snaplen = tx_header->tp_len = tx_len;
  tx_header->tp_next_offset = 0;
  tx_header->tp_status = TP_STATUS_SEND_REQUEST;

  /*---------------------------------------------------------------------*/
  struct sockaddr_ll sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sll_family = AF_PACKET;
  sockaddr.sll_protocol = htons(ETH_P_ALL);
  sockaddr.sll_ifindex = if_nametoindex(argv[1]);
  if (-1 == (bind(sockfd, (const struct sockaddr*)&sockaddr, sizeof(sockaddr)))) exit(-1);

  struct pollfd pfd = {
    .fd = sockfd,
    .events = POLLIN | POLLERR,
    .revents = 0
  };

  unsigned int blockcur = 0; 

  struct timespec ts;
  uint64_t curms,  stoms, intms = 1000;
  clock_gettime(CLOCK_MONOTONIC, &ts); stoms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

  while (likely(!sigint)) {
    clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
    poll(&pfd, 1, stoms > curms ? stoms - curms : 0);
    clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

    if (curms >= stoms) { // SYNCHRONOUS
      stoms = curms + intms - ((curms - stoms) % intms);
      printf("TIC\n");
    }

    struct tpacket3_hdr *rx_header = ((struct tpacket3_hdr *)((void *)rxmap + (rx_packet_req.tp_block_size * blockcur)));
    if ((rx_header->tp_status & TP_STATUS_USER) == 0) {
      rx_header->tp_status = TP_STATUS_KERNEL;
      blockcur = (blockcur + 1) % rx_packet_req.tp_block_nr;
      printf("RECV\n");
    }

    if (tx_header->tp_status != TP_STATUS_AVAILABLE) { 
      ssize_t sendlen = send(sockfd, NULL, 0, 0);
      tx_header->tp_status = TP_STATUS_KERNEL;
      printf("SENT (%ld))\n",sendlen);
    }
  }

  struct tpacket_stats_v3 stats;
  socklen_t len = sizeof(stats);
  if (getsockopt(sockfd, SOL_PACKET, PACKET_STATISTICS, &stats, &len) < 0) exit(-1);
  printf("\nReceived %u packets, %u dropped, freeze_q_cnt: %u\n",
    stats.tp_packets, stats.tp_drops, stats.tp_freeze_q_cnt);

  munmap(rxmap, rx_packet_req.tp_block_size * rx_packet_req.tp_block_nr);
  munmap(txmap, tx_packet_req.tp_block_size * tx_packet_req.tp_block_nr);
  close(sockfd);
}
