#include "wfb_utils.h"

#include <sys/timerfd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define PORT_LOG 5000
#define PORT_EXT 5010

#define PORT_VID 5100

#define PERIOD_DELAY_S  1

#define IP_LOCAL "127.0.0.1"

typedef struct {
  uint8_t droneid;
  uint32_t seq;
  uint16_t len;
  uint8_t dummy;
} __attribute__((packed)) wfb_utils_heads_pay_t;

const char IP_TAB[MAXDRONE+1][EXT_NB][15] = { 
  { "192.168.1.100", "192.168.2.100" }, 
  { "192.168.1.1", "192.168.4.1" }, 
  { "192.168.2.1", "192.168.3.2" }, 
  { "192.168.3.1", "192.168.4.2" } };

/*****************************************************************************/
void init_sock(uint8_t role, wfb_utils_fd_t *pfd, uint16_t port, const char *pin, const char *pout) {

  if (-1 == (pfd->id = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  if (-1 == setsockopt(pfd->id, SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);

  if (role != 1) { 
    struct sockaddr_in  inaddr;
    inaddr.sin_family = AF_INET;
    inaddr.sin_port = htons(port);
    inaddr.sin_addr.s_addr = inet_addr(pin);
    if (-1 == bind( pfd->id, (const struct sockaddr *)&inaddr, sizeof(inaddr))) exit(-1);
  }

  if (role != 0) {
    pfd->outaddr.sin_family = AF_INET;
    pfd->outaddr.sin_port = htons(port);
    pfd->outaddr.sin_addr.s_addr = inet_addr(pout);
    memcpy(&pfd->ipstr, pout, sizeof(pfd->ipstr));
  }
}

/*****************************************************************************/
void wfb_utils_init(wfb_utils_init_t *u) {

  u->log.len =0; memset(u->log.buf, 0, sizeof(u->log.buf));
  init_sock(1, &u->log.fd, PORT_LOG, (char *)0, IP_LOCAL);

  u->readnb = 0;
  if (-1 == (u->devtab[0].fd.id = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(u->devtab[0].fd.id, 0, &period, NULL);
  u->readsets[0].fd = u->devtab[0].fd.id; u->readsets[0].events = POLLIN; u->readnb++;

  for (uint8_t cpt = 1; cpt < EXT_NB; cpt++) {
    init_sock(2, &u->devtab[cpt].fd, PORT_EXT,
      (IP_TAB[DRONEID][(cpt - 1) % (EXT_NB - 1)]), IP_TAB[DRONEID][(cpt) % (EXT_NB - 1)]);
    u->readsets[cpt].fd = u->devtab[cpt].fd.id; u->readsets[cpt].events = POLLIN; u->readnb++;
  }

#if DRONEID == 0
  for (uint8_t cpt = 0; cpt < MAXDRONE ; cpt++) {
    init_sock(1,&u->devdrone[cpt][WFB_VID].fd, PORT_VID + cpt, (char *)0, IP_LOCAL);
  }
#else
  cpt = u->readnb;
  init_sock(0,&u->devtab[WFB_VID].fd, PORT_VID, IP_LOCAL, (char *)0);
  u->readsets[cpt].fd = u->devtab[WFB_VID].fd.id; u->readsets[cpt].events = POLLIN; u->readnb++;
#endif

}

/*****************************************************************************/
void print_log(wfb_utils_log_t *plog) {

  if (plog->len == 0) plog->len += sprintf((char *)plog->buf + plog->len, "TIC\n");

  sendto(plog->fd.id, plog->buf, plog->len, 0,  (const struct sockaddr *)&plog->fd.outaddr, sizeof(struct sockaddr));
  plog->len = 0;
}

/*****************************************************************************/
void send_msg(wfb_utils_fd_t fd, wfb_utils_log_t *plog) {

  ssize_t len;
  uint32_t sequence = 0;
  uint8_t payloadbuf[1500];

  wfb_utils_heads_pay_t headspay = { .droneid = DRONEID, .len = sizeof(payloadbuf), .seq = sequence};

  struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };

  struct iovec iovpay = { .iov_base = &payloadbuf, iovpay.iov_len = sizeof(payloadbuf) };

  struct iovec iovtab[2] = { iovheadpay, iovpay }; uint8_t iovtablen = 2;

  struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = iovtablen, .msg_name = &fd.outaddr, .msg_namelen = sizeof(fd.outaddr) };

  len = sendmsg(fd.id, (const struct msghdr *)&msg, MSG_DONTWAIT);

  plog->len += sprintf((char *)plog->buf + plog->len, "sendmsg (%ld)(%d)(%s)\n",len,fd.id,fd.ipstr);
}

/*****************************************************************************/
void recv_msg(wfb_utils_fd_t fd, wfb_utils_log_t *plog) {

  ssize_t len;
  uint8_t payloadbuf[1500];

  wfb_utils_heads_pay_t headspay;
  memset(&headspay,0,sizeof(wfb_utils_heads_pay_t));
  memset(&payloadbuf,0,sizeof(payloadbuf));

  struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };
  struct iovec iovpay = { .iov_base = &payloadbuf, .iov_len = sizeof(payloadbuf) };

  struct iovec iovtab[2] = { iovheadpay, iovpay }; uint8_t tablen = 2;

  struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = tablen };
  len = recvmsg(fd.id, &msg, MSG_DONTWAIT) - sizeof(wfb_utils_heads_pay_t);

  if (len > 0) {
    if (headspay.len > 0) plog->len += sprintf((char *)plog->buf + plog->len, "(%ld) from DRONEID (%d) (%s)\n",len,headspay.droneid,fd.ipstr);
  }
}

/*****************************************************************************/
void wfb_utils_loop(wfb_utils_init_t *u) {

  uint64_t exptime;
  ssize_t len;

  for(;;) {
    if (0 != poll(u->readsets, u->readnb, -1)) {
      for (uint8_t cpt=0; cpt<u->readnb; cpt++) {
        if (u->readsets[cpt].revents == POLLIN) {
	  if ( cpt == 0 ) {
	    len = read(u->readsets[cpt].fd, &exptime, sizeof(uint64_t)); 
#if DRONEID == 0
	    send_msg(u->devtab[1].fd, &u->log);
	    send_msg(u->devtab[2].fd, &u->log);
#endif
	    print_log(&u->log);
	  } else {
            recv_msg(u->devtab[cpt].fd, &u->log);
	  }
	}
      }
    }
  }
}
