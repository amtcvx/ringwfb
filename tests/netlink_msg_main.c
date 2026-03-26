/*
sudo apt-get install libnl-3-dev libnl-genl-3-dev libnl-route-3-dev

gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -c netlink_msg_main.c -o netlink_msg_main.o

cc netlink_utils.o msg_utils.o netlink_msg_main.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o exe_netlinkmain

*/

#include <unistd.h>
#include <sys/socket.h>
#include <poll.h>
#include <sys/uio.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/timerfd.h>

#include "netlink_utils.h"
#include "msg_utils.h"

#define DRIVERNAME "rtl88XXau"

#define PERIOD_DELAY_S 1

/*****************************************************************************/
int main(int argc, char **argv) {

  netlink_utils_init_t n;

  if (false == netlink_utils_init(&n,DRIVERNAME)) { printf("NO WIFI\n"); exit(-2); }
  for (uint8_t i=0;i<n.nbraws;i++) printf("(%s)\n",n.rawdevs[i]->ifname);

  uint8_t nbfds = (1 + n.nbraws);
  uint8_t fd[nbfds];

  uint64_t exptime;
  if (-1 == (fd[0] = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(fd[0], 0, &period, NULL);

  for (uint8_t rawcpt=0; rawcpt < n.nbraws; rawcpt++) {
    fd[rawcpt + 1] = n.rawdevs[rawcpt]->sockfd;
    n.rawdevs[rawcpt]->freq = (n.nbraws - rawcpt - 1) * (n.rawdevs[rawcpt]->nbfreqs / n.nbraws);
    netlink_utils_setfreq(&n.sockidnl, n.rawdevs[rawcpt]->ifindex, n.rawdevs[rawcpt]->freqs[n.rawdevs[rawcpt]->freq]);
  }

  struct pollfd readsets[nbfds];
  for (uint8_t nbfdscpt=0; nbfdscpt < nbfds; nbfdscpt++) { readsets[nbfdscpt].fd = fd[nbfdscpt]; readsets[nbfdscpt].events = POLLIN; }

  ssize_t len = 0;
  uint32_t rawpkt[n.nbraws]; 

  msg_utils_init_t u;
  msg_utils_init(&u);

  for(;;) {
    if (0 != poll(readsets, nbfds, -1)) {
      for (uint8_t cpt=0; cpt<nbfds; cpt++) {
        if (readsets[cpt].revents == POLLIN) {
          if (cpt == 0) {
            len = read(fd[0], &exptime, sizeof(uint64_t));
//            len = sendmsg(brdfd, (const struct msghdr *)&msg, MSG_DONTWAIT);
            for (uint8_t i=0;i<n.nbraws;i++) {
//              len = sendmsg(fd[i+1], (const struct msghdr *)&msg, MSG_DONTWAIT);
              len = sendmsg(fd[i+1], (const struct msghdr *)&u.msg_out, MSG_DONTWAIT);
              printf("[%d](%ld)\n",rawpkt[i],len);
	      rawpkt[i] = 0;
	    }
          } else {
            if ((len = recvmsg(fd[cpt], &u.msg_in, MSG_DONTWAIT)) > 0)
            rawpkt[cpt-1]+=len;
          }
        }
      }
    }
  }
}
