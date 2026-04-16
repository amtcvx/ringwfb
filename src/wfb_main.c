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
//  if (n.nbraws < 2)  { printf("NOT ENOUGHT WIFI\n"); exit(-2); }

  wfb_log_init_t l;
  wfb_log_init(&l);
  l.len += sprintf(l.buf + l.len,"(%d)  ",n.nbraws);
  for (uint8_t i = 0;i < n.nbraws; i++) l.len += sprintf(l.buf + l.len,"(%s)  ",n.rawdevs[i]->ifname);
  l.len += sprintf(l.buf + l.len,"\n");

  wfb_sync_init_t s;
  wfb_sync_init(&s,&n);

  uint8_t nbfds = (1 + n.nbraws);
  uint8_t fd[nbfds];
  fd[0] = s.time.fd; for (uint8_t i = 0; i < n.nbraws; i++) fd[i + 1] = n.rawdevs[i]->sockfd;
  struct pollfd readsets[nbfds];
  memset(readsets, 0, sizeof(readsets));
  for (uint8_t nbfdscpt = 0; nbfdscpt < nbfds; nbfdscpt++) { readsets[nbfdscpt].fd = fd[nbfdscpt]; readsets[nbfdscpt].events = POLLIN; }

  ssize_t len = 0, rawlen = 0;



  bool send_first = false;
  int8_t sync_first = -1, sync_scan = -1;
  uint8_t sync_cpt[n.nbraws], sync_ack[n.nbraws];
  for (uint8_t i = 0; i < n.nbraws; i++) { sync_cpt[i] = 1; sync_ack[i] = 1; }

  for(;;) {
    if (0 != poll(readsets, nbfds, -1)) {
      for (uint8_t cpt = 0; cpt < nbfds; cpt++) {
        if (readsets[cpt].revents & POLLIN) {
          if (cpt == 0) {
            len = read(fd[0], &s.time.exptime, sizeof(uint64_t));

            printf("(%d)(%d) cpt(%d)(%d) ack(%d)(%d)  freq(%d)(%d)\n",sync_first, sync_scan, sync_cpt[0], sync_cpt[1], sync_ack[0], sync_ack[1],
              n.rawdevs[0]->freqs[n.rawdevs[0]->cptfreq], n.rawdevs[1]->freqs[n.rawdevs[1]->cptfreq]); fflush(stdout);

            for (uint8_t i = 0; i < n.nbraws; i++) {
              if (i != sync_first) {
                if (sync_cpt[i] == 5) {
                  if (sync_first < 0) {
                    sync_first = i; for (uint8_t j = 0; j < n.nbraws; j++) if (i != j)
                    { sync_scan = j;  n.rawdevs[j]->cptfreq = (n.nbraws - j -1) * (n.rawdevs[j]->nbfreqs / n.nbraws);
                      wfb_netlink_setfreq(&n.sockidnl, n.rawdevs[sync_scan]->ifindex, n.rawdevs[sync_scan]->freqs[n.rawdevs[sync_scan]->cptfreq]);
                    }
                  }
                }
                if (sync_cpt[i] == 0) {

                  if ((++(n.rawdevs[i]->cptfreq)) >= (n.rawdevs[i]->nbfreqs)) n.rawdevs[i]->cptfreq = 0;
                  for (uint8_t j = 0; j < n.nbraws; j++) {
                    if ((i != j) && ((n.rawdevs[i]->cptfreq) == (n.rawdevs[j]->cptfreq))) {
                      if ((++(n.rawdevs[i]->cptfreq)) >= (n.rawdevs[i]->nbfreqs)) n.rawdevs[i]->cptfreq = 0;
		    }
		  }
                  wfb_netlink_setfreq(&n.sockidnl, n.rawdevs[i]->ifindex, n.rawdevs[i]->freqs[n.rawdevs[i]->cptfreq]);

		}

                if (sync_cpt[i] < 5) sync_cpt[i]++;
              }
            }
            if (sync_scan >= 0) {
              send_first = true;

              if (sync_ack[sync_scan] < 3) sync_ack[sync_scan]++;
              if (sync_ack[sync_scan] == 3)  {

                if ((++(n.rawdevs[sync_scan]->cptfreq)) >= (n.rawdevs[sync_scan]->nbfreqs)) n.rawdevs[sync_scan]->cptfreq = 0;
                for (uint8_t j = 0; j < n.nbraws; j++) {
                  if ((sync_scan != j) && ((n.rawdevs[sync_scan]->cptfreq) == (n.rawdevs[j]->cptfreq))) {
                    if ((++(n.rawdevs[sync_scan]->cptfreq)) >= (n.rawdevs[sync_scan]->nbfreqs)) n.rawdevs[sync_scan]->cptfreq = 0;
		  }
		}
                wfb_netlink_setfreq(&n.sockidnl, n.rawdevs[sync_scan]->ifindex, n.rawdevs[sync_scan]->freqs[n.rawdevs[sync_scan]->cptfreq]);

                sync_ack[sync_scan] = 1;
              }
            }
          } else {
            memset((uint8_t *)(n.msg.msgin[cpt-1].msg_iov[1].iov_base), 0 , n.msg.msgin[cpt-1].msg_iov[1].iov_len);
            memset((uint8_t *)(n.msg.msgin[cpt-1].msg_iov[3].iov_base), 0 , n.msg.msgin[cpt-1].msg_iov[3].iov_len);
            if ((rawlen = recvmsg(fd[cpt], &n.msg.msgin[cpt-1], MSG_DONTWAIT)) > 0) {
              if (*(4 + ((uint8_t *)(n.msg.msgin[cpt-1].msg_iov[1].iov_base))) == 0x66) {
                printf("recvmsg (%d)(%ld)\n",cpt,rawlen); fflush(stdout);
                sync_ack[cpt - 1] = 0;
              } else {
                sync_cpt[cpt - 1] = 0;
              }
            }
          }
        }
      }
      if (send_first) {
        ((wfb_netlink_payhd_t *)(n.msg.msgout[send_first].msg_iov[3].iov_base))->droneid = DRONEID;
        ((wfb_netlink_payhd_t *)(n.msg.msgout[send_first].msg_iov[3].iov_base))->msglen = 1;
        n.msg.msgout[send_first].msg_iov[4].iov_len = 1;

        rawlen = sendmsg(fd[1 + sync_first], &n.msg.msgout[send_first], MSG_DONTWAIT);
        printf("sendmsg (%d)(%ld)\n",sync_first,rawlen); fflush(stdout);
        send_first = false;
      }
    } // poll
  }

/*
  for(;;) {
    if (0 != poll(readsets, nbfds, -1)) {
      for (uint8_t cpt = 0; cpt < nbfds; cpt++) {
        if (readsets[cpt].revents & POLLIN) {
          if (cpt == 0) {
            len = read(fd[0], &s.time.exptime, sizeof(uint64_t));
	    wfb_sync_periodic(&s,&n,&l);
          } else {
            memset((uint8_t *)(n.msg.msgin[cpt-1].msg_iov[1].iov_base), 0 , n.msg.msgin[cpt-1].msg_iov[1].iov_len);
            memset((uint8_t *)(n.msg.msgin[cpt-1].msg_iov[3].iov_base), 0 , n.msg.msgin[cpt-1].msg_iov[3].iov_len);
            if ((len = recvmsg(fd[cpt], &n.msg.msgin[cpt-1], MSG_DONTWAIT)) > 0) wfb_sync_async(cpt-1, &s, &n, &l);
          }
        }
      }

      for (uint8_t cpt = 1; cpt < nbfds; cpt++) {
        if (s.com[cpt-1].len > 0) {

          ((wfb_netlink_payhd_t *)(n.msg.msgout[cpt-1].msg_iov[3].iov_base))->droneid = 3;//DRONEID;
										  
          len = sendmsg(fd[cpt], &n.msg.msgout[cpt-1], MSG_DONTWAIT);

          l.len += sprintf(l.buf + l.len,"SEND (%d)(%ld)(%d) (%d)\n",cpt-1,len,s.com[cpt-1].len,
		  ((wfb_netlink_payhd_t *)(n.msg.msgout[cpt-1].msg_iov[3].iov_base))->backfreq);

          //  len = sendmsg(n.bonds[0].sockfd, n.msg.msg_out, MSG_DONTWAIT);
	  
          //memset((uint8_t *)(n.msg.msgout[cpt-1].msg_iov[1].iov_base), 0 , n.msg.msgout[cpt-1].msg_iov[1].iov_len);
          //memset((uint8_t *)(n.msg.msgout[cpt-1].msg_iov[3].iov_base), 0 , n.msg.msgout[cpt-1].msg_iov[3].iov_len);

	  s.com[cpt-1].len = 0;
	}
      }
    } // poll
  }
*/
}
