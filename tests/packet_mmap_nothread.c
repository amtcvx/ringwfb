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

/******************************************************************************/
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

  printf("block_nr(%d) block_size(%d) frame_nr(%d) frame_size(%d)\n",block_nr,block_size,frame_nr,frame_size);

  if (-1 == setsockopt (sockfd, SOL_PACKET, PACKET_RX_RING, &packet_req[0], sizeof(struct tpacket_req3))) exit(-1); // FIRST
  if (-1 == setsockopt (sockfd, SOL_PACKET, PACKET_TX_RING, &packet_req[1], sizeof(struct tpacket_req3))) exit(-1); // SECOND

  size_t map_size = block_size * block_nr;
  uint8_t *map[2]; if (MAP_FAILED == (map[0] = mmap(0, map_size * 2, PROT_READ|PROT_WRITE, MAP_SHARED, sockfd, 0))) exit(-1);
  map[1] = map[0] + map_size;

  /*---------------------------------------------------------------------*/
/*
https://csulrong.github.io/blogs/2022/03/10/linux-afpacket/
     

   [..................................................... tpacket_block_desc ........................................................ ]
                                [ ............................................ packet_hdr_v1 .........................................]
 block                       block_hdr
   |                            |
   [ version ][ offset_to_priv ][ bloc_status ][ num_pkts ][ offset_to_first_pkt ][ blk_len ][ seq_num ][ ts_first_pkt ][ ts_last_pkt ]
                                                                      |
                                                                    - [ tp_next_offset ] -> (*)
					                            |   [ tp_sec ]
								    |     [ tp_nsec ]
								    |	    [ tp_snaplen ]
								    |	      [ tp_len ]
						      tpacket3_hdr  |           [ tp_status ]
								    |	          [ tp_mac ]
								    |		    [ tp_net ]
								    |		      [ tp_rxhash ]
							            |	                [ tp_vlan_tci ] 
								    ...................................| ... TPACKET_ALIGNMENT ...[ struct sockaddrl_ll ][ packet data ]

			            TPACKET3_HDRLEN = tpacket3_hdr + TPACKET_ALIGNMENT + [ struct sockaddrl_ll ]

                                                               (*)  - [ tp_next_offset ] 
					                            |   [ tp_sec ]
								    |     [ tp_nsec ]
								    |	    [ tp_snaplen ]
								    |	      [ tp_len ]
						      tpacket3_hdr  |           [ tp_status ]
								    |	          [ tp_mac ]
								    |		    [ tp_net ]
								    |		      [ tp_rxhash ]
							            |	                [ tp_vlan_tci ] 
								    ...................................| ... TPACKET_ALIGNMENT ...[ struct sockaddrl_ll ][ packet data ]

*/

  uint8_t packet[PAY_MTU]; uint16_t packet_len = PAY_MTU;

  struct tblock_desc {
    uint32_t version;
    uint32_t offset_to_priv;
    struct tpacket_hdr_v1 h1;
  } *pblock_desc;
/*
  pblock_desc = (struct tblock_desc *)map[1];
  pblock_desc->version = 3; pblock_desc->offset_to_priv = 0; 
  pblock_desc->h1.block_status = TP_STATUS_SEND_REQUEST; 
  pblock_desc->h1.num_pkts = 1;
  pblock_desc->h1.offset_to_first_pkt = 0;
  pblock_desc->h1.blk_len = map_size;
  pblock_desc->h1.seq_num = 0;

  struct tpacket3_hdr *packet_hdr = (struct tpacket3_hdr *)(map[1] + sizeof(struct tblock_desc));
  packet_hdr->tp_next_offset = 0;
  packet_hdr->tp_len = PAY_MTU;
  memcpy( map[1] + sizeof(struct tblock_desc) + TPACKET3_HDRLEN, packet, packet_len);
*/
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


      for(int i=0; i < frame_nr; i++ ) {
        struct tpacket3_hdr *hdr = (void*)(map[1] + (frame_size * i));
        uint8_t *data = (uint8_t*)hdr + TPACKET_ALIGN(sizeof(struct tpacket3_hdr));
        if (hdr->tp_status == TP_STATUS_AVAILABLE) {
          memcpy(data, packet, packet_len);
          hdr->tp_len = packet_len;
          hdr->tp_status = TP_STATUS_SEND_REQUEST;
        }
      }
      tosend = true;
    }

    if (pfd.revents & POLLIN) {
      struct tpacket3_hdr * rx_header = ((struct tpacket3_hdr *)((void *)map[0] + (block_size * rx_block_nr)));
      struct tblock_desc *rx_pbd = (struct tblock_desc *) rx_header;
      if ((rx_header->tp_status & TP_STATUS_USER) == 0) { 
        rx_pbd->h1.block_status = TP_STATUS_KERNEL;
        rx_block_nr = (rx_block_nr + 1) % block_nr;
        int num_pkts = rx_pbd->h1.num_pkts, i;
        unsigned long bytes = 0;
        struct tpacket3_hdr *ppd = (struct tpacket3_hdr *) ((uint8_t *) rx_pbd + rx_pbd->h1.offset_to_first_pkt);
        for (i = 0; i < num_pkts; ++i) {
          bytes += ppd->tp_snaplen;
	  printf("RECV(%ld)\n",bytes);
//                display(ppd);
          ppd = (struct tpacket3_hdr *) ((uint8_t *) ppd + ppd->tp_next_offset);
        }
        total_pkts += num_pkts;
        total_bytes += bytes;
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
