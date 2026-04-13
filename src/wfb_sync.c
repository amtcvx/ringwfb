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
/*
      if (s->cptack[i] == 0) {

        l->len += sprintf(l->buf + l->len, "ping (%d)(%d)\n",i,s->backfreq[i]);

        if (s->backfreq[i] == 0) { s->fdmain = i; s->fdback = -1; }
        else {

          uint8_t j=0;
          for (j=0; j < n->rawdevs[i]->nbfreqs; j++) {
            if (n->rawdevs[i]->freqs[j] == abs(s->backfreq[i])) break;
          }
          if (n->rawdevs[i]->freqs[j] == abs(s->backfreq[i])) {

            uint8_t k=0;
            if (s->backfreq[i] > 0) { s->fdmain = i; for (k=0; k<n->nbraws; k++) { if (k != i) { s->fdback = k; up = k; } } }
            else { s->fdback = i; for (k=0; k<n->nbraws; k++) if (k != i) s->fdmain = k; up = -1; }

            if ((up >= 0) && (abs(s->backfreq[i]) == n->rawdevs[up]->freqs[n->rawdevs[up]->cptfreq])) up = -1;
	    else {
              (n->rawdevs[up]->cptfreq) = j;
	      l->len += sprintf(l->buf + l->len, "main(%d) back(%d) up(%d)\n",s->fdmain,s->fdback,up);
	    }
	  }
        }
      }
      s->cptack[i]++;
    }
    if (up >= 0) wfb_netlink_setfreq(&n->sockidnl, n->rawdevs[up]->ifindex, n->rawdevs[up]->freqs[n->rawdevs[up]->cptfreq]);
  }
*/
/******************************************************************************/
void publish(wfb_sync_init_t *s, wfb_netlink_init_t *n, wfb_log_init_t *l) {

  if (s->fd[DRONEID].main >= 0) { 
    wfb_netlink_payhd_t *ptrmain = (wfb_netlink_payhd_t *)(n->msg.msgout[s->fd[DRONEID].main].msg_iov[3].iov_base);
    ptrmain->backfreq = 0;

    ptrmain->msglen = 1;
    s->com[s->fd[DRONEID].main].len = 1;
    n->msg.msgout[s->fd[DRONEID].main].msg_iov[4].iov_len = 1;
  }
/*
    if (s->fd[DRONEID].back >= 0) {
      ptrmain->backfreq = (n->rawdevs[s->fd[DRONEID].back]->freqs[n->rawdevs[s->fd[DRONEID].back]->cptfreq]);

      wfb_netlink_payhd_t *ptrback = (wfb_netlink_payhd_t *)(n->msg.msgout[s->fd[DRONEID].back].msg_iov[3].iov_base);
      ptrback->backfreq = -(n->rawdevs[s->fd[DRONEID].main]->freqs[n->rawdevs[s->fd[DRONEID].main]->cptfreq]);

      ptrback->msglen = 1;
      s->com[s->fd[DRONEID].back].len = 1;
      n->msg.msgout[s->fd[DRONEID].back].msg_iov[4].iov_len = 1;
    }
  }
*/
}

/******************************************************************************/
void wfb_sync_periodic(wfb_sync_init_t *s, wfb_netlink_init_t *n, wfb_log_init_t *l) {

  bool upfreq[n->nbraws];
  for (uint8_t i = 0; i < n->nbraws; i++) upfreq[i] = false;

  if (s->fd[DRONEID].back >= 0) {
    if (s->com[s->fd[DRONEID].back].cptfree == ACKTIME_S) {
/*
	 s->fd[DRONEID].main = i; 
      for (uint8_t i=0; i<MAXDRONE; i++) {
      if (s->com[s->fd[DRONEID].back].link[i].cptack == 0) {
        l->len += sprintf(l->buf + l->len, "ACK (%d)\n",i);
      }
*/
      }
    }
  } 

  for (uint8_t i = 0; i < n->nbraws; i++) {
    if (s->fd[DRONEID].main < 0) {
       if (s->com[i].cptfree == FREETIME_S) {
	 s->fd[DRONEID].main = i; 
         for (uint8_t j = 0; j < n->nbraws; j++) {
	   if (i != j) { 
	     s->fd[DRONEID].back = j;
	     n->rawdevs[j]->cptfreq = (n->nbraws - j - 1) * (n->rawdevs[j]->nbfreqs / n->nbraws);
	     upfreq[j] = true;
	   }
	 }
      }
//     else if ((s->fd[DRONEID].back < 0) && (s->fd[DRONEID].main != i)) s->fd[DRONEID].back = i;
    }
  }
/*
  for (uint8_t i = 0; i < n->nbraws; i++) {
    if ((s->com[i].cptfree == 0) && (s->fd[DRONEID].main == i) && (s->fd[DRONEID].back > 0))  {
      s->fd[DRONEID].main = s->fd[DRONEID].back;
      s->fd[DRONEID].back = -1;
      upfreq[i] = true;
    }
  }

  for (uint8_t i = 0; i < n->nbraws; i++) {
    if (s->com[i].cptfree == 0) {
      if (s->fd[DRONEID].back == i) { s->fd[DRONEID].back = -1; upfreq[i] = true; }
      else if (s->fd[DRONEID].main != i) upfreq[i] = true;
    }
  }
*/
  for (uint8_t i = 0; i < n->nbraws; i++) {
    if (s->com[i].cptfree == 0) {
      if ((s->fd[DRONEID].main != i) && (s->fd[DRONEID].back != i)) upfreq[i] = true;
    }
  }

  for (uint8_t i = 0; i < n->nbraws; i++) {
    if (upfreq[i]) {
      if ((++(n->rawdevs[i]->cptfreq)) >= (n->rawdevs[i]->nbfreqs)) n->rawdevs[i]->cptfreq = 0;
      for (uint8_t j=0; j<n->nbraws; j++) {
        if ((i != j) && ((n->rawdevs[i]->cptfreq) == (n->rawdevs[j]->cptfreq))) {
          if ((++(n->rawdevs[i]->cptfreq)) >= (n->rawdevs[i]->nbfreqs)) n->rawdevs[i]->cptfreq = 0;
	}
      }
      wfb_netlink_setfreq(&n->sockidnl, n->rawdevs[i]->ifindex, n->rawdevs[i]->freqs[n->rawdevs[i]->cptfreq]);
    }
  }

  for (uint8_t i = 0; i < n->nbraws; i++) {
    if (s->com[i].cptfree < FREETIME_S) s->com[i].cptfree++;
    for (uint8_t j = 0; j < MAXDRONE; j++) {
      if (s->com[i].link[j].cptack < ACKTIME_S) s->com[i].link[j].cptack++;
    }
  }

  publish(s,n,l);

  l->len += sprintf(l->buf + l->len, "main(%d)(%d) back(%d)(%d) freq(%d)(%d)\n",
    s->fd[0].main, s->fd[1].main, s->fd[0].back, s->fd[1].back,
    n->rawdevs[0]->freqs[n->rawdevs[0]->cptfreq],n->rawdevs[1]->freqs[n->rawdevs[1]->cptfreq]);

  wfb_log_send(l);
}

/******************************************************************************/
void wfb_sync_async(uint8_t rawcpt, wfb_sync_init_t *s, wfb_netlink_init_t *n, wfb_log_init_t *l) {

  wfb_netlink_payhd_t *ptr = (wfb_netlink_payhd_t *)(n->msg.msgin[rawcpt].msg_iov[3].iov_base);

  if ((*(4 + ((uint8_t *)(n->msg.msgin[rawcpt].msg_iov[1].iov_base))) == 0x66)
    && (ptr->droneid >= 0 ) && (ptr->droneid <= MAXDRONE)) { 

    s->com[rawcpt].link[ptr->droneid].cptack = 0;

    s->com[rawcpt].link[ptr->droneid].backfreq = 
      ((wfb_netlink_payhd_t *)(n->msg.msgin[rawcpt].msg_iov[3].iov_base))->backfreq;

  } else { s->com[rawcpt].cptfree = 0; }
}

/******************************************************************************/
void wfb_sync_init(wfb_sync_init_t *s, wfb_netlink_init_t *n) {

  if (-1 == (s->time.fd = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(s->time.fd, 0, &period, NULL);

  for (uint8_t i = 0; i < n->nbraws; i++) {
    n->rawdevs[i]->cptfreq = (n->nbraws - i - 1) * (n->rawdevs[i]->nbfreqs / n->nbraws);
    wfb_netlink_setfreq(&n->sockidnl, n->rawdevs[i]->ifindex, n->rawdevs[i]->freqs[n->rawdevs[i]->cptfreq]);

    s->fd[i].main = -1; s->fd[i].back = -1;

    s->com[i].cptfree = 1;

    s->com[i].len = 0;

    for (uint8_t j = 0; j < MAXDRONE; j++) s->com[i].link[j].cptack = 1;
  }
}
