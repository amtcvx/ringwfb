/*

gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c multiraw_3.c -o multiraw_3.o

cc multiraw_3.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o exe_multiraw_3

sudo ./exe_multiraw_3

*/

#include <unistd.h>
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
#include <linux/if_packet.h>
#include <linux/filter.h>

#include <stdbool.h>

#include <netlink/route/link.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>


#include <linux/nl80211.h>

#include <netlink/route/link.h>
#include <net/if.h>

#include <dirent.h>

#include <time.h>

#include <errno.h>

/************************************************************************************************/
#define DRONEID 0
//#define DRONEID 1

#define MAXRAWDEV 4

#define DRIVER_NAME "rtl88XXau"

#define PAY_MTU 1500

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
typedef struct {
  uint8_t droneid;
  uint64_t seq;
  uint16_t msglen;
  int32_t backfreq;
} __attribute__((packed)) payhd_t;

/*****************************************************************************/
#define NBFREQS 65
#define PERIOD_DELAY_S  1

/*****************************************************************************/
typedef struct {
  uint8_t cptfreq;
  uint8_t nbfreqs;
  uint32_t freqs[NBFREQS];
  uint32_t chans[NBFREQS];
} rawdev_t;

/******************************************************************************/
int finish_callback(struct nl_msg *nlmsg, void *arg) {
  bool* finished = arg;
  *finished = true;
  return NL_SKIP;
}

/******************************************************************************/
int getsinglewifi_callback(struct nl_msg *nlmsg, void *arg) {

  struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(nlmsg));
  struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
  nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

  if (tb_msg[NL80211_ATTR_WIPHY_BANDS]) {
    struct nlattr *tb_band[NL80211_BAND_ATTR_MAX + 1];
    struct nlattr *tb_freq[NL80211_FREQUENCY_ATTR_MAX + 1];
    struct nlattr *nl_band;
    struct nlattr *nl_freq;
    int rem_band, rem_freq;
    int last_band = -1;

    rawdev_t *ptr = ((rawdev_t *)arg);

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
void upfreq(uint8_t sockid, struct nl_sock *socknl, uint8_t raw, uint32_t ifindex, uint8_t nbraws, rawdev_t *rawdevs ) {

  if ((++(rawdevs[raw].cptfreq)) >= (rawdevs[raw].nbfreqs)) rawdevs[raw].cptfreq = 0;
  for (uint8_t j = 0; j < nbraws; j++) {
    if ((raw != j) && ((rawdevs[raw].cptfreq) == (rawdevs[j].cptfreq))) {
      if ((++(rawdevs[raw].cptfreq)) >= (rawdevs[raw].nbfreqs)) rawdevs[raw].cptfreq = 0;
    }
  }
  setfreq(sockid, socknl, ifindex, rawdevs[raw].freqs[rawdevs[raw].cptfreq]);
}

/*****************************************************************************/
//sudo sh -c "echo -n '1-1:1.0' > /sys/bus/usb/drivers/rtw_8812au/bind"
bool reload(char *ifname) {

  char netpath[] = "/sys/class/net";
  char driverpath[] = "/sys/bus/usb/drivers/";

  bool ret = false;

  char dirpath[1024];
  strcpy(dirpath,driverpath);
  strcat(dirpath,DRIVER_NAME);

  DIR *d1;
  if (!(d1 = opendir(dirpath))) exit(-1);

  char path[1024],buf[1024];
  sprintf(path,"%s/%s/device",netpath,ifname);

  ssize_t lenlink;
  if ((lenlink = readlink(path, buf, sizeof(buf)-1)) != -1) {
    buf[lenlink] = '\0';
    char *ptr = strrchr( buf, '/' );
    ptr++;
    strcpy(path,dirpath);
    strcat(path,"/unbind");
    FILE *fd;
    fd = fopen(path,"a");
    fputs(ptr,fd);fflush(fd);
    strcpy(path,dirpath);
    strcat(path,"/bind");
    fclose(fd);
    fd = fopen(path,"a");
    fputs(ptr,fd);fflush(fd);
    fclose(fd);
    ret = true;
  }
  closedir(d1);

  return(ret);
}

/******************************************************************************/
void unblock_rfkill(char *ifname) {

  char netpath[] = "/sys/class/net";

  ssize_t lenlink;
  char path[1024],buf[1024],drivername[50];
  
  sprintf(path,"%s/%s/device/driver",netpath,ifname);

  if ((lenlink = readlink(path, buf, sizeof(buf)-1)) != -1) {
    buf[lenlink] = '\0';
    char *ptr = strrchr( buf, '/' );
    strcpy(drivername, ++ptr);
  }

  if (strcmp(drivername, DRIVER_NAME) == 0) {

    sprintf(path,"%s/%s/phy80211",netpath,ifname);

    DIR *d1;
    if (!(d1 = opendir(path))) exit(-1);

    struct dirent *dir1;
    while ((dir1 = readdir(d1)) != NULL) if ((strncmp("rfkill",dir1->d_name,5)) == 0) break;
    if ((strncmp("rfkill",dir1->d_name,6)) == 0) {
      sprintf(path,"%s/%s/phy80211/%s/soft",netpath,ifname,dir1->d_name);
      FILE *fd = fopen(path,"r+");
      if (fgetc(fd)==49) {
        fseek(fd, -1, SEEK_CUR);
        fputc(48, fd);
      };
      fclose(fd);
    }
    closedir(d1);
  }
}

/*****************************************************************************/
void  setraw(uint8_t sockid, struct nl_sock *socknl, struct nl_sock *sockrt, char *ifname, uint32_t *ifindex, rawdev_t *prawdev ) {

  struct nl_cache *cache;
  struct rtnl_link *ltap;

  if ((rtnl_link_alloc_cache(sockrt, sockid, &cache)) < 0) exit(-1);
  if (!(ltap = rtnl_link_get_by_name(cache, ifname))) exit(-1);
  *ifindex = rtnl_link_get_ifindex(ltap);
  rtnl_link_put(ltap); nl_cache_free(cache);

  struct nl_msg *nlmsg;
  if (!(nlmsg  = nlmsg_alloc())) exit(-1);;
  genlmsg_put(nlmsg,0,0,sockid,0,0,NL80211_CMD_SET_INTERFACE,0);  //  DOWN interfaces
  nla_put_u32(nlmsg, NL80211_ATTR_IFINDEX, *ifindex);
  nla_put_u32(nlmsg, NL80211_ATTR_IFTYPE,NL80211_IFTYPE_MONITOR);
  nl_send_auto(socknl, nlmsg);
  if (nl_send_auto(socknl, nlmsg) >= 0)  nl_recvmsgs_default(socknl);
  nlmsg_free(nlmsg);

  if ((rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache)) < 0) exit(-1);
  if (!(ltap = rtnl_link_get(cache,*ifindex))) exit(-1);
  if (!(rtnl_link_get_flags (ltap) & IFF_UP)) {
    struct rtnl_link *change;
    change = rtnl_link_alloc ();
    rtnl_link_set_flags (change, IFF_UP);
    rtnl_link_change(sockrt, ltap, change, 0);
    rtnl_link_put(change);
  }
  rtnl_link_put(ltap); nl_cache_free(cache);

  bool msg_received = false;
  struct nl_cb *cb;
  if (!(cb = nl_cb_alloc(NL_CB_DEFAULT))) exit(-1);
  nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_callback, &msg_received);
  nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, getsinglewifi_callback, prawdev);

  if (!(nlmsg  = nlmsg_alloc())) exit(-1);
  genlmsg_put(nlmsg, NL_AUTO_PORT, NL_AUTO_SEQ, sockid, 0, NLM_F_DUMP, NL80211_CMD_GET_WIPHY, 0);
  nla_put_u32(nlmsg, NL80211_ATTR_IFINDEX, *ifindex);
  nl_send_auto(socknl, nlmsg);
  msg_received = false;
  while (!msg_received) nl_recvmsgs(socknl, cb);

  nlmsg_free(nlmsg);
  nl_cb_put(cb);
}

/*****************************************************************************/
void  setsock(uint8_t *fd, uint32_t index) {

  uint16_t protocol = htons(ETH_P_ALL);
  if (-1 == (*fd = socket(AF_PACKET,SOCK_RAW, protocol))) exit(-1);

  uint32_t bufsize = 1600; // PAY_MTU + 100 

  if(setsockopt(*fd, SOL_SOCKET, SO_SNDBUF, &bufsize , sizeof(bufsize)) !=0) exit(-1);
  if(setsockopt(*fd, SOL_SOCKET, SO_RCVBUF, &bufsize , sizeof(bufsize)) !=0) exit(-1);

  struct sockaddr_ll sll;
  memset( &sll, 0, sizeof( sll ) );
  sll.sll_family   = AF_PACKET;
  sll.sll_ifindex  = index;
  sll.sll_protocol = protocol;

  if (-1 == bind(*fd, (struct sockaddr *)&sll, sizeof(sll))) exit(-1); // must be AFTER wifi setting
  drain(*fd);

  const int32_t sock_qdisc_bypass = 1;
  if (-1 == setsockopt(*fd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass))) exit(-1);
}

/******************************************************************************/
uint8_t getwifi(char ifnames[MAXRAWDEV][50]) {

  char netpath[] = "/sys/class/net";
  uint8_t cpt=0;
  DIR *d1;
  if (!(d1 = opendir(netpath))) exit(-1);
  struct dirent *dir1;
  while ((dir1 = readdir(d1)) != NULL) {
    ssize_t lenlink;
    char path[1024],buf[1024];
    sprintf(path,"%s/%s/device/driver",netpath,dir1->d_name);
    if ((lenlink = readlink(path, buf, sizeof(buf)-1)) != -1) {
      buf[lenlink] = '\0';
      char *ptr = strrchr( buf, '/' );
      if (strcmp(DRIVER_NAME, ++ptr)==0) strcpy(ifnames[cpt++],dir1->d_name);
    }
  }
  closedir(d1);
  for(uint8_t j=0; j < cpt; j++) reload(ifnames[j]);
  sleep(1.0);
  for(uint8_t j=0; j < cpt; j++) unblock_rfkill(ifnames[j]);
  return(cpt);
}


/*****************************************************************************/
uint64_t get_time_ms(void) {

  struct timespec ts;
  int rc = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (rc < 0) { printf("Error getting time: %s", strerror(errno)); fflush(stdout); }
  return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

/*****************************************************************************/
int main(int argc, char **argv) {

  char ifnames[MAXRAWDEV][50];
  uint8_t nbraws = getwifi(ifnames);
  if (nbraws == 0) exit(-1);

  for (uint8_t i=0; i<nbraws; i++) printf("[%d](%s)\n",i,ifnames[i]);

  struct nl_sock *socknl;
  uint8_t sockid;
  if  (!(socknl = nl_socket_alloc()))  exit(-1);
  nl_socket_set_buffer_size(socknl, 8192, 8192);
  if (genl_connect(socknl)) exit(-1);
  if ((sockid = genl_ctrl_resolve(socknl, "nl80211")) < 0) exit(-1);

  struct nl_sock *sockrt;
  if (!(sockrt = nl_socket_alloc())) exit(-1);
  if (nl_connect(sockrt, NETLINK_ROUTE)) exit(-1);

  uint8_t rawfds[nbraws];
  uint32_t index[nbraws];
  rawdev_t rawdevs[nbraws];
  memset(rawdevs, 0, sizeof(rawdevs));

  uint8_t nbfds = nbraws;
  struct pollfd readsets[nbfds];
  memset(readsets, 0, sizeof(readsets));
  for (uint8_t i = 0; i <  nbraws; i++) {
    setraw(sockid, socknl, sockrt, ifnames[i], &index[i], &rawdevs[i]);
    printf("[%s][%d][%d]\n",ifnames[i], index[i], rawdevs[i].nbfreqs);
    setsock( &rawfds[i], index[i]);
    printf("[%d][%d]\n",rawfds[i], index[i]);
    readsets[i].fd = rawfds[i]; readsets[i].events = POLLIN;
  }

  setfreq(sockid, socknl, index[0], 5460);
  setfreq(sockid, socknl, index[1], 5480);

  struct timespec ts;
  uint64_t curms,  stoms, intms = 1000;
  clock_gettime(CLOCK_MONOTONIC, &ts); stoms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

  uint8_t txradiotaphd[] = {
        0x00, 0x00, // <-- radiotap version
        0x0d, 0x00, // <- radiotap header length
        0x00, 0x80, 0x08, 0x00, // <-- radiotap present flags:  RADIOTAP_TX_FLAGS + RADIOTAP_MCS
        0x08, 0x00,  // RADIOTAP_F_TX_NOACK
        MCS_KNOWN , MCS_FLAGS, MCS_INDEX // bitmap, flags, mcs_index
  };
  uint8_t txieeehd[] = {
        0x08, 0x01,                         // Frame Control : Data frame from STA to DS
        0x00, 0x00,                         // Duration
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Receiver MAC
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Transmitter MAC
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Destination MAC
        0x10, 0x86                          // Sequence control
  };
  uint8_t txllchd[4];
  payhd_t txpayhd;
  uint8_t txpaybuf[PAY_MTU];
  struct msghdr txmsg = { .msg_name = NULL, .msg_namelen = 0, .msg_control = NULL, .msg_controllen = 0, .msg_flags = 0 };

  struct iovec txiov[2] = { 
	  { .iov_base = (void *)&txradiotaphd, .iov_len = sizeof(txradiotaphd) },
	  { .iov_base = (void *)&txieeehd, .iov_len = sizeof(txieeehd) },
  };
  txmsg.msg_iov = txiov; txmsg.msg_iovlen = 2;
/*
  struct iovec txiov[5];
  txiov[0].iov_base = txradiotaphd;     txiov[0].iov_len = sizeof(txradiotaphd);
  txiov[1].iov_base = txieeehd;         txiov[1].iov_len = sizeof(txieeehd);
  txiov[2].iov_base = txllchd;          txiov[2].iov_len = sizeof(txllchd);
  txiov[3].iov_base = (void *)&txpayhd; txiov[3].iov_len = sizeof(txpayhd);
  txiov[4].iov_base = txpaybuf;         txiov[4].iov_len = sizeof(txpaybuf);
  txmsg.msg_iov = txiov; txmsg.msg_iovlen = 5;
*/

#define RXRADIOTAPSIZE 35
#define RXLOG 8
  struct rx_t {
    uint8_t rxradiotaphd[RXRADIOTAPSIZE];
    uint8_t rxieeehd[24];
    uint8_t rxllchd[4];
    payhd_t rxpayhd;
    uint8_t rxpaybuf[PAY_MTU];
    struct iovec rxiov[5];
    struct msghdr rxmsg;
  } rx[MAXRAWDEV][RXLOG];

  size_t rawlen[nbraws], rawlog[nbraws];
  for (uint8_t i = 0; i< nbraws; i++) { rawlen[i] = 0; rawlog[i] = 0; }

  uint8_t pos = 0;
  int32_t tmp = 0; 
  bool send_first = false;

  while (true) {

    clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
    poll(readsets, nbfds, stoms > curms ? stoms - curms : 0);
    clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

    if (curms >= stoms) { // SYNCHRONOUS
      stoms = curms + intms - ((curms - stoms) % intms);
      send_first = true;
      printf("(%ld) (%ld) TIC (%ld)(%ld)\n",rawlen[0],rawlen[1],rawlog[0],rawlog[1]);fflush(stdout);
      rawlen[0] = 0; rawlen[1] = 0; rawlog[0] = 0; rawlog[1] = 0;
    }

    for (uint8_t cpt = 0; cpt < nbfds; cpt++) { // ASYNCHRONOUS RECV
      tmp = 0;
      while (tmp >= 0) {

        struct rx_t *rxcur = &rx[cpt][pos];

        rxcur->rxiov[0].iov_base = (void *)&rxcur->rxradiotaphd;  rxcur->rxiov[0].iov_len = sizeof(rxcur->rxradiotaphd);
        rxcur->rxiov[1].iov_base = (void *)&rxcur->rxieeehd;      rxcur->rxiov[1].iov_len = sizeof(rxcur->rxieeehd);
        rxcur->rxiov[2].iov_base = (void *)&rxcur->rxllchd;       rxcur->rxiov[2].iov_len = sizeof(rxcur->rxllchd);
        rxcur->rxiov[3].iov_base = (void *)&rxcur->rxpayhd;       rxcur->rxiov[3].iov_len = sizeof(rxcur->rxpayhd);
        rxcur->rxiov[4].iov_base = (void *)&rxcur->rxpaybuf;      rxcur->rxiov[3].iov_len = sizeof(rxcur->rxpaybuf);
	rxcur->rxmsg.msg_iov = rxcur->rxiov;                      rxcur->rxmsg.msg_iovlen = 5;
        rxcur->rxmsg.msg_control = NULL;                          rxcur->rxmsg.msg_controllen = 0;
        rxcur->rxmsg.msg_name = NULL;                             rxcur->rxmsg.msg_namelen = 0;
        rxcur->rxmsg.msg_flags = 0;

        memset(rxcur->rxmsg.msg_iov[1].iov_base, 0 , rxcur->rxmsg.msg_iov[1].iov_len);

        tmp = recvmsg(rawfds[cpt], &rxcur->rxmsg, MSG_DONTWAIT); rawlen[cpt] += tmp;

        if ((tmp > 0) && ((*(4 + ((uint8_t *)&rxcur->rxmsg.msg_iov[1].iov_base)) == 0x66))) { rawlog[cpt]++; pos++; }

      }
    }

    if (send_first) { // SYNCHRONOUS AND ASYNCHRONOUS SEND
      send_first = false;
      tmp = sendmsg(rawfds[0], &txmsg, MSG_DONTWAIT);
      printf("SEND [0][%d]\n",tmp);fflush(stdout);

      // NEED TO RESET TX TO AVOID DUPLICATION IN RX !!
      // memset(txmsg.msg_iov[1].iov_base, 0 , txmsg.msg_iov[1].iov_len);
    }
  }
}
