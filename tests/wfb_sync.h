#ifndef WFB_SYNC_H
#define WFB_SYNC_H

#include "wfb_main.h"
#include "wfb_netlink.h"

#include <stdint.h>

typedef struct {
  uint64_t exptime;
  uint8_t nbpkt[MAXRAWDEV];
  uint8_t fd;
} wfb_sync_init_t;

void wfb_sync_periodic(wfb_sync_init_t *s, wfb_netlink_init_t *n); 
void wfb_sync_init(wfb_sync_init_t *s, wfb_netlink_init_t *n); 

#endif //  WFB_SYNC_H
