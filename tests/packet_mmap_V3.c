/*
gcc -g packet_mmap_V3.c -o packet_mmap_V3

https://docs.kernel.org/networking/packet_mmap.html
https://csulrong.github.io/blogs/2022/03/10/linux-afpacket/
*/

#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <net/if.h>
#include <net/if.h>
#include <unistd.h>
#include <stdio.h>
#include <netdb.h>
#include <poll.h>
#include <errno.h>
#include <math.h>
#include <signal.h>


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

  int err, i;
  printf("(%s)\n",argv[1]);

  int fd;
  if (fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)) < 0) exit(-1);

  int v = TPACKET_V3;
  setsockopt(fd, SOL_PACKET, PACKET_VERSION, &v, sizeof(v)); 

  struct ring_t {
    struct iovec *rd;
    uint8_t *map;
    struct tpacket_req3 req;
  } ring;

  unsigned int blocksiz = 1 << 22, framesiz = 1 << 11;
  unsigned int blocknum = 64;
  memset(&ring.req, 0, sizeof(ring.req));
  ring.req.tp_block_size = blocksiz;
  ring.req.tp_frame_size = framesiz;
  ring.req.tp_block_nr = blocknum;
  ring.req.tp_frame_nr = (blocksiz * blocknum) / framesiz;
  ring.req.tp_retire_blk_tov = 60;
  ring.req.tp_feature_req_word = TP_FT_REQ_FILL_RXHASH;

  setsockopt(fd, SOL_PACKET, PACKET_RX_RING, &ring.req, sizeof(ring.req)); 

  ring.map = mmap(NULL, ring.req.tp_block_size * ring.req.tp_block_nr,
                         PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, 0);

  ring.rd = malloc(ring.req.tp_block_nr * sizeof(*ring.rd));
  if (!ring.rd) exit(-1);
  for (i = 0; i < ring.req.tp_block_nr; ++i) {
    ring.rd[i].iov_base = ring.map + (i * ring.req.tp_block_size);
    ring.rd[i].iov_len = ring.req.tp_block_size;
  }

  struct sockaddr_ll ll;
  memset(&ll, 0, sizeof(ll));
  ll.sll_family = PF_PACKET;
  ll.sll_protocol = htons(ETH_P_ALL);
  ll.sll_ifindex = if_nametoindex(argv[1]);
  ll.sll_hatype = 0;
  ll.sll_pkttype = 0;
  ll.sll_halen = 0;

  bind(fd, (struct sockaddr *) &ll, sizeof(ll));

  struct pollfd pfd = {
    .fd = fd,
    .events = POLLIN | POLLERR,
    .revents = 0
  };

  struct block_desc {
    uint32_t version;
    uint32_t offset_to_priv;
    struct tpacket_hdr_v1 h1;
  } *pbd; 

  unsigned int block_num = 0;

  uint8_t numblock = 0;
  uint64_t packets_total = 0, bytes_total = 0;

  signal(SIGINT, sighandler);

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
/*
      struct tpacket3_hdr *hdr = NULL;
      for (i = 0; i < ring.req.tp_frame_nr; i += 1) {
        hdr = (void*)(ring + (ring.req.tp_frame_size * i));
        void *data = (uint8_t*)hdr + TPACKET_ALIGN(sizeof(struct tpacket3_hdr));

        if (hdr->tp_status == TP_STATUS_AVAILABLE) {
          memcpy(data, pkt_data, pkt_len);
          hdr->tp_len = pkt_len;
          hdr->tp_status = TP_STATUS_SEND_REQUEST;
          send(fd, NULL, 0, 0);

          break;
	}
      }
*/
    }

    if (pfd.revents & POLLIN) {
      printf("HELLO\n");

      pbd = (struct block_desc *)ring.rd[block_num].iov_base;
      int num_pkts = pbd->h1.num_pkts, i;
      unsigned long bytes = 0;
      struct tpacket3_hdr *ppd;

      ppd = (struct tpacket3_hdr *) ((uint8_t *) pbd + pbd->h1.offset_to_first_pkt);

      for (i = 0; i < num_pkts; ++i) {
        bytes += ppd->tp_snaplen;
//           display(ppd);
        ppd = (struct tpacket3_hdr *) ((uint8_t *) ppd + ppd->tp_next_offset);
      }

      packets_total += num_pkts;
      bytes_total += bytes;

      pbd->h1.block_status = TP_STATUS_KERNEL;
      block_num = (block_num + 1) % blocknum;

/*
      struct tpacket3_hdr *ppd = (struct tpacket3_hdr *) ((uint8_t *) pbd + pbd->h1.offset_to_first_pkt);
      uint32_t num_pkts = pbd->h1.num_pkts;
      uint64_t bytes = 0;
      for (uint32_t i = 0; i < num_pkts; ++i) {
        bytes += ppd->tp_snaplen;
        //printf("");
        ppd = (struct tpacket3_hdr *) ((uint8_t *) ppd +  ppd->tp_next_offset);
      }
      packets_total += num_pkts;
      bytes_total += bytes;

      pbd->h1.block_status = TP_STATUS_KERNEL;
      numblock = (numblock + 1) % nbblocks;
*/
    }
  }

  struct tpacket_stats_v3 stats;
  socklen_t len = sizeof(stats);
  if (getsockopt(fd, SOL_PACKET, PACKET_STATISTICS, &stats, &len) < 0) exit(-1);
  fflush(stdout);
  printf("\nReceived %u packets, %lu bytes, %u dropped, freeze_q_cnt: %u\n",
    stats.tp_packets, bytes_total, stats.tp_drops,
    stats.tp_freeze_q_cnt);

  munmap(ring.map, ring.req.tp_block_size * ring.req.tp_block_nr);
  free(ring.rd);
  close(fd);
}

