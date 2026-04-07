/*
gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c wfb_sendnoizeraw.c -o wfb_sendnoizeraw.o

cc wfb_netlink.o wfb_sendnoizeraw.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o exe_wfb_sendnoizeraw
*/

#include "wfb_netlink.h"

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

#include <errno.h>

/*****************************************************************************/
int main(int argc, char **argv) {

  if ((argc < 3)||(argc > 4)) exit(-1);

  wfb_netlink_init_t n;
  if (false == wfb_netlink_init(&n)) { printf("NO WIFI\n"); exit(-2); }
//  for (uint8_t i=0;i<n.nbraws;i++) printf("(%s)\n",n.rawdevs[i]->ifname);

  uint8_t sockfd;
  uint16_t protocol = 0;
  if (-1 == (sockfd = socket(AF_PACKET,SOCK_RAW,protocol))) exit(-1);

  struct ifreq ifr;

  memset(&ifr, 0, sizeof(struct ifreq));
  strncpy( ifr.ifr_name, argv[1], sizeof( ifr.ifr_name ) - 1 );
  if (ioctl( sockfd, SIOCGIFINDEX, &ifr ) < 0 ) exit(-1);

  struct sockaddr_ll sll;
  memset( &sll, 0, sizeof( sll ) );
  sll.sll_family   = AF_PACKET;
  sll.sll_ifindex  = ifr.ifr_ifindex;
  sll.sll_protocol = protocol;
  if (-1 == bind(sockfd, (struct sockaddr *)&sll, sizeof(sll))) exit(-1);

  struct nl_sock *sockrt;
  if (!(sockrt = nl_socket_alloc())) exit(-1);
  if (nl_connect(sockrt, NETLINK_ROUTE)) exit(-1);
  
  struct nl_cache *cache;
  struct rtnl_link *link, *change;

  if (rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache) < 0) exit(-1);
  if (!(link = rtnl_link_get(cache,ifr.ifr_ifindex))) exit(-1);
  if (!(change = rtnl_link_alloc ())) exit(-1);
  rtnl_link_unset_flags (change, IFF_UP);
  rtnl_link_change(sockrt, link, change, 0);
 
  uint8_t sockid;
  struct nl_sock *socknl;
  if  (!(socknl = nl_socket_alloc())) exit(-1);
  nl_socket_set_buffer_size(socknl, 8192, 8192);
  if (genl_connect(socknl)) exit(-1);
  if ((sockid = genl_ctrl_resolve(socknl, "nl80211")) < 0) exit(-1);

  struct nl_msg *nlmsg;
/*
  nlmsg = nlmsg_alloc();
  genlmsg_put(nlmsg,0,0,sockid,0,0,NL80211_CMD_SET_INTERFACE,0);
  nla_put_u32(nlmsg, NL80211_ATTR_IFINDEX, ifr.ifr_ifindex);
  nla_put_u32(nlmsg, NL80211_ATTR_IFTYPE,NL80211_IFTYPE_MONITOR);
  if (nl_send_auto(socknl, nlmsg) < 0) exit(-1);
  nlmsg_free(nlmsg);
*/
/*
  if (rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache) < 0) exit(-1);
  if (!(link = rtnl_link_get(cache,ifr.ifr_ifindex))) exit(-1);
  if (!(change = rtnl_link_alloc ())) exit(-1);
  rtnl_link_set_flags (change, IFF_UP);
  rtnl_link_change(sockrt, link, change, 0);
*/
  uint32_t freq = atoi(argv[2]);
  nlmsg = nlmsg_alloc();
  genlmsg_put(nlmsg,0,0,sockid,0,0,NL80211_CMD_SET_CHANNEL,0);
  NLA_PUT_U32(nlmsg,NL80211_ATTR_IFINDEX,ifr.ifr_ifindex);
  NLA_PUT_U32(nlmsg,NL80211_ATTR_WIPHY_FREQ,freq);
  if (nl_send_auto(socknl, nlmsg) < 0) exit(-1);
  nlmsg_free(nlmsg);
/*
  if (argc == 4) {
    uint32_t mcs = atoi(argv[3]);
    if (mcs > 31) exit(-1);
    else (*(12 + ((uint8_t *)(n.msg_out[0].msg_iov[0].iov_base)))) = (uint8_t)mcs;
   }
*/
  do {
//    ssize_t rawlen = sendmsg(sockfd, &n.msg.msgout[1], MSG_DONTWAIT);
    ssize_t rawlen = sendmsg(n.rawdevs[0]->sockfd, &n.msg.msgout[1], MSG_DONTWAIT);
    printf("(%ld)(%d)\n",rawlen,freq);
    sleep(1);
  } while (argc == 4);


  nla_put_failure:
    exit(-1);
}
