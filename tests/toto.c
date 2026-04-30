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

/******************************************************************************/
#ifndef likely
# define likely(x)              __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
# define unlikely(x)            __builtin_expect(!!(x), 0)
#endif
static sig_atomic_t sigint = 0;
static void sighandler(int num) { sigint = 1; }

struct ring_t {
  struct iovec *rd;
  uint8_t *map;
  struct tpacket_req3 req;
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
/*
  int discard = 1;
  if (-1 == (setsockopt(sockfd, SOL_PACKET, PACKET_LOSS, &discard, sizeof(discard)))) exit(-1);

  int on = 1;
  if (-1 == (setsockopt(sockfd, SOL_PACKET, PACKET_QDISC_BYPASS, &on, sizeof(on)))) exit(-1);
*/
  unsigned int  blocksize= 4096, blocknr  = 4, framesize= 2048; // (for PAGE_SIZE == 4096)
  unsigned int  framenr =  (blocksize * blocknr) / framesize;

/*
  struct ring_t ringrx;
  memset(&ringrx.req,0,sizeof(ringrx.req));
  ringrx.req.tp_block_size = blocksize;
  ringrx.req.tp_block_nr = blocknr;
  ringrx.req.tp_frame_size = framesize;
  ringrx.req.tp_frame_nr = framenr;

  if (-1 == (setsockopt(sockfd, SOL_PACKET, PACKET_RX_RING, &ringrx.req, sizeof(ringrx.req)))) exit(-1);

  ringrx.map = (uint8_t *)mmap( NULL, blocksize * blocknr, PROT_READ|PROT_WRITE, MAP_SHARED, sockfd, 0 );

  ringrx.rd = malloc(blocknr * sizeof(*ringrx.rd));
  for (int i = 0; i < blocknr; ++i) {
    ringrx.rd[i].iov_base = ringrx.map + (i * blocksize);
    ringrx.rd[i].iov_len = blocksize;
  }
*/

  struct ring_t ringtx;
  memset(&ringtx.req,0,sizeof(ringtx.req));
  ringtx.req.tp_block_size = blocksize;
  ringtx.req.tp_block_nr = blocknr;
  ringtx.req.tp_frame_size = framesize;
  ringtx.req.tp_frame_nr = framenr;

  if (-1 == (setsockopt(sockfd, SOL_PACKET, PACKET_TX_RING, &ringtx.req, sizeof(ringtx.req)))) exit(-1);

  struct tpacket_hdr *map = (uint8_t *)mmap( NULL, blocksize * blocknr, PROT_WRITE, MAP_SHARED, sockfd, 0 );
tx_buffer_payload_offset = TPACKET2_HDRLEN - sizeof(struct sockaddr_ll);
tx_buffer_payload_offset += sizeof(ether_header_t);


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

  struct block_desc {
    uint32_t version;
    uint32_t offset_to_priv;
    struct tpacket_hdr_v1 h1;
  } *pbd;

  unsigned int blockcur = 0;

  struct timespec ts;
  uint64_t curms,  stoms, intms = 1000;
  clock_gettime(CLOCK_MONOTONIC, &ts); stoms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

  while (likely(!sigint)) {
    clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

    pbd = (struct block_desc*)ringrx.rd[blockcur].iov_base;
    if ((pbd->h1.block_status & TP_STATUS_USER) == 0) {
      poll(&pfd, 1, stoms > curms ? stoms - curms : 0);
      clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
      if (curms >= stoms) { // SYNCHRONOUS
        stoms = curms + intms - ((curms - stoms) % intms);
        printf("TIC\n");
      }
      if (pfd.revents & POLLIN) {
        printf("HELLO\n");

        pbd = (struct block_desc *)ringrx.rd[blockcur].iov_base;
        //walk_block(pbd, block_num);
	pbd->h1.block_status = TP_STATUS_KERNEL;
        blockcur = (blockcur + 1) % blocknr;
      }
    }
  }

  struct tpacket_stats_v3 stats;
  socklen_t len = sizeof(stats);
  if (getsockopt(sockfd, SOL_PACKET, PACKET_STATISTICS, &stats, &len) < 0) exit(-1);
  printf("\nReceived %u packets, %u dropped, freeze_q_cnt: %u\n",
    stats.tp_packets, stats.tp_drops, stats.tp_freeze_q_cnt);
  munmap(ringrx.map, blocksize * blocknr);
//  munmap(txmap, blocksize * blocknr);
  close(sockfd);
}
