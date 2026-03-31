/*
sudo apt-get install libnl-3-dev libnl-genl-3-dev libnl-route-3-dev

gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c wfb_utils_netlink.c -o wfb_utils_netlink.o

sudo ip address show wfbbond
sudo ip link show master wfbbond 

sudo ip link del name wfbbond

*/


#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>

#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>
#include <netlink/route/link.h>
#include <net/if.h>
#include <dirent.h>
#include <net/ethernet.h>
#include <linux/filter.h>
#include <linux/if_packet.h>

#include <netlink/route/link.h>
#include <netlink/route/link/bonding.h>

#include "wfb_utils_netlink.h"

#define DRIVER_NAME "rtl88XXau"
#define BOND_NAME "wfbbond"

/************************************************************************************************/
typedef struct {
  uint8_t nb;
  uint8_t curr;
  wfb_utils_netlink_raw_t *devs;
} elt_t;

/******************************************************************************/
int finish_callback(struct nl_msg *msg, void *arg) {

  bool* finished = arg;
  *finished = true;
  return NL_SKIP;
}

/******************************************************************************/
int getallinterfaces_callback(struct nl_msg *msg, void *arg) {

  struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
  struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
  nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

  wfb_utils_netlink_raw_t *ptr = &(((elt_t *)arg)->devs[((elt_t *)arg)->nb]);

  char ifname[30];
  if (tb_msg[NL80211_ATTR_IFNAME]) {
    strcpy(ifname, nla_get_string(tb_msg[NL80211_ATTR_IFNAME]));
    if (strlen(ifname) > 0) {
      strcpy(ptr->ifname, ifname);
//      if (tb_msg[NL80211_ATTR_IFINDEX]) ptr->ifindex = nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]);
//      if (tb_msg[NL80211_ATTR_IFTYPE])  ptr->iftype = nla_get_u32(tb_msg[NL80211_ATTR_IFTYPE]);
//      if (((elt_t *)arg)->nb < 2) ((((elt_t *)arg)->nb)++);
      ((((elt_t *)arg)->nb)++);
    }
  }

  return NL_SKIP;
}

/******************************************************************************/
int getsinglewifi_callback(struct nl_msg *msg, void *arg) {

  struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
  struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
  nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

  if (tb_msg[NL80211_ATTR_WIPHY_BANDS]) {
    struct nlattr *tb_band[NL80211_BAND_ATTR_MAX + 1];
    struct nlattr *tb_freq[NL80211_FREQUENCY_ATTR_MAX + 1];
    struct nlattr *nl_band;
    struct nlattr *nl_freq;
    int rem_band, rem_freq;
    int last_band = -1;

    wfb_utils_netlink_raw_t *ptr = &(((elt_t *)arg)->devs[((elt_t *)arg)->curr]);

    nla_for_each_nested(nl_band, tb_msg[NL80211_ATTR_WIPHY_BANDS], rem_band) {
      if (last_band != nl_band->nla_type) last_band = nl_band->nla_type;
      nla_parse(tb_band, NL80211_BAND_ATTR_MAX, nla_data(nl_band), nla_len(nl_band), NULL);
      if (tb_band[NL80211_BAND_ATTR_FREQS]) {
        nla_for_each_nested(nl_freq, tb_band[NL80211_BAND_ATTR_FREQS], rem_freq) {
          nla_parse(tb_freq, NL80211_FREQUENCY_ATTR_MAX, nla_data(nl_freq), nla_len(nl_freq), NULL);

          uint32_t freq = nla_get_u32(tb_freq[NL80211_FREQUENCY_ATTR_FREQ]);
          ptr->freqs[ptr->nbfreqs] = freq;

          if (freq == 2484) freq = 14;
          else if (freq < 2484) freq = (freq - 2407) / 5;
          else if (freq < 5000) freq = 15 + ((freq - 2512) / 20);
          else freq = ((freq - 5000) / 5);

          ptr->chans[ptr->nbfreqs] = freq;
          ptr->nbfreqs++;
        }
      }
    }
  }

  return NL_SKIP;
}

/******************************************************************************/
void unblock_rfkill(elt_t *elt) {

  char *ptr,*netpath = "/sys/class/net";
  char path[1024],buf[1024];
  ssize_t lenlink;
  struct dirent *dir1;
  DIR *d1;
  FILE *fd;

  for(uint8_t i=0;i<elt->nb;i++) {
    sprintf(path,"%s/%s/device/driver",netpath,elt->devs[i].ifname);
    if ((lenlink = readlink(path, buf, sizeof(buf)-1)) != -1) {
      buf[lenlink] = '\0';
      ptr = strrchr( buf, '/' );
      strcpy(elt->devs[i].drivername, ++ptr);
    }
    sprintf(path,"%s/%s/phy80211",netpath,elt->devs[i].ifname);
    d1 = opendir(path);
    while ((dir1 = readdir(d1)) != NULL)
      if ((strncmp("rfkill",dir1->d_name,5)) == 0) break;
    if ((strncmp("rfkill",dir1->d_name,6)) == 0) {
      sprintf(path,"%s/%s/phy80211/%s/soft",netpath,elt->devs[i].ifname,dir1->d_name);
      fd = fopen(path,"r+");
      if (fgetc(fd)==49) {
        fseek(fd, -1, SEEK_CUR);
        fputc(48, fd);
      };
      fclose(fd);
    }
  }
}

/*****************************************************************************/
//sudo sh -c "echo -n '1-1:1.0' > /sys/bus/usb/drivers/rtw_8812au/bind"
bool reload(char *ifname) {

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

/******************************************************************************/
void setbond(struct rtnl_link *ltap[2], char *name, struct nl_sock *sockrt, wfb_utils_netlink_bond_t *bonds) {
      // 	struct rtnl_link **link) {

  struct rtnl_link *old;
  struct nl_cache *cache;
  if ((rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache)) < 0) exit(-1);
  if (!(old = rtnl_link_alloc ())) exit(-1);
  if ((old = rtnl_link_get_by_name(cache, name))) {
    if (rtnl_link_delete(sockrt, old) < 0) exit(-1);
  }

  struct rtnl_link *opts; 
  if (!(opts = rtnl_link_alloc ())) exit(-1);
  if ((rtnl_link_bond_add(sockrt, name, opts)) < 0) exit(-1); 

  if ((rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache)) < 0) exit(-1);
  if (!(bonds->link = rtnl_link_alloc ())) exit(-1);
  if (!(bonds->link = rtnl_link_get_by_name(cache, name))) exit(-1);

  if ((rtnl_link_bond_enslave(sockrt, bonds->link, ltap[0])) < 0) exit(-1);
  if ((rtnl_link_bond_enslave(sockrt, bonds->link, ltap[1])) < 0) exit(-1);

  struct rtnl_link *change;
  if (!(rtnl_link_get_flags (bonds->link) & IFF_UP)) {
    change = rtnl_link_alloc ();
    rtnl_link_set_flags (change, IFF_UP);
    rtnl_link_change(sockrt, bonds->link, change, 0);
  }

  uint16_t protocol = htons(ETH_P_ALL);
  if (-1 == (bonds->sockfd = socket(AF_PACKET,SOCK_RAW,protocol))) exit(-1);
  struct sockaddr_ll sll;
  memset( &sll, 0, sizeof( sll ) );
  sll.sll_family   = AF_PACKET;
  sll.sll_ifindex  = rtnl_link_get_ifindex(bonds->link);
  sll.sll_protocol = protocol;
  if (-1 == bind(bonds->sockfd, (struct sockaddr *)&sll, sizeof(sll))) exit(-1);
}

/******************************************************************************/
uint8_t setwifi(elt_t *elt, wfb_utils_netlink_socknl_t *n, struct nl_sock *sockrt) {

  bool msg_received = false;

  struct nl_cb *cb1 = nl_cb_alloc(NL_CB_DEFAULT);
  if (!cb1) return(0);
  nl_cb_set(cb1, NL_CB_VALID, NL_CB_CUSTOM, getallinterfaces_callback, elt);
  nl_cb_set(cb1, NL_CB_FINISH, NL_CB_CUSTOM, finish_callback, &msg_received);

  struct nl_msg *msg1 = nlmsg_alloc();
  if (!msg1) return(0);
  genlmsg_put(msg1, NL_AUTO_PORT, NL_AUTO_SEQ, n->sockid, 0, NLM_F_DUMP, NL80211_CMD_GET_INTERFACE, 0);
  nl_send_auto(n->socknl, msg1);
  msg_received = false;
  while (!msg_received) nl_recvmsgs(n->socknl, cb1);
  nlmsg_free(msg1);

  if (elt->nb == 0) return(0);

  for(uint8_t i=0;i<elt->nb;i++) reload(elt->devs[i].ifname);
  sleep(1.0);

  struct nl_cache *cache;
  for(uint8_t i=0;i<elt->nb;i++) {
    if ((rtnl_link_alloc_cache(sockrt, n->sockid, &cache)) < 0) exit(-1);
    if (!(elt->devs[i].ltap = rtnl_link_get_by_name(cache, elt->devs[i].ifname))) exit(-1);
    elt->devs[i].ifindex = rtnl_link_get_ifindex(elt->devs[i].ltap);
  }

  struct rtnl_link *change;
  for(uint8_t i=0;i<elt->nb;i++) {
    if (IFF_UP & rtnl_link_get_flags(elt->devs[i].ltap)) {
      if (!(change = rtnl_link_alloc())) exit(-1);
      rtnl_link_unset_flags(change, IFF_UP);
      if ((rtnl_link_change(sockrt, elt->devs[i].ltap, change, 0)) < 0) exit(-1);
    }
  }

  struct nl_msg *nlmsg;
  for(uint8_t i=0;i<elt->nb;i++) {
    if (!(nlmsg  = nlmsg_alloc())) exit(-1);
    genlmsg_put(nlmsg,0,0,n->sockid,0,0,NL80211_CMD_SET_INTERFACE,0);  //  DOWN interfaces
    nla_put_u32(nlmsg, NL80211_ATTR_IFINDEX, elt->devs[i].ifindex);
    nla_put_u32(nlmsg, NL80211_ATTR_IFTYPE,NL80211_IFTYPE_MONITOR);
    nl_send_auto(n->socknl, nlmsg);
    if (nl_send_auto(n->socknl, nlmsg) >= 0)  nl_recvmsgs_default(n->socknl);
    nlmsg_free(nlmsg);
  }

  unblock_rfkill(elt);

  for(uint8_t i=0;i<elt->nb;i++) {
    if (!(rtnl_link_get_flags (elt->devs[i].ltap) & IFF_UP)) {
      change = rtnl_link_alloc ();
      rtnl_link_set_flags (change, IFF_UP);
      rtnl_link_change(sockrt, elt->devs[i].ltap, change, 0);
    }
  }

  nl_cb_set(cb1, NL_CB_VALID, NL_CB_CUSTOM, getsinglewifi_callback, elt);
  for(uint8_t i=0;i<elt->nb;i++) {
    elt->curr = i;
    struct nl_msg *msg2 = nlmsg_alloc();
    if (!msg2) return(0);
    genlmsg_put(msg2, NL_AUTO_PORT, NL_AUTO_SEQ, n->sockid, 0, NLM_F_DUMP, NL80211_CMD_GET_WIPHY, 0);
    nla_put_u32(msg2, NL80211_ATTR_IFINDEX, elt->devs[i].ifindex);
    nl_send_auto(n->socknl, msg2);
    msg_received = false;
    while (!msg_received) nl_recvmsgs(n->socknl, cb1);
    nlmsg_free(msg2);
  }

  return(elt->nb);
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

/******************************************************************************/
uint8_t setraw(elt_t *elt, wfb_utils_netlink_raw_t *arr[]) {

  uint8_t cpt = 0;
  uint16_t protocol = htons(ETH_P_ALL);

  for(uint8_t i=0;i<elt->nb;i++) {
    if (strcmp(elt->devs[i].drivername,DRIVER_NAME)!=0) continue;
    if (-1 == (elt->devs[i].sockfd = socket(AF_PACKET,SOCK_RAW,protocol))) continue;

    struct sockaddr_ll sll;
    memset( &sll, 0, sizeof( sll ) );
    sll.sll_family   = AF_PACKET;
    sll.sll_ifindex  = elt->devs[i].ifindex;
    sll.sll_protocol = protocol;
    if (-1 == bind(elt->devs[i].sockfd, (struct sockaddr *)&sll, sizeof(sll))) continue;

    drain(elt->devs[i].sockfd);

    const int32_t sock_qdisc_bypass = 1;
    if (-1 == setsockopt(elt->devs[i].sockfd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass))) continue;

    arr[cpt] = &(elt->devs[i]);
    cpt++;
  }
  return(cpt);
}

/*****************************************************************************/
/*****************************************************************************/
bool wfb_utils_netlink_setfreq(wfb_utils_netlink_socknl_t *psock, int ifindex, uint32_t freq) {

  bool ret=true;
  struct nl_msg *msg=nlmsg_alloc();
  genlmsg_put(msg,0,0,psock->sockid,0,0,NL80211_CMD_SET_CHANNEL,0);
  NLA_PUT_U32(msg,NL80211_ATTR_IFINDEX,ifindex);
  NLA_PUT_U32(msg,NL80211_ATTR_WIPHY_FREQ,freq);
  if (nl_send_auto(psock->socknl, msg) < 0) ret=false;
  nlmsg_free(msg);
  return(ret);
  nla_put_failure:
    nlmsg_free(msg);
    return(false);
}

/******************************************************************************/
bool wfb_utils_netlink_init(wfb_utils_netlink_init_t *n) {

  if  (!(n->sockidnl.socknl = nl_socket_alloc())) return(false);
  nl_socket_set_buffer_size(n->sockidnl.socknl, 8192, 8192);
  if (genl_connect(n->sockidnl.socknl)) return(false);
  if ((n->sockidnl.sockid = genl_ctrl_resolve(n->sockidnl.socknl, "nl80211")) < 0) return(false);

  struct nl_sock *sockrt;
  if (!(sockrt = nl_socket_alloc())) return(false);
  if (nl_connect(sockrt, NETLINK_ROUTE)) return(false);

  static wfb_utils_netlink_raw_t all[MAXRAWDEV];
  elt_t elt; memset(&elt, 0, sizeof(elt_t)); elt.devs = all;

  uint8_t nb;
  if ((nb = setwifi(&elt, &n->sockidnl, sockrt)) > 0) {
    if ((nb = setraw(&elt, n->rawdevs)) > 0) {
      if (nb > 1) {
        struct rtnl_link *ltap[2]; ltap[0] = elt.devs[0].ltap; ltap[1] = elt.devs[1].ltap;
	//setbond(ltap, BOND_NAME, sockrt, &n->bonds[0].link);
	setbond(ltap, BOND_NAME, sockrt, &n->bonds[0]);
      }
      n->nbraws = nb;
      return(true);
    }
  }
  return(false);
}
