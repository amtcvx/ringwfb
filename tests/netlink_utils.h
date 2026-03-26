#ifndef NETLINK_UTILS_H
#define NETLINK_UTILS_H

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
} netlink_utils_raw_t;

typedef struct {
  uint8_t sockid;
  struct nl_sock *socknl;
} netlink_utils_socknl_t;

typedef struct {
  uint8_t nbraws;
  netlink_utils_socknl_t sockidnl;
  netlink_utils_raw_t *rawdevs[MAXRAWDEV];
} netlink_utils_init_t;

bool netlink_utils_setfreq(netlink_utils_socknl_t *psock, int ifindex, uint32_t freq); 
bool netlink_utils_init(netlink_utils_init_t *n, char *drivername); 

#endif // NETLINK_UTILS_H
