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

const char IP_TAB[4][2][15] = { 
//  { "192.168.1.100", "192.168.2.100" }, 
//  { "192.168.1.1",   "192.168.4.1" }, 
  { "192.168.1.29", "192.168.2.100" }, 
  { "192.168.1.129",   "192.168.4.1" }, 
  { "192.168.2.1",   "192.168.3.2" }, 
  { "192.168.3.1",   "192.168.4.2" }
};

const uint8_t IP_ROUTE[4][2][2][2] = {
  {{{ 0,0 },{ 0,1 }},{{ 1,0 },{ 0,2 }}},  // 192.168.1.100 192.168.1.1   <->  192.168.2.100 192.168.2.1
  {{{ 0,1 },{ 0,0 }},{{ 1,1 },{ 1,3 }}},  // 192.168.1.1   192.168.1.100 <->  192.168.4.1   192.168.4.2 					
  {{{ 0,2 },{ 1,0 }},{{ 1,2 },{ 0,3 }}},  // 192.168.2.1   192.168.2.100 <->  192.168.3.2   192.168.3.1
  {{{ 0,3 },{ 1,2 }},{{ 1,3 },{ 1,1 }}}   // 192.168.3.1   192.168.3.2   <->  192.168.4.2   192.168.4.1 
};

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
    memcpy(&pfd->ipstr, pout,sizeof(pfd->ipstr));
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

  uint8_t droneid = 0, cpt=1;
#if DRONEID == 0
  for (droneid = 0; droneid <= MAXDRONE ; droneid++) {
#else   
  droneid = DRONEID;
#endif
    for (uint8_t i = 0; i < 2; i++) {
      init_sock(2, &u->devtab[cpt].fd, PORT_EXT, 
		    IP_TAB[IP_ROUTE[droneid][i][0][1]][IP_ROUTE[droneid][i][0][0]],
		    IP_TAB[IP_ROUTE[droneid][i][1][1]][IP_ROUTE[droneid][i][1][0]]);
    }
    u->readsets[cpt].fd = u->devtab[cpt].fd.id; u->readsets[cpt].events = POLLIN; u->readnb++;
#if DRONEID == 0
  }

  for (uint8_t cpt = 0; cpt < MAXDRONE ; cpt++) {
    init_sock(1,&u->devdrone[cpt][WFB_VID].fd, PORT_VID + cpt, (char *)0, IP_LOCAL);
  }
#else
  cpt = u->readnb;
  init_sock(0,&u->devtab[EXT_NB + WFB_VID].fd, PORT_VID, IP_LOCAL, (char *)0);
  u->readsets[cpt].fd = u->devtab[EXT_NB + WFB_VID].fd.id; u->readsets[cpt].events = POLLIN; u->readnb++;
#endif

}

/*****************************************************************************/
void wfb_utils_periodic(wfb_utils_init_t *u) {

  print_log(&u->log);
}
