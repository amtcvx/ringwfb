/*
gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -c wfb_sync.c -o wfb_sync.o
*/


#include "wfb_sync.h"
#include "wfb_netlink.h"

#include <stdlib.h>
#include <sys/timerfd.h>

#define PERIOD_DELAY_S 1

/******************************************************************************/
void wfb_sync_periodic(wfb_sync_init_t *s, wfb_netlink_init_t *n) {

  for (uint8_t i=0; i<n->nbraws; i++) {
    if (s->nbpkt[i] != 0)  s->nbpkt[i]=0;
  }
}

/******************************************************************************/
void wfb_sync_init(wfb_sync_init_t *s, wfb_netlink_init_t *n) {

  if (-1 == (s->fd = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(s->fd, 0, &period, NULL);

  for (uint8_t i=0; i<n->nbraws; i++) {
    s->nbpkt[i]=0;

    n->rawdevs[i]->freq = (n->nbraws - i - 1) * (n->rawdevs[i]->nbfreqs / n->nbraws);
    wfb_netlink_setfreq(&n->sockidnl, n->rawdevs[i]->ifindex, n->rawdevs[i]->freqs[n->rawdevs[i]->freq]);
  }
}
