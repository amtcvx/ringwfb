/*
sudo apt-get install libnl-3-dev libnl-genl-3-dev libnl-route-3-dev

gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c bridgeraw.c -o bridgeraw.o

cc bridgeraw.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o exe_bridgeraw

sudo rfkill unblock ...

export DEVICE=wlx3c7c3fa9c1e4
sudo ./exe_bridgeraw $DEVICE

sudo ip address show br0
sudo ip link show master br0

sudo ip link del name br0

*/

#include <linux/netlink.h>

#include <netlink/netlink.h>
#include <netlink/route/link.h>
#include <netlink/route/link/bridge.h>


#define TEST_BRIDGE_NAME "br0"
#define TEST_INTERFACE_NAME "wlx3c7c3fa9c1e4"

int main(int argc, char *argv[]) {

  struct nl_sock *sockrt;
  if (!(sockrt = nl_socket_alloc())) exit(-1);
  if (nl_connect(sockrt, NETLINK_ROUTE)) exit(-1);

  struct nl_cache *cache;
  if ((rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache)) < 0) exit(-1);

  struct rtnl_link *link,*ltap;
  link = rtnl_link_alloc();
  if ((rtnl_link_set_type(link, "bridge")) < 0) exit(-1);
  rtnl_link_set_name(link, TEST_BRIDGE_NAME);
  if ((rtnl_link_add(sockrt, link, NLM_F_CREATE)) < 0) exit(-1);

  nl_cache_refill(sockrt, cache);
  if (!(link = rtnl_link_get_by_name(cache, TEST_BRIDGE_NAME))) exit(-1);
  if (!(ltap = rtnl_link_get_by_name(cache, TEST_INTERFACE_NAME))) exit(-1);

  if ((rtnl_link_enslave(sockrt, link, ltap)) < 0) exit(-1);
  if ((rtnl_link_get_master(ltap) <= 0)) exit(-1);

  printf("HELLO\n");

  return 0;
}
