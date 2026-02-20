#include "wfb_utils.h"

#include <sys/timerfd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define PORT_LOG 5000
#define PORT_EXT 5010

#define PORT_VID 5600

#define PERIOD_DELAY_S  1

#define IP_LOCAL "127.0.0.1"

const char IP_TAB[MAXDRONE+1][EXT_NB][15] = { 
  { "192.168.1.100", "192.168.2.100" }, 
  { "192.168.1.1", "192.168.4.1" }, 
  { "192.168.2.1", "192.168.3.2" }, 
  { "192.168.3.1", "192.168.4.2" } };

/*****************************************************************************/
void print_log(wfb_utils_log_t *plog) {

  if (plog->len == 0) plog->len += sprintf((char *)plog->buf + plog->len, "TIC\n");

  sendto(plog->fd.id, plog->buf, plog->len, 0,  (const struct sockaddr *)&plog->fd.outaddr, sizeof(struct sockaddr));
  plog->len = 0;
}

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
    memcpy(&pfd->ipstr, pout, strlen(pout));
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

  for (uint8_t cpt = 0; cpt < (EXT_NB - 1); cpt++) {
#if DRONEID == 0      
    const char *pin = (IP_TAB[0][cpt]); const char *pout = IP_TAB[cpt + 1][0];
    // cpt=0: 192.168.1.100 192.168.1.1  cpt=1:  192.168.2.100 192.168.2.1 //
#else
    const char *pin = (IP_TAB[DRONEID][cpt]); const char *pout = IP_TAB[cpt * MAXDRONE * (2 - DRONEID) ][cpt];
    // DRONEID=1: cpt=0: 192.168.1.1 192.168.1.100   cpt=1: 192.168.4.1 192.168.4.2 //
    // DRONEID=2: cpt=0: 192.168.2.1 192.168.2.100   cpt=1: 192.168.3.2 192.168.3.1 //
    // DRONEID=3: cpt=0: 192.168.3.1 192.168.3.2     cpt=1: 192.168.4.2 192.168.4.1 // 
#endif
    init_sock(2, &u->devtab[cpt].fd, PORT_EXT, pin, pout);
    u->readsets[cpt].fd = u->devtab[cpt].fd.id; u->readsets[cpt].events = POLLIN; u->readnb++;
  }

#if DRONEID == 0
  for (uint8_t cpt = 0; cpt < MAXDRONE ; cpt++) {
    init_sock(1,&u->devdrone[cpt][WFB_VID].fd, PORT_VID + cpt, (char *)0, IP_LOCAL);
  }
#else
  uint8_t cpt = u->readnb;
  init_sock(0,&u->devtab[EXT_NB + WFB_VID].fd, PORT_VID, IP_LOCAL, (char *)0);
  u->readsets[cpt].fd = u->devtab[EXT_NB + WFB_VID].fd.id; u->readsets[cpt].events = POLLIN; u->readnb++;
#endif

}

/*****************************************************************************/
void wfb_utils_periodic(wfb_utils_init_t *u) {

  print_log(&u->log);
}
