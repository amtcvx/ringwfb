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

/******************************************************************************/
void main(int argc, char **argv) {

  signal(SIGINT, sighandler);

  if (argc != 2) exit(-1);
  printf("(%s)\n",argv[1]);

  int sockfd;
  if (-1 == (sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)))) exit(-1);

  int tpver = TPACKET_V3;
  if (-1 == (setsockopt(sockfd, SOL_PACKET, PACKET_VERSION,  &tpver, sizeof(tpver)))) exit(-1);

  int discard = 1;
  if (-1 == (setsockopt(sockfd, SOL_PACKET, PACKET_LOSS, &discard, sizeof(discard)))) exit(-1);

  int on = 1;
  if (-1 == (setsockopt(sockfd, SOL_PACKET, PACKET_QDISC_BYPASS, &on, sizeof(on)))) exit(-1);

  unsigned int blocknr = 64;
  unsigned int blocksize = 1 << 22, framesize = 1 << 11;
  unsigned int framenr =  (blocksize * blocknr) / framesize;

  struct tpacket_req3 req;
  memset(&req,0,sizeof(req));
  req.tp_block_size = blocksize;
  req.tp_block_nr = blocknr;
  req.tp_frame_size = framesize;
  req.tp_frame_nr = framenr;

  if (-1 == (setsockopt(sockfd, SOL_PACKET, PACKET_RX_RING, &req, sizeof(req)))) exit(-1);

  if (-1 == (setsockopt(sockfd, SOL_PACKET, PACKET_TX_RING, &req, sizeof(req)))) exit(-1);

  void *rxmap = mmap(NULL, req.tp_block_size * req.tp_block_nr,  PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, sockfd, 0);
  if (!rxmap) exit(-1);

  struct iovec rd[req.tp_block_nr][sizeof(struct iovec)];
  for (int i = 0; i < req.tp_block_nr; ++i) {
    rd[i]->iov_base = rxmap + (i * req.tp_block_size);
    rd[i]->iov_len = req.tp_block_size;
  }
 
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
    if (pfd.revents & POLLIN) {
      printf("HELLO\n");
/*
      int block_pos = 0;
      for ( int block_pos = 0; 1; block_pos = ((block_pos+1) % req.tp_block_nr) ) {

        struct block_desc {
          uint32_t version;
          uint32_t offset_to_priv;
          struct tpacket_hdr_v1 h1;
        } *pbd = (struct block_desc *) ( mmap + (block_pos * req.tp_block_size) );

        pbd->h1.block_status = TP_STATUS_KERNEL;
      }
*/
    }
  }

  struct tpacket_stats_v3 stats;
  socklen_t len = sizeof(stats);
  if (getsockopt(sockfd, SOL_PACKET, PACKET_STATISTICS, &stats, &len) < 0) exit(-1);
  printf("\nReceived %u packets, %u dropped, freeze_q_cnt: %u\n",
    stats.tp_packets, stats.tp_drops, stats.tp_freeze_q_cnt);

  munmap(rxmap, req.tp_block_size * req.tp_block_nr);
  close(sockfd);
}
