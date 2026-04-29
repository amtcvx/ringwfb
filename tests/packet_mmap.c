/*
gcc -g packet_mmap.c -lm -o packet_nmap

http://paul.chavent.free.fr/packet_mmap.html
    
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

  struct ifreq ifr;
  strcpy( ifr.ifr_name, argv[1] );
  struct sockaddr_ll ll = {
    .sll_family = PF_PACKET,
    .sll_protocol = htons(ETH_P_ALL),
    .sll_ifindex = if_nametoindex(ifr.ifr_name)
  }; 
  if (bind( fd, (struct sockaddr *) &ll, sizeof(ll)) < 0) exit(-1);

  int i = TPACKET_V2;
  if (setsockopt( fd, SOL_PACKET, PACKET_VERSION, &i, sizeof(i) ) < 0 ) exit(-1);

  int tx_has_off = 1;
  if (setsockopt(fd, SOL_PACKET,  PACKET_TX_HAS_OFF, &tx_has_off, sizeof(tx_has_off)) < 0) exit(-1);

  uint16_t mtu = 1500;
  struct tpacket_req rx_paquet_req;
  rx_paquet_req.tp_block_size = sysconf(_SC_PAGESIZE) << 1; 
  rx_paquet_req.tp_block_nr = 2;
  rx_paquet_req.tp_frame_size = pow(2, ceil(log(mtu + 128)/log(2)));
  rx_paquet_req.tp_frame_nr = (rx_paquet_req.tp_block_size / rx_paquet_req.tp_frame_size) * rx_paquet_req.tp_block_nr;

  struct tpacket_req tx_paquet_req;
  tx_paquet_req.tp_block_size = sysconf(_SC_PAGESIZE) << 1; 
  tx_paquet_req.tp_block_nr = 2;
  tx_paquet_req.tp_frame_size = pow(2, ceil(log(mtu + 128)/log(2))); // next power of two
  tx_paquet_req.tp_frame_nr = (tx_paquet_req.tp_block_size / tx_paquet_req.tp_frame_size) * tx_paquet_req.tp_block_nr;

  if (setsockopt(fd, SOL_PACKET, PACKET_RX_RING, &rx_paquet_req, sizeof(rx_paquet_req)) < 0) exit(-1);
  if (setsockopt(fd, SOL_PACKET, PACKET_TX_RING, &tx_paquet_req, sizeof(tx_paquet_req)) < 0) exit(-1);

  int mmap_size = 
    rx_paquet_req.tp_block_size * rx_paquet_req.tp_block_nr +
    tx_paquet_req.tp_block_size * tx_paquet_req.tp_block_nr ;
  void *mmap_base = mmap(0, mmap_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (mmap_base == MAP_FAILED ) exit(-1);

  int rx_buffer_size = rx_paquet_req.tp_block_size * rx_paquet_req.tp_block_nr;
  void *rx_buffer_addr = mmap_base;
  int rx_buffer_idx  = 0;
  int rx_buffer_cnt  = rx_paquet_req.tp_block_size * rx_paquet_req.tp_block_nr / rx_paquet_req.tp_frame_size;
  int tx_buffer_size = tx_paquet_req.tp_block_size * tx_paquet_req.tp_block_nr;
  void *tx_buffer_addr = mmap_base + rx_buffer_size;
  int tx_buffer_idx  = 0;
  int tx_buffer_cnt  = tx_paquet_req.tp_block_size * tx_paquet_req.tp_block_nr / tx_paquet_req.tp_frame_size;

  void *base = rx_buffer_addr + rx_buffer_idx * rx_paquet_req.tp_frame_size;
  volatile struct tpacket2_hdr *header = (struct tpacket2_hdr *)base;

  struct pollfd pfd = {
    .fd = fd,
    .events = POLLIN | POLLERR,
    .revents = 0
  };

  struct timespec ts;
  uint64_t curms,  stoms, intms = 1000;
  clock_gettime(CLOCK_MONOTONIC, &ts); stoms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

  while(1) {
    clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
    poll(&pfd, 1, stoms > curms ? stoms - curms : 0);
    clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
    if (curms >= stoms) { // SYNCHRONOUS
      stoms = curms + intms - ((curms - stoms) % intms);
      printf("TIC\n");
    }
    if (pfd.revents & POLLIN) {

      if( ( header->tp_status & TP_STATUS_USER ) == 0 ) {

//        for( block_pos = 0; 1; block_pos = ((block_pos+1) % req.tp_block_nr) ) {

//      if( ( header->tp_status & TP_STATUS_USER ) == 0 ) {

        printf("DATA\n"); fflush(stdout);
/*
        void * data = base + header->tp_net;
        unsigned data_len = header->tp_snaplen;
        struct timespec ts;
        ts.tv_sec  = header->tp_sec;
        ts.tv_nsec = header->tp_nsec;
        printf("captured [%d]\n",data_len);
*/
        header->tp_status = TP_STATUS_KERNEL;
      }
    }
  }
}
/*


  struct tpacket_req3 req = {
    .tp_block_size = 1 << 22,
    .tp_frame_size = 1 << 11,
    .tp_block_nr = 64,
  };
  req.tp_frame_nr = (req.tp_block_size * req.tp_block_nr) / req.tp_frame_size;
  if (setsockopt( sock, SOL_PACKET, PACKET_RX_RING, &req, sizeof(struct tpacket_req3) ) < 0 ) exit(-1);

  struct tpacket_req3 s_packet_req = {
    .tp_block_size = 1<<12,
    .tp_frame_size = 1<<12,
    .tp_block_nr = 10
  };
  s_packet_req.tp_frame_nr = (s_packet_req.tp_block_size * s_packet_req.tp_block_nr) / s_packet_req.tp_frame_size;
  if (setsockopt( sock, SOL_PACKET, PACKET_TX_RING, &s_packet_req, sizeof(struct tpacket_req3) ) < 0 ) exit(-1);

  struct ifreq ifr;
  strcpy( ifr.ifr_name, argv[1] );
  if (ioctl( sock, SIOCGIFFLAGS, &ifr) == -1) exit(-1);
  ifr.ifr_flags |= ( IFF_PROMISC | IFF_UP );
  if (ioctl( sock, SIOCSIFFLAGS, &ifr ) == -1 ) exit(-1);

  uint8_t *const rxmap = mmap( NULL, req.tp_block_size * req.tp_block_nr, PROT_READ, MAP_SHARED, sock, 0 );
  printf("(%s)\n",strerror(errno));
  y
  if (rxmap == MAP_FAILED ) exit(-1);

  printf("H4LLO\n");fflush(stdout);
  exit(-1);

  uint8_t *const rxmap = mmap( NULL, req.tp_block_size * req.tp_block_nr, PROT_READ, MAP_SHARED, sock, 0 );
  printf("(%s)\n",strerror(errno));

  if (rxmap == MAP_FAILED ) exit(-1);

  struct tpacket_hdr * const txmap = mmap(0, s_packet_req.tp_block_size * s_packet_req.tp_block_nr, PROT_WRITE, MAP_SHARED, sock, 0);
  if (txmap == MAP_FAILED ) exit(-1);

  int one = 1; // MUST BE SET to prevent TX in RX (seen also with wireshark)
  if (setsockopt(sock, SOL_PACKET, PACKET_QDISC_BYPASS, &one, sizeof(one)) < 0 ) exit(-1);

  struct sockaddr_ll ll = {
    .sll_family = PF_PACKET,
    .sll_protocol = htons(ETH_P_ALL),
    .sll_ifindex = if_nametoindex(ifr.ifr_name)
  }; 
  if (bind( sock, (struct sockaddr *) &ll, sizeof(ll)) < 0) exit(-1);
  
  struct pollfd pfd = {
    .fd = sock,
    .events = POLLIN | POLLERR,
    .revents = 0
  };

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
	  if ((ec_send = sendto( sock, NULL, 0, MSG_DONTWAIT, NULL, sizeof(struct sockaddr_ll))) < 0) exit(-1);
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
  close(sock);
*/
