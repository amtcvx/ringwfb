/*
sudo apt-get install libnl-3-dev libnl-genl-3-dev libnl-route-3-dev

gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -c wfb_main.c -o wfb_main.o

cc wfb_netlink.o wfb_sync.o wfb_log.o wfb_main.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o exe_main

*/

#include <sys/utsname.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <poll.h>
#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>

#include "wfb_main.h"
#include "wfb_netlink.h"
#include "wfb_sync.h"
#include "wfb_log.h"

#define RELEASE "6.8.0-060800-generic"

/*****************************************************************************/
int main(int argc, char **argv) {

  struct utsname utsnamebuf;
  if (uname(&utsnamebuf) != 0) exit(-1);
  if (strcmp(utsnamebuf.release, RELEASE)!=0) exit(-1);

  wfb_netlink_init_t n;
  if (false == wfb_netlink_init(&n)) { printf("NO WIFI\n"); exit(-2); }
  for (uint8_t i=0;i<n.nbraws;i++) printf("(%s)\n",n.rawdevs[i]->ifname);
  if (n.nbraws < 2)  { printf("NOT ENOUGHT WIFI\n"); exit(-2); }

  wfb_log_init_t l;
  wfb_log_init(&l);

  wfb_sync_init_t s;
  wfb_sync_init(&s,&n);

  uint8_t nbfds = (1 + n.nbraws);
  uint8_t fd[nbfds];
  fd[0] = s.fd; for (uint8_t i=0; i<n.nbraws; i++) fd[i + 1] = n.rawdevs[i]->sockfd;
  struct pollfd readsets[nbfds];
  for (uint8_t nbfdscpt=0; nbfdscpt < nbfds; nbfdscpt++) { readsets[nbfdscpt].fd = fd[nbfdscpt]; readsets[nbfdscpt].events = POLLIN; }

  ssize_t len = 0;

  for(;;) {
    if (0 != poll(readsets, nbfds, -1)) {
      for (uint8_t cpt=0; cpt<nbfds; cpt++) {
        if (readsets[cpt].revents == POLLIN) {
          if (cpt == 0) {
            len = read(s.fd, &s.exptime, sizeof(uint64_t));
	    wfb_sync_periodic(&s,&n,&l);
          } else {
            ((wfb_netlink_payhd_t *)(n.msg.msgin[cpt-1].msg_iov[3].iov_base))->droneid = 0;
            if ((len = recvmsg(fd[cpt], &n.msg.msgin[cpt-1], MSG_DONTWAIT)) > 0) wfb_sync_async(cpt-1, &s, &n, &l);
          }
        }
      }

      for (uint8_t cpt=1; cpt<nbfds; cpt++) {
        if (s.len[cpt-1] > 0) {
          ((wfb_netlink_payhd_t *)(n.msg.msgout[cpt-1].msg_iov[3].iov_base))->droneid = DRONEID;
          len = sendmsg(fd[cpt], &n.msg.msgout[cpt-1], MSG_DONTWAIT);

          l.len += sprintf(l.buf + l.len,"SEND (%d)(%ld)(%d) (%d)\n",cpt-1,len,s.len[cpt-1],
		  ((wfb_netlink_payhd_t *)(n.msg.msgout[cpt-1].msg_iov[3].iov_base))->backfreq);

          //  len = sendmsg(n.bonds[0].sockfd, n.msg.msg_out, MSG_DONTWAIT);
	  
	  s.len[cpt-1] = 0;
	}
      }
    }
  }
}
