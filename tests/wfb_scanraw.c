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

#include <poll.h>
#include <sys/timerfd.h>

#define PERIOD_DELAY_S 1

#define MAXDRONE 10

/*****************************************************************************/
int main(int argc, char **argv) {

  if (argc > 2)  exit(-1);

  wfb_netlink_init_t n;
  if (false == wfb_netlink_init(&n)) { printf("NO WIFI\n"); exit(-1); }

  n.rawdevs[0]->cptfreq = 0;
  if (argc == 2) {
    uint8_t i = 0;
    for (i=0;i<n.rawdevs[0]->nbfreqs; i++) if (n.rawdevs[0]->freqs[i] == atoi(argv[1])) break;
    if (n.rawdevs[0]->freqs[i] == atoi(argv[1])) n.rawdevs[0]->cptfreq = i; else exit(-1);
    wfb_netlink_setfreq(&n.sockidnl, n.rawdevs[0]->ifindex, n.rawdevs[0]->freqs[n.rawdevs[0]->cptfreq]);
  }

  uint8_t fd[2];

  uint64_t exptime;
  if (-1 == (fd[0] = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(fd[0], 0, &period, NULL);

  fd[1] = n.rawdevs[0]->sockfd;

  struct pollfd readsets[2] = { { .fd = fd[0], .events = POLLIN }, { .fd = fd[1], .events = POLLIN }};

  ssize_t rawlen = 0, len = 0;
  uint8_t rawnb = 0;

  for(;;) {
    if (0 != poll(readsets, 2, -1)) {
      for (uint8_t cpt=0; cpt<2; cpt++) {
        if (readsets[cpt].revents == POLLIN) {
          if (cpt == 0) {
            len = read(fd[0], &exptime, sizeof(uint64_t));
            printf("[%d] (%d)\n",n.rawdevs[0]->freqs[n.rawdevs[0]->cptfreq],rawnb);
            if (argc != 2) {
              if (n.rawdevs[0]->cptfreq < (n.rawdevs[0]->nbfreqs - 1)) n.rawdevs[0]->cptfreq++; else n.rawdevs[0]->cptfreq = 0;
              wfb_netlink_setfreq(&n.sockidnl, n.rawdevs[0]->ifindex, n.rawdevs[0]->freqs[n.rawdevs[0]->cptfreq]);
	    }
            rawnb = 0;
          } else {
            if (argc == 2) {
              rawlen = recvmsg(fd[1], &n.msg.msgin[0], MSG_DONTWAIT);
              rawnb++;

              wfb_netlink_payhd_t *ptr = (wfb_netlink_payhd_t *)(n.msg.msgin[cpt-1].msg_iov[2].iov_base);

              if ((*(4 + ((uint8_t *)(n.msg.msgin[cpt-1].msg_iov[1].iov_base))) == 0x66)
                && (ptr->droneid > 0 ) && (ptr->droneid <= MAXDRONE)) {
	      }
	    }
          }
	}
      }
    }
  }
}
