/*
gcc -fsanitize=address,undefined  -o exe_multiraw_map  -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 multiraw_mmap.c -lnl-route-3 -lnl-genl-3 -lnl-3

gcc -o exe_multiraw_map  -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 multiraw_mmap.c -lnl-route-3 -lnl-genl-3 -lnl-3
sudo ./exe_multiraw_map

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
#include <signal.h>
#include <sys/mman.h>
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
        0x36, 0x35, 0x34, 0x33, 0x32, 0x31, // Receiver MAC
        0x26, 0x25, 0x24, 0x23, 0x22, 0x21, // Transmitter MAC
        0x16, 0x15, 0x14, 0x13, 0x12, 0x11, // Destination MAC
        0x10, 0x86                          // Sequence control
};
uint8_t spare[4];

typedef struct {
  uint8_t droneid;
  uint64_t seq;
  uint16_t msglen;
  int32_t backfreq;
} __attribute__((packed)) tpayhd;

uint8_t paybuf[PAY_MTU];

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

typedef struct {
  struct tpacket_req3 packet_req[2]; 
  unsigned int cursto[2];
  void *ptrmmap;
} trawmmap;

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

  // sudo tcpdump -i wlx3c7c3fa9bdc6 'not wlan addr1 36:35:34:33:32:31 or not wlan addr2 26:25:24:23:22:21' -dd
  struct sock_filter mac_bytecode[] = {
    { 0x30, 0, 0, 0x00000003 },
    { 0x64, 0, 0, 0x00000008 },
    { 0x7, 0, 0, 0x00000000 },
    { 0x30, 0, 0, 0x00000002 },
    { 0x4c, 0, 0, 0x00000000 },
    { 0x2, 0, 0, 0x00000000 },
    { 0x7, 0, 0, 0x00000000 },
    { 0x40, 0, 0, 0x00000006 },
    { 0x15, 0, 16, 0x34333231 },
    { 0x48, 0, 0, 0x00000004 },
    { 0x15, 0, 14, 0x00003635 },
    { 0x50, 0, 0, 0x00000000 },
    { 0x54, 0, 0, 0x0000000c },
    { 0x15, 0, 6, 0x00000004 },
    { 0x50, 0, 0, 0x00000000 },
    { 0x54, 0, 0, 0x000000f0 },
    { 0x15, 8, 0, 0x000000c0 },
    { 0x50, 0, 0, 0x00000000 },
    { 0x54, 0, 0, 0x000000f0 },
    { 0x15, 5, 0, 0x000000d0 },
    { 0x40, 0, 0, 0x0000000c },
    { 0x15, 0, 3, 0x24232221 },
    { 0x48, 0, 0, 0x0000000a },
    { 0x15, 0, 1, 0x00002625 },
    { 0x6, 0, 0, 0x00000000 },
    { 0x6, 0, 0, 0x00040000 },
  };
  struct sock_fprog mac_program = { .len = (sizeof(mac_bytecode) / sizeof(mac_bytecode[0])), 
	                            .filter = mac_bytecode};
  setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &mac_program, sizeof(mac_program));
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
void  setsock(uint8_t *fd, uint32_t index, trawmmap *rawmmap) {

  if (-1 == (*fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)))) exit(-1);
  int tpver = TPACKET_V3;
  if (-1 == (setsockopt(*fd, SOL_PACKET, PACKET_VERSION,  &tpver, sizeof(tpver)))) exit(-1);
  int on = 1;
  if (-1 == (setsockopt(*fd, SOL_PACKET, PACKET_QDISC_BYPASS, &on, sizeof(on)))) exit(-1);
  if (-1 == setsockopt (*fd, SOL_PACKET, PACKET_RX_RING, &rawmmap->packet_req[0], sizeof(struct tpacket_req3))) exit(-1); // FIRST
  if (-1 == setsockopt (*fd, SOL_PACKET, PACKET_TX_RING, &rawmmap->packet_req[1], sizeof(struct tpacket_req3))) exit(-1); // SECOND
															  
  if (MAP_FAILED == (rawmmap->ptrmmap = mmap(0, 
    2*(rawmmap->packet_req->tp_block_nr * rawmmap->packet_req->tp_block_size), PROT_READ|PROT_WRITE, MAP_SHARED, *fd, 0))) exit(-1);

  struct sockaddr_ll sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sll_family = AF_PACKET;
  sockaddr.sll_protocol = htons(ETH_P_ALL);
  sockaddr.sll_ifindex = index;
  if (-1 == (bind(*fd, (const struct sockaddr*)&sockaddr, sizeof(sockaddr)))) exit(-1);
  drain(*fd);
}

/*****************************************************************************/
uint64_t get_time_ms(void) {

  struct timespec ts;
  int rc = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (rc < 0) { printf("Error getting time: %s", strerror(errno)); fflush(stdout); }
  return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

/******************************************************************************/
#ifndef likely
# define likely(x)              __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
# define unlikely(x)            __builtin_expect(!!(x), 0)
#endif
static sig_atomic_t sigint = 0;
static void sighandler(int num) { sigint = 1; }

/******************************************************************************/
int main(int argc, char **argv) {

  signal(SIGINT, sighandler);

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
  uint8_t nbfds = nbraws;
  struct pollfd readsets[nbfds];
  memset(readsets, 0, sizeof(readsets));
  uint8_t rawfds[nbraws];
  uint32_t index[nbraws];
  rawdev_t rawdevs[nbraws];
  memset(rawdevs, 0, sizeof(rawdevs));

  unsigned int blsz = 1<<12, frsz = 1<<11, blnr = 1;
  unsigned int frnr = (blsz * blnr) / frsz;
  struct tpacket_req3 packet_req[2] = { [0 ... 1] = {
    .tp_block_size = blsz, .tp_frame_size = frsz, .tp_block_nr = blnr, .tp_frame_nr = frnr }};
  unsigned int mmapsize = blsz * blnr;

  bool tosend[nbraws];
  trawmmap rawmmap[nbraws];
  for (uint8_t i = 0; i <  nbraws; i++) {
    setraw(sockid, socknl, sockrt, ifnames[i], &index[i], &rawdevs[i]);
    memset(&rawmmap[i], 0, sizeof(rawmmap[i])); memcpy(&rawmmap[i].packet_req, packet_req, sizeof(rawmmap[i].packet_req));
    setsock( &rawfds[i], index[i], &rawmmap[i] );
    readsets[i].fd = rawfds[i]; readsets[i].events = POLLIN; // no passthrough POLLOUT
    tosend[i] = false;							
  }

  /*---------------------------------------------------------------------*/
  struct tblock_desc {
    uint32_t version;
    uint32_t offset_to_priv;
    struct tpacket_hdr_v1 h1;
  };
  /*---------------------------------------------------------------------*/
  for (uint8_t i = 0; i < nbraws; i++) {
    rawdevs[i].cptfreq = (nbraws - i -1) * (rawdevs[i].nbfreqs / nbraws);
    setfreq(sockid, socknl, index[i], rawdevs[i].freqs[rawdevs[i].cptfreq]);
  }

  struct timespec ts;
  uint64_t curms,  stoms, intms = 1000;
  clock_gettime(CLOCK_MONOTONIC, &ts); stoms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

  while (likely(!sigint)) {
    clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
    poll(readsets, nbfds, stoms > curms ? stoms - curms : 0);
    clock_gettime(CLOCK_MONOTONIC, &ts); curms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

    if (curms >= stoms) { // SYNCHRONOUS
      stoms = curms + intms - ((curms - stoms) % intms);
      struct tpacket_stats_v3 stats;
      socklen_t len = sizeof(stats);

      for (uint8_t cpt=0; cpt<nbraws; cpt++) {
        if (getsockopt(rawfds[cpt], SOL_PACKET, PACKET_STATISTICS, &stats, &len) < 0) exit(-1);
        printf("[%d] rcv %u packets, %u dropped, freeze_q_cnt: %u\n",
          cpt, stats.tp_packets, stats.tp_drops, stats.tp_freeze_q_cnt);
      }

      for (uint8_t cpt=0; cpt<nbraws; cpt++) {
	tosend[cpt] = false;
        struct tpacket3_hdr *tx_header = 
	  (struct tpacket3_hdr *)((uint8_t *)rawmmap[cpt].ptrmmap + mmapsize + (rawmmap[cpt].cursto[1] * frsz));
	if (tx_header->tp_status == TP_STATUS_AVAILABLE) {
          uint8_t pos = 0, offset = 0;
	  uint8_t *tx_data = (uint8_t *)tx_header + TPACKET_ALIGN(sizeof(struct tpacket3_hdr));
          offset = sizeof(radiotaphd);    memcpy(tx_data + pos, radiotaphd, offset); pos += offset;
          offset = sizeof(ieeehd);        memcpy(tx_data + pos, ieeehd,     offset); pos += offset;
          offset = sizeof(spare);         memcpy(tx_data + pos, spare,      offset); pos += offset;
  	  uint16_t paylen = 1500;
  	  tpayhd payhd = { .droneid = DRONEID, .seq = 0, .msglen = paylen, .backfreq = 0 };
          offset = sizeof(tpayhd); memcpy(tx_data + pos, (uint8_t *)&payhd, offset); pos += offset;
  	  memset(tx_data + pos, 1, paylen);
          tx_header->tp_len = pos + paylen;
          tx_header->tp_next_offset = 0;
          tx_header->tp_status = TP_STATUS_SEND_REQUEST;
          rawmmap[cpt].cursto[1] = (rawmmap[cpt].cursto[1] + 1) % frnr;
	  tosend[cpt] = true;
        }
      }
    }

    for (uint8_t cpt=0; cpt<nbraws; cpt++) {
      if (readsets[cpt].revents & POLLIN) {

        struct tpacket3_hdr *rx_header = (struct tpacket3_hdr *)((uint8_t *)rawmmap[cpt].ptrmmap + (rawmmap[cpt].cursto[0] * blsz));
        struct tblock_desc *rx_pbd = (struct tblock_desc *) rx_header;
        if ((rx_header->tp_status & TP_STATUS_USER) == 0) { 
          rx_pbd->h1.block_status = TP_STATUS_KERNEL;
          rawmmap[cpt].cursto[0] = (rawmmap[cpt].cursto[0] + 1) % blnr;
          int num_pkts = rx_pbd->h1.num_pkts, i;
          struct tpacket3_hdr *ppd = (struct tpacket3_hdr *) ((uint8_t *)rx_pbd + rx_pbd->h1.offset_to_first_pkt);

	  // misaligned address 0x7a0b5cd6a3ba for type 'struct tpacket3_hdr', which requires 4 byte alignment
          //#define my_TPACKET_ALIGN(x) (((x)+(uint64_t)(TPACKET_ALIGNMENT-1))&~((uint64_t)(TPACKET_ALIGNMENT-1)))
          //struct tpacket3_hdr *ppda = ((void*) ppd) + my_TPACKET_ALIGN(sizeof(struct tpacket3_hdr));
          struct tpacket3_hdr *ppda = ((void*) ppd);

          for (i = 0; i < num_pkts; ++i) {
//  	    printf("%d << recv (%d)\n",cpt,ppda->tp_snaplen);
            ppd = (struct tpacket3_hdr *) ((uint8_t *)ppda + ppda->tp_next_offset);
          }
        }
      }
    }

    for (uint8_t cpt=0; cpt<nbraws; cpt++) {
      if (tosend[cpt]) {
        tosend[cpt] = false;
        ssize_t ec_send = sendto( rawfds[cpt], NULL, 0, MSG_DONTWAIT, NULL, sizeof(struct sockaddr_ll) );
 //       printf("%d >> send (%ld)\n",cpt,ec_send);
      }
    }
  }

  for (uint8_t i=0; i<nbraws; i++) {
    struct tpacket_stats_v3 stats;
    socklen_t len = sizeof(stats);
    if (getsockopt(rawfds[i], SOL_PACKET, PACKET_STATISTICS, &stats, &len) < 0) exit(-1);
    printf("\nReceived %u packets, %u dropped, freeze_q_cnt: %u\n",
    stats.tp_packets, stats.tp_drops, stats.tp_freeze_q_cnt);

    munmap( rawmmap[i].ptrmmap, 2 * mmapsize);
    close(rawfds[i]);
  }
  return(0);
}
