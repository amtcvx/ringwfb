#ifndef WFB_UTILS_NETLINK_H
#define WFB_UTILS_NETLINK_H

#include <stdint.h>
#include <stdbool.h>

#define MAXRAWDEV 4
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
} wfb_utils_netlink_raw_t;

typedef struct {
  uint8_t sockid;
  struct nl_sock *socknl;
} wfb_utils_netlink_socknl_t;

typedef struct {
  uint8_t nbraws;
  wfb_utils_netlink_socknl_t sockidnl;
  wfb_utils_netlink_raw_t *rawdevs[MAXRAWDEV];
} wfb_utils_netlink_init_t;

bool wfb_utils_netlink_setfreq(wfb_utils_netlink_socknl_t *psock, int ifindex, uint32_t freq); 
bool wfb_utils_netlink_init(wfb_utils_netlink_init_t *n, char *drivername); 

#endif //  WFB_UTILS_NETLINK_H
