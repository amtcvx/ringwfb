/*
sudo apt-get install libnl-3-dev libnl-genl-3-dev libnl-route-3-dev

gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c mppbridgeraw.c -o mppbridgeraw.o

cc mppbridgeraw.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o exe_mppbridgeraw

sudo ./exe_bridgeraw 

sudo ip link add name br0 type bridge

sudo ip address show br0
sudo ip link show master br0

sudo ip link del name br0

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
#include <sys/ioctl.h>
#include <linux/if_packet.h>

#include <stdint.h>
#include <stdbool.h>

#include <sys/timerfd.h>


#define DRIVERNAME "rtl88XXau"
//#define DRIVERNAME "rtw88_8812au"
//#define DRIVERNAME "rtw_8812au"

#define MAXRAWDEV 4

#define PERIOD_DELAY_S  1

#define NBFREQS 65

typedef struct {
  uint8_t sockfd;
  char drivername[30];
  char ifname[30];
  int ifindex;
  int iftype;
  uint8_t freq;
  uint8_t nbfreqs;
  uint32_t freqs[NBFREQS];
  uint32_t chans[NBFREQS];
} wfb_net_device_t;

typedef struct {
  uint8_t sockid;
  struct nl_sock *socknl;
} wfb_net_sockidnl_t;

typedef struct {
  uint8_t nbraws;
  wfb_net_sockidnl_t sockidnl;
  wfb_net_device_t *rawdevs[MAXRAWDEV];
} wfb_net_init_t;

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

/************************************************************************************************/
typedef struct {
  uint8_t nb;
  uint8_t curr;
  wfb_net_device_t *devs;
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

  wfb_net_device_t *ptr = &(((elt_t *)arg)->devs[((elt_t *)arg)->nb]);

  char ifname[30];
  if (tb_msg[NL80211_ATTR_IFNAME]) {
    strcpy(ifname, nla_get_string(tb_msg[NL80211_ATTR_IFNAME]));
    if (strlen(ifname) > 0) {
      strcpy(ptr->ifname, ifname);
      if (tb_msg[NL80211_ATTR_IFINDEX]) ptr->ifindex = nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]);
      if (tb_msg[NL80211_ATTR_IFTYPE])  ptr->iftype = nla_get_u32(tb_msg[NL80211_ATTR_IFTYPE]);
//      if ((ptr->iftype != NL80211_IFTYPE_MONITOR) && (((elt_t *)arg)->nb < 2)) ((((elt_t *)arg)->nb)++);
      if (((elt_t *)arg)->nb < 2) ((((elt_t *)arg)->nb)++);
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

    wfb_net_device_t *ptr = &(((elt_t *)arg)->devs[((elt_t *)arg)->curr]);

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
  strcat(dirpath,DRIVERNAME);
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
    printf("TOP\n");fflush(stdout);
  }
  return(ret);
}

/******************************************************************************/
uint8_t setwifi(elt_t *elt, wfb_net_sockidnl_t *n) {

  bool msg_received = false;

  struct nl_sock *sockrt;
  if (!(sockrt = nl_socket_alloc())) return(false);
  if (nl_connect(sockrt, NETLINK_ROUTE)) return(false);

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

  for(uint8_t i=0;i<elt->nb;i++) {
    struct nl_msg *msg3 = nlmsg_alloc();
    if (!msg3) return(0);
    genlmsg_put(msg3,0,0,n->sockid,0,0,NL80211_CMD_SET_INTERFACE,0);  //  DOWN interfaces
    nla_put_u32(msg3, NL80211_ATTR_IFINDEX, elt->devs[i].ifindex);
    nla_put_u32(msg3, NL80211_ATTR_IFTYPE,NL80211_IFTYPE_MONITOR);
    nl_send_auto(n->socknl, msg3);
    if (nl_send_auto(n->socknl, msg3) >= 0)  nl_recvmsgs_default(n->socknl);
    nlmsg_free(msg3);
  }

  unblock_rfkill(elt);

  int8_t err = 0;
  struct nl_cache *cache;
  struct rtnl_link *link, *change;
  if ((err = rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache)) >= 0) {
    for(uint8_t i=0;i<elt->nb;i++) {
      if ((link = rtnl_link_get(cache,elt->devs[i].ifindex))) {
        if (!(rtnl_link_get_flags (link) & IFF_UP)) {
          change = rtnl_link_alloc ();
          rtnl_link_set_flags (change, IFF_UP);
          rtnl_link_change(sockrt, link, change, 0);
        }
      }
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
void wfb_net_drain(uint8_t fd) {

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
uint8_t setraw(elt_t *elt, wfb_net_device_t *arr[]) {

  uint8_t cpt = 0;
  uint16_t protocol = htons(ETH_P_ALL);

  for(uint8_t i=0;i<elt->nb;i++) {
    if (strcmp(elt->devs[i].drivername,DRIVERNAME)!=0) continue;
    if (-1 == (elt->devs[i].sockfd = socket(AF_PACKET,SOCK_RAW,protocol))) continue;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy( ifr.ifr_name, elt->devs[i].ifname, sizeof( ifr.ifr_name ) - 1 );
    if (ioctl( elt->devs[i].sockfd, SIOCGIFINDEX, &ifr ) < 0 ) continue;
    struct sockaddr_ll sll;
    memset( &sll, 0, sizeof( sll ) );
    sll.sll_family   = AF_PACKET;
    sll.sll_ifindex  = ifr.ifr_ifindex;
    sll.sll_protocol = protocol;
    if (-1 == bind(elt->devs[i].sockfd, (struct sockaddr *)&sll, sizeof(sll))) continue;

    wfb_net_drain(elt->devs[i].sockfd);

    const int32_t sock_qdisc_bypass = 1;
    if (-1 == setsockopt(elt->devs[i].sockfd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass))) continue;

    arr[cpt] = &(elt->devs[i]);
    cpt++;
  }
  return(cpt);
}

/*****************************************************************************/
bool wfb_net_setfreq(wfb_net_sockidnl_t *psock, int ifindex, uint32_t freq) {

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
bool wfb_net_init(wfb_net_init_t *n) {

  if  (!(n->sockidnl.socknl = nl_socket_alloc())) return(false);
  nl_socket_set_buffer_size(n->sockidnl.socknl, 8192, 8192);
  if (genl_connect(n->sockidnl.socknl)) return(false);
  if ((n->sockidnl.sockid = genl_ctrl_resolve(n->sockidnl.socknl, "nl80211")) < 0) return(false);

  static wfb_net_device_t wfb_net_all80211[MAXRAWDEV];
  elt_t elt; memset(&elt, 0, sizeof(elt_t)); elt.devs = wfb_net_all80211;

  uint8_t nb;
  if ((nb = setwifi(&elt, &n->sockidnl)) > 0) {
    if ((nb = setraw(&elt, n->rawdevs)) > 0) {
      n->nbraws = nb;
      return(true);
    }
  }
  return(false);
}

/*****************************************************************************/
int main(int argc, char **argv) {

  wfb_net_init_t n;
  if (false == wfb_net_init(&n)) { printf("NO WIFI\n"); exit(-1); }
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
    wfb_net_setfreq(&n.sockidnl, n.rawdevs[rawcpt]->ifindex, n.rawdevs[rawcpt]->freqs[n.rawdevs[rawcpt]->freq]);
  }

  struct pollfd readsets[nbfds];
  for (uint8_t nbfdscpt=0; nbfdscpt < nbfds; nbfdscpt++) { readsets[nbfdscpt].fd = fd[nbfdscpt]; readsets[nbfdscpt].events = POLLIN; }

  ssize_t len = 0, len1;
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
//            len = sendmsg(brdfd, (const struct msghdr *)&msg, MSG_DONTWAIT);
            len1 = sendmsg(fd[1], (const struct msghdr *)&msg, MSG_DONTWAIT);
 //           len2 = sendmsg(fd[2], (const struct msghdr *)&msg, MSG_DONTWAIT);
            printf("[%d][%d]  (%ld)\n",rawpkt[0],rawpkt[1],len1);
            rawpkt[0] = 0; rawpkt[1] = 0;
          } else {
            if ((len = recvmsg(fd[cpt], &msg, MSG_DONTWAIT)) > 0)
            rawpkt[cpt-1]+=len;
          }
        }
      }
    }
  }

}
