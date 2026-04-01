/*
gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -c toto.c -o toto.o

cc  titi.o toto.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o exe_toto
*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/socket.h>

#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>

#include <unistd.h>

#include "titi.h"

/*****************************************************************************/
int main(int argc, char **argv) {

  uint8_t sockfd;
  uint16_t protocol = 0;
  if (-1 == (sockfd = socket(AF_PACKET,SOCK_RAW,protocol))) exit(-1);

  struct sockaddr_ll sll;
  memset( &sll, 0, sizeof( sll ) );
  sll.sll_family   = AF_PACKET;
  sll.sll_ifindex  = 65;
  sll.sll_protocol = protocol;
  if (-1 == bind(sockfd, (struct sockaddr *)&sll, sizeof(sll))) exit(-1);

  uint8_t dumbuf[1400] = {-1};
  struct iovec iovdum = { .iov_base = dumbuf, .iov_len = sizeof(dumbuf) };
  struct iovec iovtab[4] = { iov_radiotaphd_tx, iov_ieeehd_tx, iov_llchd_tx, iovdum };
  struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 4 };

  do {
    ssize_t rawlen = sendmsg(sockfd, (const struct msghdr *)&msg, MSG_DONTWAIT);
    printf("(%ld)\n",rawlen);
    sleep(1);
  } while (true);
}
