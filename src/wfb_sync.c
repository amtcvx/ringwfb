#include "wfb_sync.h"
#include "wfb_netlink.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/timerfd.h>

#define PERIOD_DELAY_S 1
#define FREETIME_S  5
#define ACKTIME_S   2

#define DRONEIDMAX 10

/******************************************************************************/
void periodic_slave(wfb_sync_init_t *s, wfb_netlink_init_t *n, wfb_log_init_t *l) {

  printf("slave\n");fflush(stdout);

  for (uint8_t i=0; i<n->nbraws; i++) {
    int8_t up = -1;
    if (s->cptack[i] >= ACKTIME_S) { 
      s->cptack[i] = 1;

      up = i;
      if ((++(n->rawdevs[i]->cptfreq)) >= (n->rawdevs[i]->nbfreqs)) n->rawdevs[i]->cptfreq = 0;
      for (uint8_t j=0; j<n->nbraws; j++) {
        if ((i != j) && ((n->rawdevs[i]->cptfreq) == (n->rawdevs[j]->cptfreq))) {
          if ((++(n->rawdevs[i]->cptfreq)) >= (n->rawdevs[i]->nbfreqs)) n->rawdevs[i]->cptfreq = 0;
	}
      }

    } else {
      if (s->cptack[i] == 0) {

        if (s->backfreq[i] == 0) { s->fdmain = i; s->fdback = -1; }
        else {

          uint8_t j=0;
          for (j=0; j < n->rawdevs[j]->nbfreqs; j++) {
            if (n->rawdevs[i]->freqs[j] == abs(s->backfreq[i])) break;
          }
          if (n->rawdevs[i]->freqs[j] == s->backfreq[i]) {

            uint8_t k=0;
            if (s->backfreq[i] > 0) { s->fdmain = i; for (k=0; k<n->nbraws; k++) if (k != i) s->fdback = k; }
            else if (s->backfreq[i] < 0) { s->fdback = i; for (k=0; k<n->nbraws; k++) if (k != i) s->fdmain = k; up = k; }

            (n->rawdevs[up]->cptfreq) = j;
	  }
        }
      }
      s->cptack[i]++;

    }
    if (up >= 0) wfb_netlink_setfreq(&n->sockidnl, n->rawdevs[up]->ifindex, n->rawdevs[up]->freqs[n->rawdevs[up]->cptfreq]);
  }

/*
  if (s->fdmain >= 0) { 
    wfb_netlink_payhd_t *ptrmain = (wfb_netlink_payhd_t *)(n->msg.msg_out[s->fdmain].msg_iov[3].iov_base);

    ptrmain->msglen = 1;
    s->len[s->fdmain] = 1;
    n->msg.msg_out[s->fdmain].msg_iov[4].iov_len = 1;

    if (s->fdback >= 0) {
      wfb_netlink_payhd_t *ptrback = (wfb_netlink_payhd_t *)(n->msg.msg_out[s->fdback].msg_iov[3].iov_base);

      ptrback->msglen = 1;
      s->len[s->fdback] = 1;
      n->msg.msg_out[s->fdback].msg_iov[4].iov_len = 1;
    }
  }
*/
}

/******************************************************************************/
void periodic_master(wfb_sync_init_t *s, wfb_netlink_init_t *n, wfb_log_init_t *l) {

  printf("master\n");fflush(stdout);

  if (s->fdmain >= 0) {
    if (s->cptfree[s->fdmain] == 0) {
      if (s->fdback >=0) {
        if (s->cptfree[s->fdback] != 0) s->fdmain = s->fdback;
        s->fdback = -1;
      }
    }
  }

  for (uint8_t i=0; i<n->nbraws; i++) {
    if ((s->cptfree[i] == 0) && (i != s->fdmain)) {
      if ((++(n->rawdevs[i]->cptfreq)) >= (n->rawdevs[i]->nbfreqs)) n->rawdevs[i]->cptfreq = 0;
      for (uint8_t j=0; j<n->nbraws; j++) {
        if ((i != j) && ((n->rawdevs[i]->cptfreq) == (n->rawdevs[j]->cptfreq))) {
          if ((++(n->rawdevs[i]->cptfreq)) >= (n->rawdevs[i]->nbfreqs)) n->rawdevs[i]->cptfreq = 0;
	}
      }
      wfb_netlink_setfreq(&n->sockidnl, n->rawdevs[i]->ifindex, n->rawdevs[i]->freqs[n->rawdevs[i]->cptfreq]);
    };
    if ((s->cptfree[i] == FREETIME_S) && (s->fdmain < 0)) s->fdmain = i;
  }

  for (uint8_t i=0; i<n->nbraws; i++) {
    if ((s->fdmain >= 0) && (i != s->fdmain) && (s->cptfree[i] == FREETIME_S) && (s->fdback < 0)) s->fdback = i;
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
      ptrback->backfreq = -(n->rawdevs[s->fdmain]->freqs[n->rawdevs[s->fdmain]->cptfreq]);

      ptrback->msglen = 1;
      s->len[s->fdback] = 1;
      n->msg.msg_out[s->fdback].msg_iov[4].iov_len = 1;
    }
  }
}

/******************************************************************************/
void wfb_sync_periodic(wfb_sync_init_t *s, wfb_netlink_init_t *n, wfb_log_init_t *l) {

#if DRONEID > 0
  periodic_master(s,n,l);
#else
  periodic_slave(s,n,l);
#endif

  l->len += sprintf(l->buf + l->len, "main(%d) back(%d) freq(%d)(%d)\n",
    s->fdmain, s->fdback,n->rawdevs[0]->freqs[n->rawdevs[0]->cptfreq],n->rawdevs[1]->freqs[n->rawdevs[1]->cptfreq]);

  wfb_log_send(l);
}

/******************************************************************************/
void wfb_sync_async(uint8_t rawcpt, wfb_sync_init_t *s, wfb_netlink_init_t *n, wfb_log_init_t *l) {

  wfb_netlink_payhd_t *ptr = (wfb_netlink_payhd_t *)(n->msg.msg_in[rawcpt].msg_iov[3].iov_base);

  if ((*(4 + ((uint8_t *)(n->msg.msg_in[rawcpt].msg_iov[2].iov_base))) == 0x66)
    && (ptr->droneid > 0 ) && (ptr->droneid <= MAXDRONE)) { 

    s->cptack[rawcpt] = 0;
    s->backfreq[rawcpt] = ((wfb_netlink_payhd_t *)(n->msg.msg_in[rawcpt].msg_iov[3].iov_base))->backfreq;

  } else { s->cptfree[rawcpt] = 0; }
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

    s->cptfree[i]=1; s->cptack[i]=1; s->len[i]=0;
  }
}
