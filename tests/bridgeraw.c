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

#include <linux/netlink.h>

#include <netlink/netlink.h>

#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h> 
#include <linux/nl80211.h>

#include <netlink/route/link.h>
#include <netlink/route/link/bridge.h>

#include <net/if.h>

#define TEST_BRIDGE_NAME "br0"
#define TEST_INTERFACE_NAME "wlx3c7c3fa9bdc6"

/*****************************************************************************/
int main(int argc, char *argv[]) {

  struct nl_sock *socknl;
  if  (!(socknl = nl_socket_alloc()))  exit(-1);
  nl_socket_set_buffer_size(socknl, 8192, 8192);
  if (genl_connect(socknl)) exit(-1);
  uint8_t sockid;
  if ((sockid = genl_ctrl_resolve(socknl, "nl80211")) < 0) exit(-1);

  struct nl_sock *sockrt;
  if (!(sockrt = nl_socket_alloc())) exit(-1);
  if (nl_connect(sockrt, NETLINK_ROUTE)) exit(-1);

  struct nl_cache *cache;
  if ((rtnl_link_alloc_cache(sockrt, sockid, &cache)) < 0) exit(-1);

  struct rtnl_link *ltap;
  if (!(ltap = rtnl_link_get_by_name(cache, TEST_INTERFACE_NAME))) exit(-1);

  struct rtnl_link *change;
  if (!(change = rtnl_link_alloc())) exit(-1);
  rtnl_link_unset_flags(change, IFF_UP);
  if ((rtnl_link_change(sockrt, ltap, change, 0)) < 0) exit(-1);


  uint16_t index = rtnl_link_get_ifindex(ltap);
  struct nl_msg *nlmsg;
  if (!(nlmsg  = nlmsg_alloc())) exit(-1);
  genlmsg_put(nlmsg,0,0,sockid,0,0,NL80211_CMD_SET_INTERFACE,0);  //  DOWN interfaces
  nla_put_u32(nlmsg, NL80211_ATTR_IFINDEX, index);
  nla_put_u8(nlmsg, NL80211_ATTR_4ADDR,1);
//  nla_put_u32(nlmsg, NL80211_ATTR_IFTYPE,NL80211_IFTYPE_MONITOR);
  nl_send_auto(socknl, nlmsg);
  if (nl_send_auto(socknl, nlmsg) >= 0)  nl_recvmsgs_default(socknl);
  nlmsg_free(nlmsg);


  if ((rtnl_link_bridge_add(sockrt, TEST_BRIDGE_NAME)) < 0) exit(-1);
  if ((rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache)) < 0) exit(-1);
  struct rtnl_link *link;
  if (!(link = rtnl_link_alloc ())) exit(-1);
  if (!(link = rtnl_link_get_by_name(cache, TEST_BRIDGE_NAME))) exit(-1);
  if ((rtnl_link_enslave(sockrt, link, ltap)) < 0) exit(-1);
  
  if (!(change = rtnl_link_alloc())) exit(-1);
  rtnl_link_set_flags(change, IFF_UP);
  if ((rtnl_link_change(sockrt, ltap, change, 0)) < 0) exit(-1);

  printf("HELLO (%d)\n",index);

  return 0;
}
