/*
sudo apt-get install libnl-3-dev libnl-genl-3-dev libnl-route-3-dev

gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c bridgeraw.c -o bridgeraw.o

cc bridgeraw.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o exe_bridgeraw

sudo rfkill unblock ...

sudo ./exe_bridgeraw 

sudo ip link add name br0 type bridge

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
#include <netlink/route/link/bridge.h>

#include <net/if.h>

#include <sys/timerfd.h>

#include <dirent.h>

#include <errno.h>

#define DRIVER_NAME "rtw88_8812au"
//#define DRIVER_NAME "rtw_8812au"

#define TEST_BRIDGE_NAME "br0"

char *rawnames[] = { "wlx3c7c3fa9c1e4", "wlx3c7c3fa9bdca" };
uint32_t rawfreqs[] = { 2484, 2432 };

/*****************************************************************************/
#define PERIOD_DELAY_S  1

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
void preset(uint8_t sockid, struct nl_sock *sockrt, struct nl_sock *socknl, char *name,  uint16_t *index) {

  struct rtnl_link *ltap;
  struct nl_cache *cache;
  if ((rtnl_link_alloc_cache(sockrt, sockid, &cache)) < 0) exit(-1);
  if (!(ltap = rtnl_link_get_by_name(cache, name))) exit(-1);
  *index = rtnl_link_get_ifindex(ltap);

  if (IFF_UP & rtnl_link_get_flags(ltap)) {
    struct rtnl_link *change;
    if (!(change = rtnl_link_alloc())) exit(-1);
    rtnl_link_unset_flags(change, IFF_UP);
    if ((rtnl_link_change(sockrt, ltap, change, 0)) < 0) exit(-1);
  }

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
void set(struct nl_sock *sockrt, struct rtnl_link **link) {

  struct rtnl_link *old;
  struct nl_cache *cache;
  if ((rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache)) < 0) exit(-1);
  if (!(old = rtnl_link_alloc ())) exit(-1);
  if ((old = rtnl_link_get_by_name(cache, TEST_BRIDGE_NAME))) {
    if (rtnl_link_delete(sockrt, old) < 0) exit(-1);
  }

  if ((rtnl_link_bridge_add(sockrt, TEST_BRIDGE_NAME)) < 0) exit(-1);
  if ((rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache)) < 0) exit(-1);
  if (!(*link = rtnl_link_alloc ())) exit(-1);
  if (!(*link = rtnl_link_get_by_name(cache, TEST_BRIDGE_NAME))) exit(-1);
}

/*****************************************************************************/
void postset(uint8_t sockid, struct nl_sock *sockrt, struct nl_sock *socknl,  uint16_t index,
  struct rtnl_link *link) {

  struct rtnl_link *ltap;
  struct nl_cache *cache;
  if ((rtnl_link_alloc_cache(sockrt, sockid, &cache)) < 0) exit(-1);
  if (!(ltap = rtnl_link_get(cache, index))) exit(-1);

  if ((rtnl_link_enslave(sockrt, link, ltap)) < 0) exit(-1);

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

/******************************************************************************/
void drain(uint8_t fd) {

  struct sock_filter zero_bytecode = BPF_STMT(BPF_RET | BPF_K, 0);
  struct sock_fprog zero_program = { 1, &zero_bytecode};
  setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &zero_program, sizeof(zero_program));
  char drain[1];
  while (recv(fd, drain, sizeof(drain), MSG_DONTWAIT) >= 0) printf("----\n");
  struct sock_filter full_bytecode = BPF_STMT(BPF_RET | BPF_K, (u_int)-1);
  struct sock_fprog full_program = { 1, &full_bytecode};
  setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &full_program, sizeof(full_program));
}

/*****************************************************************************/
void sockset(uint16_t index, uint8_t *fd) {

  uint16_t protocol = htons(ETH_P_ALL);
  if (-1 == (*fd = socket(AF_PACKET,SOCK_RAW,protocol))) exit(-1);

  struct sockaddr_ll sll;
  memset( &sll, 0, sizeof( sll ) );
  sll.sll_family   = AF_PACKET;
  sll.sll_ifindex  = index;
  sll.sll_protocol = protocol;
  if (-1 == bind(*fd, (struct sockaddr *)&sll, sizeof(sll))) exit(-1); // BIND must be AFTER wifi setting

  drain(*fd);

  const int32_t sock_qdisc_bypass = 1;
  if (-1 == setsockopt(*fd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass))) exit(-1);
}

/*****************************************************************************/
//sudo sh -c "echo -n '1-1:1.0' > /sys/bus/usb/drivers/rtw_8812au/bind"
bool rebind(char *ifname) {
  bool ret = false;

  char *ptr,*netpath = "/sys/class/net";
  char *driverpath = "/sys/bus/usb/drivers/";
  char path[1024],buf[1024];
  ssize_t lenlink;
  FILE *fd;

  char dirpath[1024];
  strcpy(dirpath,driverpath);
  strcat(dirpath,DRIVER_NAME);
  if (!(opendir(dirpath))) exit(-1);

  sprintf(path,"%s/%s/device",netpath,ifname);
  if ((lenlink = readlink(path, buf, sizeof(buf)-1)) != -1) {
    buf[lenlink] = '\0';
    ptr = strrchr( buf, '/' );
    ptr++;
    strcpy(path,dirpath);
    strcat(path,"/unbind");
    fd = fopen(path,"a");
    fputs(ptr,fd);fflush(fd);
    strcpy(path,dirpath);
    strcat(path,"/bind");
    fd = fopen(path,"a");
    fputs(ptr,fd);fflush(fd);
    ret = true;
  }
  return(ret);
}

/*****************************************************************************/
int main(int argc, char **argv) {

  uint8_t nbraws = sizeof(rawnames)/sizeof(rawnames[0]);
  uint8_t nbfds = 1 + nbraws;
  uint8_t fd[nbfds];
  struct rawdev_t { uint16_t index; char *name; } rawdev[nbraws];
  for (uint8_t i=0;i<nbraws;i++) rawdev[i].name = &rawnames[i][0];
  for (uint8_t i=0;i<nbraws;i++) if (rebind(rawdev[i].name) == false) exit(-1);
  sleep(1);

  uint64_t exptime;
  if (-1 == (fd[0] = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(fd[0], 0, &period, NULL);

  uint8_t sockid; struct nl_sock *sockrt; struct nl_sock *socknl;
  init(&sockid, &sockrt, &socknl);

  for(uint8_t i=0; i<nbraws; i++) preset(sockid, sockrt, socknl, rawdev[i].name, &rawdev[i].index );

  struct rtnl_link *link;
  set(sockrt, &link);

  for (uint8_t i=0; i<nbraws; i++) postset(sockid, sockrt, socknl, rawdev[i].index, link);

  for (uint8_t i=0; i<nbraws; i++) sockset(rawdev[i].index, &fd[i + 1]);

  for (uint8_t i=0; i<nbraws; i++) setfreq(sockid, socknl, rawdev[i].index, rawfreqs[i]);

  struct pollfd readsets[nbfds];
  for (uint8_t i=0; i<nbfds; i++) { readsets[i].fd = fd[i]; readsets[i].events = POLLIN; }

  ssize_t len = 0, len1 = 0, len2 = 0;
  uint32_t rawpkt[2] = { 0, 0 };

  uint8_t dumbuf[1400] = {-1};
  struct iovec iovdum = { .iov_base = dumbuf, .iov_len = sizeof(dumbuf) };
  struct iovec iovtab[4] = { iov_radiotaphd_tx, iov_ieeehd_tx, iov_llchd_tx, iovdum };
  struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 4 };

  for(;;) {
    if (0 != poll(readsets, nbfds, -1)) {
      for (uint8_t cpt=0; cpt<nbfds; cpt++) {
        if (readsets[cpt].revents == POLLIN) {
          if (cpt == 0) {
            len = read(fd[0], &exptime, sizeof(uint64_t));
            len1 = sendmsg(fd[1], (const struct msghdr *)&msg, MSG_DONTWAIT);
            len2 = sendmsg(fd[2], (const struct msghdr *)&msg, MSG_DONTWAIT);
	    printf("[%d][%d]  (%ld)(%ld)\n",rawpkt[0],rawpkt[1],len1,len2);
	    rawpkt[0] = 0; rawpkt[1] = 0;
	  } else {
            if ((len = recvmsg(fd[cpt], &msg, MSG_DONTWAIT)) > 0)
	    rawpkt[cpt-1]++;
	  }
	}
      }
    }
  }
}
