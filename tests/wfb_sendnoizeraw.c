/*
gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c wfb_sendnoizeraw.c -o wfb_sendnoizeraw.o

cc wfb_netlink.o wfb_sendnoizeraw.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o exe_wfb_sendnoizeraw

sudo ./exe_wfb_sendnoizeraw 2484
sudo ./exe_wfb_sendnoizeraw 2484 4

*/

#include "wfb_netlink.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

/*****************************************************************************/
int main(int argc, char **argv) {

  if ((argc < 2)||(argc > 3))  exit(-1);

  wfb_netlink_init_t n;
  if (false == wfb_netlink_init(&n)) { printf("NO WIFI\n"); exit(-2); }
  uint32_t freq = atoi(argv[1]);
  wfb_netlink_setfreq(&n.sockidnl, n.rawdevs[0]->ifindex, freq);

  uint8_t mcs = *(12 + ((uint8_t *)(n.msg.msgout[0].msg_iov[0].iov_base)));
  if (argc == 3) {
    mcs = atoi(argv[2]);
    if (mcs > 31) exit(-1);
    else (*(12 + ((uint8_t *)(n.msg.msgout[0].msg_iov[0].iov_base)))) = mcs;
  }
  do {
    ssize_t rawlen = sendmsg(n.rawdevs[0]->sockfd, &n.msg.msgout[0], MSG_DONTWAIT);
    printf("(%ld)(%d)(%d)\n",rawlen,freq, mcs);
    sleep(1);
  } while (argc == 3);
}
