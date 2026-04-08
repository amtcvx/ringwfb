/*
gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c wfb_scanraw.c -o wfb_scanraw.o

cc wfb_netlink.o wfb_scanraw.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o exe_wfb_scanraw

sudo ./exe_wfb_scanraw
sudo ./exe_wfb_scanraw 2484

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

}
