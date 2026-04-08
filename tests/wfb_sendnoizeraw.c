/*
gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c wfb_sendnoizeraw.c -o wfb_sendnoizeraw.o

cc wfb_netlink.o wfb_sendnoizeraw.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o exe_wfb_sendnoizeraw
*/

#include "wfb_netlink.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

/*****************************************************************************/
int main(int argc, char **argv) {

  if ((argc < 3)||(argc > 4)) exit(-1);

  wfb_netlink_init_t n;
  if (false == wfb_netlink_init(&n)) { printf("NO WIFI\n"); exit(-2); }
  uint32_t freq = atoi(argv[2]);
  wfb_netlink_setfreq(&n.sockidnl, n.rawdevs[0]->ifindex, freq);

  if (argc == 4) {
    uint32_t mcs = atoi(argv[3]);
    if (mcs > 31) exit(-1);
    else (*(12 + ((uint8_t *)(n.msg.msgout[0].msg_iov[0].iov_base)))) = (uint8_t)mcs;
  }

  do {
    ssize_t rawlen = sendmsg(n.rawdevs[0]->sockfd, &n.msg.msgout[0], MSG_DONTWAIT);
    printf("(%ld)(%d)\n",rawlen,freq);
    sleep(1);
} while (argc == 4);
}
