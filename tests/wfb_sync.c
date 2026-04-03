/*
gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -c wfb_sync.c -o wfb_sync.o
*/


#include "wfb_sync.h"
#include "wfb_netlink.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/timerfd.h>

#define PERIOD_DELAY_S 1
#define FREETIME_S  5

/******************************************************************************/
void wfb_sync_periodic(wfb_sync_init_t *s, wfb_netlink_init_t *n, wfb_log_init_t *l) {

  for (uint8_t i=0; i<n->nbraws; i++) {
    if (s->nbpkt[i] != 0) {

      if ((++(n->rawdevs[i]->cptfreq)) >= (n->rawdevs[i]->nbfreqs)) n->rawdevs[i]->cptfreq = 0;

      for (uint8_t j=0; j<n->nbraws; j++) {
        if ((i != j) && ((n->rawdevs[i]->cptfreq) == (n->rawdevs[j]->cptfreq))) {
          if ((++(n->rawdevs[i]->cptfreq)) >= (n->rawdevs[i]->nbfreqs)) n->rawdevs[i]->cptfreq = 0;
	}
      }

      l->len += sprintf(l->buf + l->len, "freq update (%d)(%d)\n", n->rawdevs[i]->ifindex, n->rawdevs[i]->freqs[n->rawdevs[i]->cptfreq]);

      wfb_netlink_setfreq(&n->sockidnl, n->rawdevs[i]->ifindex, n->rawdevs[i]->freqs[n->rawdevs[i]->cptfreq]);

      s->nbpkt[i]=0;
      s->cptfree[i]=0;

    } else {
      if (s->cptfree[i] < FREETIME_S) (s->cptfree[i])++;
      else {
        l->len += sprintf(l->buf + l->len, "freq free (%d)(%d)\n", n->rawdevs[i]->ifindex, n->rawdevs[i]->freqs[n->rawdevs[i]->cptfreq]);
      }
    }
  }

  l->len += sprintf(l->buf + l->len, "freq (%d)(%d)  (%d)(%d)\n", 
    n->rawdevs[0]->ifindex, n->rawdevs[0]->freqs[n->rawdevs[0]->cptfreq],
    n->rawdevs[1]->ifindex, n->rawdevs[1]->freqs[n->rawdevs[1]->cptfreq]);

  wfb_log_send(l);
}

/******************************************************************************/
void wfb_sync_init(wfb_sync_init_t *s, wfb_netlink_init_t *n) {

  if (-1 == (s->fd = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(s->fd, 0, &period, NULL);

  for (uint8_t i=0; i<n->nbraws; i++) {
    n->rawdevs[i]->cptfreq = (n->nbraws - i - 1) * (n->rawdevs[i]->nbfreqs / n->nbraws);
    wfb_netlink_setfreq(&n->sockidnl, n->rawdevs[i]->ifindex, n->rawdevs[i]->freqs[n->rawdevs[i]->cptfreq]);
    s->nbpkt[i]=0;
  }
}
