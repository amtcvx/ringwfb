/*
gcc packet_mmap.c -o packet_nmap
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

/******************************************************************************/
int main(int argc, char **argv) {

  int rxsock;
  if ((rxsock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0 ) exit(-1);;

  int i = TPACKET_V3;
  if (setsockopt( rxsock, SOL_PACKET, PACKET_VERSION, &i, sizeof(i) ) < 0 ) exit(-1);

  struct ifreq ifr;
  strcpy( ifr.ifr_name, argv[1] );
  if (ioctl( rxsock, SIOCGIFFLAGS, &ifr) == -1) exit(-1);
  ifr.ifr_flags |= ( IFF_PROMISC | IFF_UP );
  if (ioctl( rxsock, SIOCSIFFLAGS, &ifr ) == -1 ) exit(-1);

  struct tpacket_req3 req = {
    .tp_block_size = 1 << 22,
    .tp_frame_size = 1 << 11,
    .tp_block_nr = 64,
  };
  req.tp_frame_nr = (req.tp_block_size * req.tp_block_nr) / req.tp_frame_size;

  if (setsockopt( rxsock, SOL_PACKET, PACKET_RX_RING, &req, sizeof(struct tpacket_req3) ) < 0 ) exit(-1);

  uint8_t *const rxmap = mmap( NULL, req.tp_block_size * req.tp_block_nr, PROT_READ|PROT_WRITE, MAP_SHARED | MAP_LOCKED, rxsock, 0 );
  if (rxmap == MAP_FAILED ) exit(-1);

  struct sockaddr_ll ll = {
    .sll_family = PF_PACKET,
    .sll_protocol = htons(ETH_P_ALL),
    .sll_ifindex = if_nametoindex(ifr.ifr_name)
  };
  if (bind( rxsock, (struct sockaddr *) &ll, sizeof(ll)) < 0) exit(-1);

  struct pollfd pfd = {
    .fd = rxsock,
    .events = POLLIN | POLLERR,
    .revents = 0
  };


  int txsock;
  if ((txsock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0 ) exit(-1);
  struct sockaddr_ll my_addr = {
    .sll_family = AF_PACKET,
    .sll_protocol = htons(ETH_P_ALL),
    .sll_ifindex = (int)if_nametoindex(argv[1])
  };
 
  if (bind( txsock, (struct sockaddr *)&my_addr, sizeof(struct sockaddr_ll) ) == -1 ) exit(-1);

  struct tpacket_req s_packet_req = {
    .tp_block_size = 1<<12,
    .tp_frame_size = 1<<12,
    .tp_block_nr = 10
  };
  s_packet_req.tp_frame_nr = (s_packet_req.tp_block_size*s_packet_req.tp_block_nr)/s_packet_req.tp_frame_size;
 
  if (setsockopt( txsock, SOL_PACKET, PACKET_TX_RING, (char *)&s_packet_req, sizeof(s_packet_req) ) < 0 ) exit(-1);

  int one = 1; // MUST BE SET to prevent TX in RX (seen also with wireshark)
  if (setsockopt(txsock, SOL_PACKET, PACKET_QDISC_BYPASS, &one, sizeof(one)) < 0 ) exit(-1);

  struct tpacket_hdr * const txmap = mmap(0, s_packet_req.tp_block_size * s_packet_req.tp_block_nr, PROT_WRITE, MAP_SHARED, txsock, 0);
  if (txmap == MAP_FAILED ) exit(-1);


  struct timespec ts;
  uint64_t curms,  stoms, intms = 1000;
  clock_gettime(CLOCK_MONOTONIC, &ts); stoms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

  int block_pos = 0;
  int pkt_num = 0;
  for( block_pos = 0; 1; block_pos = ((block_pos+1) % req.tp_block_nr) ) {

    struct block_desc {
      uint32_t version;
      uint32_t offset_to_priv;
      struct tpacket_hdr_v1 h1;
    } *pbd = (struct block_desc *) ( rxmap + (block_pos * req.tp_block_size) );

    if( ( pbd->h1.block_status & TP_STATUS_USER ) == 0 ) {
      clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
      poll(&pfd, 1, stoms > curms ? stoms - curms : 0);
      clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
      if (curms >= stoms) { // SYNCHRONOUS
        stoms = curms + intms - ((curms - stoms) % intms);
	printf("TIC\n");

        const int c_packet_sz = 200;
        for (uint32_t i=0; i<s_packet_req.tp_block_nr; i++ ) {
          struct tpacket_hdr * ps_header = ((struct tpacket_hdr *)((void *)txmap + (s_packet_req.tp_block_size*i)));
          #define my_TPACKET_ALIGN(x)	(((x)+(uint64_t)(TPACKET_ALIGNMENT-1))&~((uint64_t)(TPACKET_ALIGNMENT-1)))
          char * pkt_ptr = ((void*) ps_header) + my_TPACKET_ALIGN(sizeof(struct tpacket_hdr));
          for(int j=0; j<c_packet_sz; j++ )  pkt_ptr[j] = j;
          ps_header->tp_len = (uint32_t)c_packet_sz;
          ps_header->tp_status = TP_STATUS_SEND_REQUEST;
        }
 	int total_pkts = 0, ec_send, total_bytes = 0;
	while (total_pkts<s_packet_req.tp_block_nr ) {
	  if ((ec_send = sendto( txsock, NULL, 0, MSG_DONTWAIT, NULL, sizeof(struct sockaddr_ll))) < 0) exit(-1);
	  else  {
	    total_pkts += ec_send/(c_packet_sz);
	    total_bytes += ec_send;
	    printf("%s %d send %d packets (+%d bytes)\n", __func__, __LINE__, total_pkts, total_bytes );
	  }
        }

      }
    }
    printf( "number of packets captured: %d\n", pbd->h1.num_pkts );

    struct tpacket3_hdr *ppd = (struct tpacket3_hdr *) ((uint8_t *) pbd + pbd->h1.offset_to_first_pkt);
    for(int i = 0; i<pbd->h1.num_pkts; i++ ) {
      printf( "packet Number: %d packet size: %d\n", pkt_num++, ppd->tp_snaplen );
      uint8_t *pkt_ptr = ((uint8_t *) ppd + ppd->tp_mac);
      for(int j=0; j<ppd->tp_snaplen; j++ ) {
        printf( "%02X ", pkt_ptr[j] );
      }
      printf("\n");
      ppd = (struct tpacket3_hdr *) ((uint8_t *) ppd + ppd->tp_next_offset);
    }
    pbd->h1.block_status = TP_STATUS_KERNEL;
  }

  munmap(rxmap, req.tp_block_size * req.tp_block_nr);
  munmap(txmap, s_packet_req.tp_block_size * s_packet_req.tp_block_nr );
  close(rxsock); close(txsock);
}
