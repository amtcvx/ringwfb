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
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h> 


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
#define TEST_INTERFACE_NAME "wlx3c7c3fa9bdca"

/************************************************************************************************/
#define IEEE80211_RADIOTAP_MCS_HAVE_BW    0x01
#define IEEE80211_RADIOTAP_MCS_HAVE_MCS   0x02
#define IEEE80211_RADIOTAP_MCS_HAVE_GI    0x04

#define IEEE80211_RADIOTAP_MCS_HAVE_STBC  0x20

#define IEEE80211_RADIOTAP_MCS_BW_20    0
#define IEEE80211_RADIOTAP_MCS_SGI      0x04

#define IEEE80211_RADIOTAP_MCS_STBC_1  1
#define IEEE80211_RADIOTAP_MCS_STBC_SHIFT 5

#define MCS_KNOWN (IEEE80211_RADIOTAP_MCS_HAVE_MCS | IEEE80211_RADIOTAP_MCS_HAVE_BW | IEEE80211_RADIOTAP_MCS_HAVE_GI | IEEE80211_RADIOTAP_MCS_HAVE_STBC )

#define MCS_FLAGS  (IEEE80211_RADIOTAP_MCS_BW_20 | IEEE80211_RADIOTAP_MCS_SGI | (IEEE80211_RADIOTAP_MCS_STBC_1 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT))

#define MCS_INDEX  2

/************************************************************************************************/

uint8_t radiotaphd_tx[] = {
        0x00, 0x00, // <-- radiotap version
        0x0d, 0x00, // <- radiotap header length
        0x00, 0x80, 0x08, 0x00, // <-- radiotap present flags:  RADIOTAP_TX_FLAGS + RADIOTAP_MCS
        0x08, 0x00,  // RADIOTAP_F_TX_NOACK
        MCS_KNOWN , MCS_FLAGS, MCS_INDEX // bitmap, flags, mcs_index
};
uint8_t ieeehd_tx[] = {
        0x08, 0x01,                         // Frame Control : Data frame from STA to DS
        0x00, 0x00,                         // Duration
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Receiver MAC
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Transmitter MAC
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Destination MAC
        0x10, 0x86                          // Sequence control
};
uint8_t llchd_tx[4];

struct iovec iov_radiotaphd_tx = { .iov_base = radiotaphd_tx, .iov_len = sizeof(radiotaphd_tx)};
struct iovec iov_ieeehd_tx =     { .iov_base = ieeehd_tx,     .iov_len = sizeof(ieeehd_tx)};
struct iovec iov_llchd_tx =      { .iov_base = llchd_tx,      .iov_len = sizeof(llchd_tx)};

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
void preset(uint8_t sockid, struct nl_sock *sockrt, struct nl_sock *socknl, 
  struct rtnl_link **ltap, uint16_t *index) {

  struct nl_cache *cache;
  if ((rtnl_link_alloc_cache(sockrt, sockid, &cache)) < 0) exit(-1);
  if (!(*ltap = rtnl_link_get_by_name(cache, TEST_INTERFACE_NAME))) exit(-1);
  struct rtnl_link *change;
  if (!(change = rtnl_link_alloc())) exit(-1);
  rtnl_link_unset_flags(change, IFF_UP);
  if ((rtnl_link_change(sockrt, *ltap, change, 0)) < 0) exit(-1);

  *index = rtnl_link_get_ifindex(*ltap);
  struct nl_msg *nlmsg;

  if (!(nlmsg  = nlmsg_alloc())) exit(-1);
  genlmsg_put(nlmsg,0,0,sockid,0,0,NL80211_CMD_SET_INTERFACE,0);  //  DOWN interfaces
  nla_put_u32(nlmsg, NL80211_ATTR_IFINDEX, *index);
  nla_put_u8(nlmsg, NL80211_ATTR_4ADDR,1);
  nl_send_auto(socknl, nlmsg);
  if (nl_send_auto(socknl, nlmsg) >= 0)  nl_recvmsgs_default(socknl);
  nlmsg_free(nlmsg);
}

/*****************************************************************************/
void set(struct nl_sock *sockrt, struct rtnl_link *ltap, struct rtnl_link **link) {
  struct nl_cache *cache;

  if ((rtnl_link_bridge_add(sockrt, TEST_BRIDGE_NAME)) < 0) exit(-1);
  if ((rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache)) < 0) exit(-1);
  if (!(*link = rtnl_link_alloc ())) exit(-1);
  if (!(*link = rtnl_link_get_by_name(cache, TEST_BRIDGE_NAME))) exit(-1);
  if ((rtnl_link_enslave(sockrt, *link, ltap)) < 0) exit(-1);
}

/*****************************************************************************/
void postset(uint8_t sockid, uint16_t index, struct nl_sock *sockrt, struct nl_sock *socknl,
  struct rtnl_link *ltap)  {

  struct nl_msg *nlmsg;
  if (!(nlmsg  = nlmsg_alloc())) exit(-1);
  genlmsg_put(nlmsg,0,0,sockid,0,0,NL80211_CMD_SET_INTERFACE,0);  //  DOWN interfaces
  nla_put_u32(nlmsg, NL80211_ATTR_IFINDEX, index);
  nla_put_u32(nlmsg, NL80211_ATTR_IFTYPE,NL80211_IFTYPE_MONITOR);
  nl_send_auto(socknl, nlmsg);
  if (nl_send_auto(socknl, nlmsg) >= 0)  nl_recvmsgs_default(socknl);
  nlmsg_free(nlmsg);

  struct rtnl_link *change;
  if (!(change = rtnl_link_alloc())) exit(-1);
  rtnl_link_set_flags(change, IFF_UP);
  if ((rtnl_link_change(sockrt, ltap, change, 0)) < 0) exit(-1);
}

/*****************************************************************************/
int main(int argc, char *argv[]) {

  uint8_t sockid; struct nl_sock *sockrt; struct nl_sock *socknl;
  init(&sockid, &sockrt, &socknl);

  struct rtnl_link *ltap; uint16_t index;
  preset(sockid, sockrt, socknl, &ltap, &index);

  struct rtnl_link *link;
  set(sockrt, ltap, &link);

  postset(sockid, index, sockrt, socknl, ltap);

  struct rtnl_link *change;
  if (!(change = rtnl_link_alloc())) exit(-1);
  rtnl_link_set_flags(change, IFF_UP);
  if ((rtnl_link_change(sockrt, link, change, 0)) < 0) exit(-1);

  uint8_t sockfd;
  uint16_t protocol = 0;
  if (-1 == (sockfd = socket(AF_PACKET,SOCK_RAW,protocol))) exit(-1);
  struct sockaddr_ll sll;
  memset( &sll, 0, sizeof( sll ) );
  sll.sll_family   = AF_PACKET;
  sll.sll_ifindex  = rtnl_link_get_ifindex(link);
  sll.sll_protocol = protocol;
  if (-1 == bind(sockfd, (struct sockaddr *)&sll, sizeof(sll))) exit(-1);

   
  uint8_t dumbuf[1400] = {-1};
  struct iovec iovdum = { .iov_base = dumbuf, .iov_len = sizeof(dumbuf) };
  struct iovec iovtab[4] = { iov_radiotaphd_tx, iov_ieeehd_tx, iov_llchd_tx, iovdum };
  struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 4 };

  ssize_t rawlen = sendmsg(sockfd, (const struct msghdr *)&msg, MSG_DONTWAIT);
  printf("(%ld)\n",rawlen);

  printf("SALUT\n");
  return 0;
}
