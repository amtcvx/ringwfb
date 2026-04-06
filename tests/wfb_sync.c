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

#define DRONEIDMAX 10

/******************************************************************************/
void wfb_sync_async(uint8_t rawcpt, wfb_sync_init_t *s, wfb_netlink_init_t *n, wfb_log_init_t *l) {

  wfb_netlink_payhd_t *ptr = (wfb_netlink_payhd_t *)(n->msg.msg_in[rawcpt].msg_iov[3].iov_base);

  if ((ptr->droneid > 0 ) && (ptr->droneid <= DRONEIDMAX)) { 

    printf("BINGO\n"); fflush(stdout);

  } else {

    if (s->fdmain == rawcpt) {
      if (s->fdback >=0) { s->fdmain = s->fdback; s->fdback = -1; }
      else s->cptfree[rawcpt] = 0;
    } else {
      if (s->fdback == rawcpt) s->fdback = -1;
      s->cptfree[rawcpt] = 0;
    }
  }
}

/******************************************************************************/
void wfb_sync_periodic(wfb_sync_init_t *s, wfb_netlink_init_t *n, wfb_log_init_t *l) {

  for (uint8_t i=0; i<n->nbraws; i++) {
    if (s->cptfree[i] == 0) {
      if ((++(n->rawdevs[i]->cptfreq)) >= (n->rawdevs[i]->nbfreqs)) n->rawdevs[i]->cptfreq = 0;
      for (uint8_t j=0; j<n->nbraws; j++) {
        if ((i != j) && ((n->rawdevs[i]->cptfreq) == (n->rawdevs[j]->cptfreq))) {
          if ((++(n->rawdevs[i]->cptfreq)) >= (n->rawdevs[i]->nbfreqs)) n->rawdevs[i]->cptfreq = 0;
	}
      }
      wfb_netlink_setfreq(&n->sockidnl, n->rawdevs[i]->ifindex, n->rawdevs[i]->freqs[n->rawdevs[i]->cptfreq]);
    };
    if (s->cptfree[i] == FREETIME_S) {
      if (s->fdmain == -1) s->fdmain = i; else if (s->fdback == -1) s->fdback = i;
    }
    if (s->cptfree[i] < FREETIME_S) s->cptfree[i]++;
  }

  if (s->fdmain >= 0) { 
    wfb_netlink_payhd_t *ptrmain = (wfb_netlink_payhd_t *)(n->msg.msg_out[s->fdmain].msg_iov[3].iov_base);
    ptrmain->backfreq = 0;

    ptrmain->msglen = 1;
    s->len[s->fdmain] = 1;
    n->msg.msg_out[s->fdmain].msg_iov[4].iov_len = 1;

    if (s->fdback >= 0) {
      ptrmain->backfreq = (n->rawdevs[s->fdback]->freqs[n->rawdevs[s->fdback]->cptfreq]);

      wfb_netlink_payhd_t *ptrback = (wfb_netlink_payhd_t *)(n->msg.msg_out[s->fdback].msg_iov[3].iov_base);
      ptrback->backfreq = n->rawdevs[s->fdmain]->freqs[n->rawdevs[s->fdmain]->cptfreq];

      ptrback->msglen = 1;
      s->len[s->fdback] = 1;
      n->msg.msg_out[s->fdback].msg_iov[4].iov_len = 1;
    }
  }

  l->len += sprintf(l->buf + l->len, "main(%d) back(%d) freq(%d)(%d)\n",
    s->fdmain, s->fdback,n->rawdevs[0]->freqs[n->rawdevs[0]->cptfreq],n->rawdevs[1]->freqs[n->rawdevs[1]->cptfreq]);

  wfb_log_send(l);
}

/******************************************************************************/
void wfb_sync_init(wfb_sync_init_t *s, wfb_netlink_init_t *n) {

  if (-1 == (s->fd = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(s->fd, 0, &period, NULL);

  s->fdmain = -1; s->fdback = -1;

  for (uint8_t i=0; i<n->nbraws; i++) {
    n->rawdevs[i]->cptfreq = (n->nbraws - i - 1) * (n->rawdevs[i]->nbfreqs / n->nbraws);
    wfb_netlink_setfreq(&n->sockidnl, n->rawdevs[i]->ifindex, n->rawdevs[i]->freqs[n->rawdevs[i]->cptfreq]);

    s->cptfree[i]=0; s->len[i]=0;
  }
}
