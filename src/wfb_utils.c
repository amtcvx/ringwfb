#include "wfb_utils.h"

#include <sys/timerfd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define PORT 5000

#define PERIOD_DELAY_S  1

#define IP_LOCAL "127.0.0.1"

typedef struct {
  uint8_t droneid;
  uint32_t seq;
  uint16_t len;
  uint8_t dummy;
} __attribute__((packed)) wfb_utils_heads_pay_t;

const char IP_TAB[MAXDRONE+1][MAXDEV][15] = { 
  { "192.168.1.100", "192.168.2.100" }, 
  { "192.168.1.1", "192.168.4.1" }, 
  { "192.168.4.2", "192.168.3.1" }, 
  { "192.168.3.2", "192.168.2.1" } };

/*****************************************************************************/
void wfb_utils_init(wfb_utils_init_t *u) {

  u->log.len =0; memset(u->log.buf, 0, sizeof(u->log.buf));
  if (-1 == (u->log.fd.id = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  if (-1 == setsockopt(u->log.fd.id, SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  u->log.fd.outaddr.sin_family = AF_INET;
  u->log.fd.outaddr.sin_port = htons(PORT);
  u->log.fd.outaddr.sin_addr.s_addr = inet_addr(IP_LOCAL);

  if (-1 == (u->fd[0].id = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(u->fd[0].id, 0, &period, NULL);
  u->readnb = 0;
  u->readtab[u->readnb] = 0;
  u->readsets[u->readnb].fd = u->fd[0].id; u->readsets[u->readnb].events = POLLIN; u->readnb++;

  for (uint8_t cpt=1; cpt<(MAXDEV+1); cpt++) {
    if (-1 == (u->fd[cpt].id = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
    if (-1 == setsockopt(u->fd[cpt].id, SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
    struct sockaddr_in  inaddr;
    inaddr.sin_family = AF_INET;
    inaddr.sin_port = htons(PORT);
    inaddr.sin_addr.s_addr = inet_addr(IP_TAB[DRONEID][(cpt % MAXDEV)]);
    if (-1 == bind( u->fd[cpt].id, (const struct sockaddr *)&inaddr, sizeof(inaddr))) exit(-1);
    u->fd[cpt].outaddr.sin_family = AF_INET;
    u->fd[cpt].outaddr.sin_port = htons(PORT);
    u->fd[cpt].outaddr.sin_addr.s_addr = inet_addr(IP_TAB[DRONEID][(cpt + 1) % MAXDEV]);
    memcpy(u->fd[cpt].ipstr, IP_TAB[DRONEID][(cpt + 1) % MAXDEV], 15);
    u->readtab[u->readnb] = cpt ;
    u->readsets[u->readnb].fd = u->fd[cpt].id; u->readsets[u->readnb].events = POLLIN; u->readnb++;
  }
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
          uint8_t cptid =  u->readtab[cpt];
	  if ( cptid == 0 ) {
	    len = read(u->readsets[cpt].fd, &exptime, sizeof(uint64_t)); 
#if DRONEID == 0
	    send_msg(u->fd[1], &u->log);
	    send_msg(u->fd[2], &u->log);
#endif
	    print_log(&u->log);
	  } else {
            recv_msg(u->fd[cpt], &u->log);
	  }
	}
      }
    }
  }
}
