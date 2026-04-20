/*

gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c multiraw.c -o multiraw.o

cc multiraw.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o exe_multiraw

sudo ./exe_multiraw

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

#include <sys/timerfd.h>

#include <errno.h>

/************************************************************************************************/
#define DRONEID 0
//#define DRONEID 1

#define MAXRAWDEV 4

#define DRIVER_NAME "rtl88XXau"

#define PAY_MTU 1400

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
void unblock_rfkill(char *ifname) {

  char *ptr,*netpath = "/sys/class/net";
  char path[1024],buf[1024],drivername[50];
  ssize_t lenlink;
  struct dirent *dir1;
  DIR *d1;
  FILE *fd;

  sprintf(path,"%s/%s/device/driver",netpath,ifname);
  if ((lenlink = readlink(path, buf, sizeof(buf)-1)) != -1) {
    buf[lenlink] = '\0';
    ptr = strrchr( buf, '/' );
    strcpy(drivername, ++ptr);
  }

  if (strcmp(drivername, DRIVER_NAME) == 0) {

    sprintf(path,"%s/%s/phy80211",netpath,ifname);
    d1 = opendir(path);
    while ((dir1 = readdir(d1)) != NULL)
      if ((strncmp("rfkill",dir1->d_name,5)) == 0) break;
    if ((strncmp("rfkill",dir1->d_name,6)) == 0) {
      sprintf(path,"%s/%s/phy80211/%s/soft",netpath,ifname,dir1->d_name);
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
void  setraw(uint8_t sockid, struct nl_sock *socknl, struct nl_sock *sockrt, char *ifname, uint32_t *ifindex, rawdev_t *prawdev ) {

  struct nl_cache *cache;
  struct rtnl_link *ltap;

  if ((rtnl_link_alloc_cache(sockrt, sockid, &cache)) < 0) exit(-1);
  if (!(ltap = rtnl_link_get_by_name(cache, ifname))) exit(-1);
  *ifindex = rtnl_link_get_ifindex(ltap);

  struct nl_msg *nlmsg;
  if (!(nlmsg  = nlmsg_alloc())) exit(-1);;
  genlmsg_put(nlmsg,0,0,sockid,0,0,NL80211_CMD_SET_INTERFACE,0);  //  DOWN interfaces
  nla_put_u32(nlmsg, NL80211_ATTR_IFINDEX, *ifindex);
  nla_put_u32(nlmsg, NL80211_ATTR_IFTYPE,NL80211_IFTYPE_MONITOR);
  nl_send_auto(socknl, nlmsg);
  if (nl_send_auto(socknl, nlmsg) >= 0)  nl_recvmsgs_default(socknl);
  nlmsg_free(nlmsg);

  struct rtnl_link *link, *change;
  if ((rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache)) < 0) exit(-1);
  if (!(link = rtnl_link_get(cache,*ifindex))) exit(-1);
  if (!(rtnl_link_get_flags (link) & IFF_UP)) {
    change = rtnl_link_alloc ();
    rtnl_link_set_flags (change, IFF_UP);
    rtnl_link_change(sockrt, link, change, 0);
  }

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
}

/*****************************************************************************/
void  setsock(uint8_t *fd, uint32_t index) {
/*
  uint16_t protocol = htons(ETH_P_ALL);
  if (-1 == (*fd = socket(AF_PACKET,SOCK_RAW,protocol))) exit(-1);

  struct sockaddr_ll sll;
  memset( &sll, 0, sizeof( sll ) );
  sll.sll_family   = AF_PACKET;
  sll.sll_ifindex  = index;
  sll.sll_protocol = protocol;
  if (-1 == bind(*fd, (struct sockaddr *)&sll, sizeof(sll))) exit(-1); // TO BE CHECK BIND must be AFTER wifi setting

  drain(*fd);

  const int32_t sock_qdisc_bypass = 1;
  if (-1 == setsockopt(*fd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass))) exit(-1);
*/
/*
https://hundeboll.net/receiving-udp-packets-in-promiscuous-mode.html
*/

  uint16_t protocol = htons(ETH_P_ALL);
  if (-1 == (*fd = socket(AF_PACKET,SOCK_RAW,protocol))) exit(-1);
/*
  int sndlenbuf = 2048;
  if (-1 == setsockopt(*fd, SOL_PACKET, SO_SNDBUF, &sndlenbuf, sizeof(sndlenbuf))) exit(-1);
  int recvlenbuf = 2048;
  if (-1 == setsockopt(*fd, SOL_PACKET, SO_RCVBUF, &recvlenbuf, sizeof(recvlenbuf))) exit(-1);

  struct packet_mreq mreq = {0};
  int action;

  mreq.mr_ifindex = index;
  mreq.mr_type = PACKET_MR_PROMISC;

//  action = PACKET_ADD_MEMBERSHIP;
  action = PACKET_DROP_MEMBERSHIP;

  if (-1 == setsockopt(*fd, SOL_PACKET, action, &mreq, sizeof(mreq))) exit(-1);

//sudo tcpdump -dd not ether src 66:55:44:33:22:11

  struct sock_fprog prog;
  struct sock_filter filter[] = {
{ 0x20, 0, 0, 0x00000008 },
{ 0x15, 0, 3, 0x44332211 },
{ 0x28, 0, 0, 0x00000006 },
{ 0x15, 0, 1, 0x00006655 },
{ 0x6, 0, 0, 0x00000000 },
{ 0x6, 0, 0, 0x00040000 },
};

  prog.len = sizeof(filter)/sizeof(filter[0]);
  prog.filter = filter;

  if (-1 == setsockopt(*fd, SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof(prog))) exit(-1);
*/
  struct sockaddr_ll sa = {0};

  sa.sll_family = AF_PACKET;
  sa.sll_ifindex = index;
  sa.sll_protocol = htons(ETH_P_ALL);

  //sa.sll_pkttype = PACKET_OTHERHOST|PACKET_BROADCAST|PACKET_MULTICAST|PACKET_HOST;
  //sa.sll_pkttype = PACKET_OTHERHOST; // PACKET_OUTGOING;

  if (-1 == bind(*fd, (struct sockaddr *)&sa, sizeof(sa))) exit(-1);
}


/******************************************************************************/
uint8_t getwifi(char ifnames[MAXRAWDEV][50]) {

  char *netpath = "/sys/class/net";
  char path[1024],buf[1024],*ptr;
  ssize_t lenlink;
  uint8_t i=0;

  DIR *d1;
  struct dirent *dir1;
  d1 = opendir(netpath);
  while ((dir1 = readdir(d1)) != NULL) {
    sprintf(path,"%s/%s/device/driver",netpath,dir1->d_name);
    if ((lenlink = readlink(path, buf, sizeof(buf)-1)) != -1) {
      buf[lenlink] = '\0';
      ptr = strrchr( buf, '/' );
      if (strcmp(DRIVER_NAME, ++ptr)==0) strcpy(ifnames[i++],dir1->d_name);
    }
  }
  for(uint8_t j=0; j < i; j++) reload(ifnames[j]);
  sleep(1.0);

  for(uint8_t j=0; j < i; j++) unblock_rfkill(ifnames[j]);

  return(i);
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

  struct nl_sock *socknl;
  uint8_t sockid;
  if  (!(socknl = nl_socket_alloc()))  exit(-1);
  nl_socket_set_buffer_size(socknl, 8192, 8192);
  if (genl_connect(socknl)) exit(-1);
  if ((sockid = genl_ctrl_resolve(socknl, "nl80211")) < 0) exit(-1);

  struct nl_sock *sockrt;
  if (!(sockrt = nl_socket_alloc())) exit(-1);
  if (nl_connect(sockrt, NETLINK_ROUTE)) exit(-1);

  uint8_t nbfds = 1 + nbraws;
  uint8_t fds[nbfds];

  uint64_t exptime;
  if (-1 == (fds[0] = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(fds[0], 0, &period, NULL);

  struct pollfd readsets[nbfds];
  memset(readsets, 0, sizeof(readsets));
  readsets[0].fd = fds[0]; readsets[0].events = POLLIN;

  uint32_t index[nbraws];
  rawdev_t rawdevs[nbraws];
  for (uint8_t i = 0; i <  nbraws; i++) {
    memset(&rawdevs[i],0,sizeof(rawdevs[i]));
    setraw(sockid, socknl, sockrt, ifnames[i], &index[i], &rawdevs[i]);
    setsock( &fds[i + 1], index[i]);
    readsets[i + 1].fd = fds[i + 1]; readsets[i + 1].events = POLLIN;
  }

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

  uint8_t rxrawlog[MAXRAWDEV];

  for(uint8_t i = 0; i < MAXRAWDEV; i++) {
    for(uint8_t j = 0; j < RXLOG; j++) {
      rx[i][j].rxiov[0].iov_base = &rx[i][j].rxradiotaphd; rx[i][j].rxiov[0].iov_len = sizeof(rx[i][j].rxradiotaphd);
      rx[i][j].rxiov[1].iov_base = &rx[i][j].rxieeehd;     rx[i][j].rxiov[1].iov_len = sizeof(rx[i][j].rxieeehd);
      rx[i][j].rxiov[2].iov_base = &rx[i][j].rxllchd;      rx[i][j].rxiov[2].iov_len = sizeof(rx[i][j].rxllchd);
      rx[i][j].rxiov[3].iov_base = &rx[i][j].rxpayhd;      rx[i][j].rxiov[3].iov_len = sizeof(rx[i][j].rxpayhd);
      rx[i][j].rxiov[4].iov_base = &rx[i][j].rxpaybuf;     rx[i][j].rxiov[4].iov_len = sizeof(rx[i][j].rxpaybuf);

      rx[i][j].rxmsg.msg_iov = rx[i][j].rxiov;             rx[i][j].rxmsg.msg_iovlen = 5;
    }
  }


  uint8_t radiotaphd[] = {
        0x00, 0x00, // <-- radiotap version
        0x0d, 0x00, // <- radiotap header length
        0x00, 0x80, 0x08, 0x00, // <-- radiotap present flags:  RADIOTAP_TX_FLAGS + RADIOTAP_MCS
        0x08, 0x00,  // RADIOTAP_F_TX_NOACK
        MCS_KNOWN , MCS_FLAGS, MCS_INDEX // bitmap, flags, mcs_index
  };
  uint8_t ieeehd[] = {
        0x08, 0x01,                         // Frame Control : Data frame from STA to DS
        0x00, 0x00,                         // Duration
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Receiver MAC
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Transmitter MAC
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Destination MAC
        0x10, 0x86                          // Sequence control
  };

 struct tx_t {
    uint8_t txradiotaphd[sizeof(radiotaphd)];
    uint8_t txieeehd[sizeof(ieeehd)];
    uint8_t txllchd[4];
    payhd_t txpayhd;
    uint8_t txpaybuf[PAY_MTU];
    struct iovec txiov[5];
    struct msghdr txmsg;
  } tx[MAXRAWDEV];

  for(uint8_t i = 0; i < MAXRAWDEV; i++) {
    
    memcpy(&tx[i].txradiotaphd, &radiotaphd, sizeof(radiotaphd));
    memcpy(&tx[i].txieeehd, &ieeehd, sizeof(ieeehd));

    tx[i].txiov[0].iov_base = &tx[i].txradiotaphd; tx[i].txiov[0].iov_len = sizeof(tx[i].txradiotaphd);
    tx[i].txiov[1].iov_base = &tx[i].txieeehd;     tx[i].txiov[1].iov_len = sizeof(tx[i].txieeehd);
    tx[i].txiov[2].iov_base = &tx[i].txllchd;      tx[i].txiov[2].iov_len = sizeof(tx[i].txllchd);
    tx[i].txiov[3].iov_base = &tx[i].txpayhd;      tx[i].txiov[3].iov_len = sizeof(tx[i].txpayhd);
    tx[i].txiov[4].iov_base = &tx[i].txpaybuf;     tx[i].txiov[4].iov_len = sizeof(tx[i].txpaybuf);

    tx[i].txmsg.msg_iov = tx[i].txiov;             tx[i].txmsg.msg_iovlen = 5;
  }

  ssize_t len = 0, rawlen = 0;

  bool send_first = false;
  int8_t sync_first = -1, sync_scan = -1;
  uint8_t sync_cpt[nbraws], sync_ack[nbraws];
  for (uint8_t i = 0; i < nbraws; i++) { sync_cpt[i] = 1; sync_ack[i] = 1; }

  for (uint8_t i = 0; i < nbraws; i++) {
    rawdevs[i].cptfreq = (nbraws - i -1) * (rawdevs[i].nbfreqs / nbraws);
    setfreq(sockid, socknl, index[i], rawdevs[i].freqs[rawdevs[i].cptfreq]);
  }

  while (poll(readsets, nbfds, -1) != -1) {

    for (uint8_t cpt = 0; cpt < nbfds; cpt++) {
      if (readsets[cpt].revents & POLLIN) {
        if (cpt == 0) {
          len = read(fds[0], &exptime, sizeof(uint64_t));

          printf("(%d)(%d) cpt(%d)(%d) ack(%d)(%d)  freq (%d)(%d)\n",sync_first, sync_scan, sync_cpt[0], sync_cpt[1], sync_ack[0], sync_ack[1],
            rawdevs[0].freqs[rawdevs[0].cptfreq], rawdevs[1].freqs[rawdevs[1].cptfreq]); fflush(stdout);

          if (sync_scan >= 0) {
            if (sync_ack[sync_scan] > 1) { upfreq(sockid, socknl, sync_scan, index[sync_scan], nbraws, rawdevs ); sync_ack[sync_scan] = 0; }
            if (sync_ack[sync_scan] < 2) sync_ack[sync_scan]++;
	  }

          for (uint8_t i = 0; i < nbraws; i++) {
            if (sync_first < 0) {
	      if (sync_cpt[i] == 0) upfreq(sockid, socknl, i, index[i], nbraws, rawdevs);
	      if (sync_cpt[i] < 5)  sync_cpt[i]++;
	      if (sync_cpt[i] == 5) sync_first = i;
	    }
	  }

          if (sync_first >= 0) send_first = true;

          if ((sync_scan < 0) && (sync_first >= 0)) {
	    for(uint8_t i = 0; i < nbraws; i++) if (i != sync_first) {
	      sync_scan = i;  rawdevs[i].cptfreq = (nbraws - i -1) * (rawdevs[i].nbfreqs / nbraws);
              setfreq(sockid, socknl, index[i], rawdevs[i].freqs[rawdevs[i].cptfreq]);
	    }
	  }

        } else {

          if (readsets[cpt].revents & (POLLERR | POLLNVAL)) { printf("socket error: %s", strerror(errno)); fflush(stdout); }

          if (readsets[cpt].revents & POLLIN) {

            uint8_t raw = cpt - 1; 

	    sync_cpt[raw] = 0;

	    rxrawlog[raw] = 0;

	    uint8_t stlog = rxrawlog[raw];

	    rawlen = 0;
	    int32_t tmp = 0; uint8_t pos;

            while (tmp >= 0) { 
	      pos = rxrawlog[raw];
              memset((uint8_t *)(rx[raw][pos].rxmsg.msg_iov[1].iov_base), 0 , rx[raw][pos].rxmsg.msg_iov[1].iov_len);
              memset((uint8_t *)(rx[raw][pos].rxmsg.msg_iov[3].iov_base), 0 , rx[raw][pos].rxmsg.msg_iov[3].iov_len);
	      tmp = recvmsg(fds[cpt], &rx[raw][pos].rxmsg, MSG_DONTWAIT); rawlen += tmp;
              if ((tmp > 0) && (*(4 + ((uint8_t *)(rx[raw][pos].rxmsg.msg_iov[1].iov_base))) == 0x66)) (rxrawlog[raw]++);
	    }

	    for (pos = stlog; pos < rxrawlog[raw]; pos++) {
              payhd_t *ptrrx = (payhd_t *)(rx[raw][pos].rxmsg.msg_iov[3].iov_base);
              if (ptrrx->droneid == DRONEID) { printf("\n!! This should no happened !!\n\n");fflush(stdout); }
	      else {
                printf("raw (%d)\n",raw); fflush(stdout);
                printf("droneid (%d)\n",ptrrx->droneid); fflush(stdout);
                printf("msglen (%d)\n",ptrrx->msglen); fflush(stdout);
		sync_ack[raw] = 0;
              }
	    }
          }
        }
      }
    }

    if (send_first) {
      ((payhd_t *)(tx[sync_first].txmsg.msg_iov[3].iov_base))->droneid = DRONEID;
      ((payhd_t *)(tx[sync_first].txmsg.msg_iov[3].iov_base))->msglen = 1;
      tx[sync_first].txmsg.msg_iov[4].iov_len = 1;

      rawlen = sendmsg(fds[sync_first + 1], &tx[sync_first].txmsg, MSG_DONTWAIT);

      payhd_t *ptrtx = (payhd_t *)(tx[sync_first].txmsg.msg_iov[3].iov_base);
      printf("sendmsg droneid(%d) msglen(%d) sync_first(%d) rawlen(%ld) freq(%d) \n",
      ptrtx->droneid, ptrtx->msglen, sync_first, rawlen, rawdevs[sync_first].freqs[rawdevs[sync_first].cptfreq]); fflush(stdout);

      send_first = false;
    }
  }
}
