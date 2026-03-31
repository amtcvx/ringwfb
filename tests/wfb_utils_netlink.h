#ifndef WFB_UTILS_NETLINK_H
#define WFB_UTILS_NETLINK_H

#include <stdint.h>
#include <stdbool.h>

#define MAXRAWDEV 4
#define MAXNBBONDS MAXRAWDEV / 2
#define MAXNBFREQS 65

typedef struct {
  uint8_t sockfd;
  char drivername[30];
  char ifname[30];
  int ifindex;
  uint8_t freq;
  uint8_t nbfreqs;
  uint32_t freqs[MAXNBFREQS];
  uint32_t chans[MAXNBFREQS];
  struct rtnl_link *ltap;
} wfb_utils_netlink_raw_t;

typedef struct {
  uint8_t sockfd;
  struct rtnl_link *link;
} wfb_utils_netlink_bond_t;

typedef struct {
  uint8_t sockid;
  struct nl_sock *socknl;
} wfb_utils_netlink_socknl_t;

typedef struct {
  uint8_t nbraws;
  wfb_utils_netlink_socknl_t sockidnl;
  wfb_utils_netlink_raw_t *rawdevs[MAXRAWDEV];
  wfb_utils_netlink_bond_t bonds[MAXNBBONDS];
  struct rtnl_link *link;
} wfb_utils_netlink_init_t;

bool wfb_utils_netlink_setfreq(wfb_utils_netlink_socknl_t *psock, int ifindex, uint32_t freq); 
bool wfb_utils_netlink_init(wfb_utils_netlink_init_t *n);

#endif //  WFB_UTILS_NETLINK_H
