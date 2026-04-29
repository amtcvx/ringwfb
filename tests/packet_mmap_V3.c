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

/******************************************************************************/
int main(int argc, char **argv) {

  int fd;
  if ((fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0 ) exit(-1);;

  int i = TPACKET_V3;
  if (setsockopt( fd, SOL_PACKET, PACKET_VERSION, &i, sizeof(i) ) < 0 ) exit(-1);

  int tx_has_off = 1;
  if (setsockopt(fd, SOL_PACKET,  PACKET_TX_HAS_OFF, &tx_has_off, sizeof(tx_has_off)) < 0) exit(-1);

  uint8_t nbblocks = 128;
  struct ring {
    struct iovec rd[nbblocks][sizeof(struct iovec)];
    struct tpacket_req3 req;
    uint8_t *map;
  } ring;

  memset(&ring.req, 0, sizeof(struct tpacket_req3));
  ring.req.tp_block_size = 4096;
  ring.req.tp_frame_size = 2048;
  ring.req.tp_block_nr = nbblocks;
  ring.req.tp_frame_nr = (ring.req.tp_block_size * ring.req.tp_block_nr) / ring.req.tp_frame_size;
  ring.req.tp_retire_blk_tov = 60;
  ring.req.tp_feature_req_word = TP_FT_REQ_FILL_RXHASH;

  if (setsockopt(fd, SOL_PACKET, PACKET_RX_RING, &ring.req, sizeof(struct tpacket_req3)) < 0) exit(-1);

  ring.map = mmap(NULL, ring.req.tp_block_size * ring.req.tp_block_nr, PROT_READ, MAP_SHARED, fd, 0);
  if (ring.map == MAP_FAILED ) exit(-1);

  if (ring.rd == NULL) exit(-1);
  for (uint8_t i = 0; i < ring.req.tp_block_nr; ++i) {
    ring.rd[i]->iov_base = ring.map + (i * ring.req.tp_block_size);
    ring.rd[i]->iov_len = ring.req.tp_block_size;
  }


  struct sockaddr_ll ll = {
    .sll_family = PF_PACKET,
    .sll_protocol = htons(ETH_P_ALL),
    .sll_ifindex = if_nametoindex(argv[1])
  };
  if (bind( fd, (struct sockaddr *) &ll, sizeof(ll)) < 0) exit(-1);

  struct pollfd pfd = {
    .fd = fd,
    .events = POLLIN | POLLERR,
    .revents = 0
  };

  struct timespec ts;
  uint64_t curms,  stoms, intms = 1000;
  clock_gettime(CLOCK_MONOTONIC, &ts); stoms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

  struct block_desc {
    uint32_t version;
    uint32_t offset_to_priv;
    struct tpacket_hdr_v1 h1;
  } *pbd; 

  uint8_t numblock = 0;
  uint64_t packets_total = 0, bytes_total = 0;

  while(1) {
    clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
    poll(&pfd, 1, stoms > curms ? stoms - curms : 0);
    clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
    if (curms >= stoms) { // SYNCHRONOUS
      stoms = curms + intms - ((curms - stoms) % intms);
      printf("TIC\n");

      struct tpacket3_hdr *hdr = NULL;
      for (i = 0; i < ring.req.tp_frame_nr; i += 1) {
//        hdr = (void*)(ring + (ring.req.tp_frame_size * i));
        void *data = (uint8_t*)hdr + TPACKET_ALIGN(sizeof(struct tpacket3_hdr));

        if (hdr->tp_status == TP_STATUS_AVAILABLE) {
//          memcpy(data, pkt_data, pkt_len);
//          hdr->tp_len = pkt_len;
          hdr->tp_status = TP_STATUS_SEND_REQUEST;
          send(fd, NULL, 0, 0);

          break;
	}
      }
    }

    pbd = (struct block_desc *) ring.rd[numblock]->iov_base;
    if ((pbd->h1.block_status & TP_STATUS_USER) == 0) {
      printf("HELLO\n");

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
    }
  }
}

