/*

https://serverfault.com/questions/152363/bridging-wlan0-to-eth0

sudo apt-get install libnl-3-dev libnl-genl-3-dev libnl-route-3-dev

gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c bridgeraw.c -o bridgeraw.o

cc bridgeraw.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o exe_bridgeraw

sudo rfkill unblock ...

sudo ./exe_bridgeraw 

sudo ip address show br0
sudo ip link show master br0

sudo ip link del name br0

*/
#include<unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/uio.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>
#include <linux/filter.h>

#include <stdbool.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>

#include <netlink/route/link.h>
#include <net/if.h>

#include <sys/timerfd.h>

#include <errno.h>

char *rawnames[] = { "wlx3c7c3fa9c1e4" };
uint32_t rawfreqs[] = { 2412 };

/*****************************************************************************/
#define PERIOD_DELAY_S  1

/*****************************************************************************/
void init(uint8_t *sockid, struct nl_sock **sockrt, struct nl_sock **socknl) {
  if  (!(*socknl = nl_socket_alloc()))  exit(-1);
  nl_socket_set_buffer_size(*socknl, 8192, 8192);
  if (genl_connect(*socknl)) exit(-1);
  if ((*sockid = genl_ctrl_resolve(*socknl, "nl80211")) < 0) exit(-1);
  if (!(*sockrt = nl_socket_alloc())) exit(-1);
  if (nl_connect(*sockrt, NETLINK_ROUTE)) exit(-1);
}

/*****************************************************************************/
bool setfreq(uint8_t sockid, struct nl_sock *socknl, int ifindex, uint32_t freq) {

  bool ret=true;
  struct nl_msg *nlmsg;
  if (!(nlmsg  = nlmsg_alloc())) exit(-1);;
  genlmsg_put(nlmsg,0,0,sockid,0,0,NL80211_CMD_SET_CHANNEL,0);
  NLA_PUT_U32(nlmsg,NL80211_ATTR_IFINDEX,ifindex);
  NLA_PUT_U32(nlmsg,NL80211_ATTR_WIPHY_FREQ,freq);
  if (nl_send_auto(socknl, nlmsg) < 0) ret=false;
  nlmsg_free(nlmsg);
  return(ret);
  nla_put_failure:
    nlmsg_free(nlmsg);
    return(false);
}

/*****************************************************************************/
int main(int argc, char **argv) {

  uint8_t nbraws = sizeof(rawnames)/sizeof(rawnames[0]);
  uint8_t nbfds = 1 + nbraws;
  uint8_t fd[nbfds];
  struct rawdev_t { uint16_t index; char *name; } rawdev[nbraws];
  for (uint8_t i=0;i<nbraws;i++) rawdev[i].name = &rawnames[i][0];

  uint64_t exptime;
  if (-1 == (fd[0] = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(fd[0], 0, &period, NULL);

  uint8_t sockid; struct nl_sock *sockrt; struct nl_sock *socknl;
  init(&sockid, &sockrt, &socknl);

  struct rtnl_link *ltap;
  struct nl_cache *cache;
  if ((rtnl_link_alloc_cache(sockrt, sockid, &cache)) < 0) exit(-1);
  if (!(ltap = rtnl_link_get_by_name(cache, rawdev[0].name))) exit(-1);
  struct rtnl_link *change;
  if (!(change = rtnl_link_alloc())) exit(-1);
  rtnl_link_unset_flags(change, IFF_UP);
  if ((rtnl_link_change(sockrt, ltap, change, 0)) < 0) exit(-1);

  uint16_t index = rtnl_link_get_ifindex(ltap);

  struct nl_msg *nlmsg;
  if (!(nlmsg  = nlmsg_alloc())) exit(-1);
  genlmsg_put(nlmsg,0,0,sockid,0,0,NL80211_CMD_SET_INTERFACE,0);  //  DOWN interfaces
  nla_put_u32(nlmsg, NL80211_ATTR_IFINDEX, index);
  nla_put_u32(nlmsg, NL80211_ATTR_IFTYPE,NL80211_IFTYPE_MONITOR);
  nl_send_auto(socknl, nlmsg);
  if (nl_send_auto(socknl, nlmsg) >= 0)  nl_recvmsgs_default(socknl);
  nlmsg_free(nlmsg);

  if (!(change = rtnl_link_alloc())) exit(-1);
  rtnl_link_set_flags(change, IFF_UP);
  if ((rtnl_link_change(sockrt, ltap, change, 0)) < 0) exit(-1);


  uint16_t protocol = htons(ETH_P_ALL);
  if (-1 == (fd[1] = socket(AF_PACKET,SOCK_RAW,protocol))) exit(-1);

  struct sockaddr_ll sll;
  memset( &sll, 0, sizeof( sll ) );
  sll.sll_family   = AF_PACKET;
  sll.sll_ifindex  = index;
  sll.sll_protocol = protocol;
  if (-1 == bind(fd[1], (struct sockaddr *)&sll, sizeof(sll))) exit(-1); // BIND must be AFTER wifi setting

  const int32_t sock_qdisc_bypass = 1;
  if (-1 == setsockopt(fd[1], SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass))) exit(-1);

  uint8_t dumbuf[1500];
  struct iovec iov_dum = { .iov_base = dumbuf, .iov_len = sizeof(dumbuf)};
  struct iovec iovtab[1] = { iov_dum };
  struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 1 };

  ssize_t rawlen = 0, len = 0;
  uint8_t rawnb = 0;

  setfreq(sockid, socknl, index, 2484);

  struct pollfd readsets[nbfds];
  for (uint8_t i=0; i<nbfds; i++) { readsets[i].fd = fd[i]; readsets[i].events = POLLIN; }

  for(;;) {
    if (0 != poll(readsets, 2, -1)) {
      for (uint8_t cpt=0; cpt<2; cpt++) {
        if (readsets[cpt].revents == POLLIN) {
          if (cpt == 0) {
            len = read(fd[0], &exptime, sizeof(uint64_t));
	    printf("[%d]\n",rawnb);
	    rawnb = 0;
	  } else {
            rawlen = recvmsg(fd[1], &msg, MSG_DONTWAIT);
	    rawnb++;
	  }
	}
      }
    }
  }
}
