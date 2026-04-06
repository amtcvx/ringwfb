#ifndef WFB_NETLINK_H
#define WFB_NETLINK_H

#include "wfb_main.h"

#include <stdint.h>
#include <stdbool.h>

#define MAXNBBONDS MAXRAWDEV / 2
#define MAXNBFREQS 65

typedef struct {
  uint8_t droneid;
  uint64_t seq;
  uint16_t msglen;
  int32_t backfreq;
} __attribute__((packed)) wfb_netlink_payhd_t;


typedef struct {
  struct msghdr *msg_in;
  struct msghdr *msg_out;
} wfb_netlink_msg_t;

typedef struct {
  uint8_t sockfd;
  char drivername[30];
  char ifname[30];
  int ifindex;
  uint8_t cptfreq;
  uint8_t nbfreqs;
  uint32_t freqs[MAXNBFREQS];
  uint32_t chans[MAXNBFREQS];
  struct rtnl_link *ltap;
} wfb_netlink_raw_t;

typedef struct {
  uint8_t sockfd;
  struct rtnl_link *link;
} wfb_netlink_bond_t;

typedef struct {
  uint8_t sockid;
  struct nl_sock *socknl;
} wfb_netlink_socknl_t;

typedef struct {
  uint8_t nbraws;
  wfb_netlink_socknl_t sockidnl;
  wfb_netlink_raw_t *rawdevs[MAXRAWDEV];
  wfb_netlink_bond_t bonds[MAXNBBONDS];
  struct rtnl_link *link;
  wfb_netlink_msg_t msg;
} wfb_netlink_init_t;

bool wfb_netlink_setfreq(wfb_netlink_socknl_t *psock, int ifindex, uint32_t freq); 
bool wfb_netlink_init(wfb_netlink_init_t *n);

#endif //  WFB_NETLINK_H
