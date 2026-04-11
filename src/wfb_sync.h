#ifndef WFB_SYNC_H
#define WFB_SYNC_H

#include "wfb_main.h"
#include "wfb_netlink.h"
#include "wfb_log.h"

#include <stdint.h>

typedef struct {
  int32_t backfreq;
  uint8_t cptack;
} wfb_sync_link_t;

typedef struct {
  uint8_t cptfree;
  wfb_sync_link_t link[MAXDRONE];
} wfb_sync_com_t;

typedef struct {
  uint64_t exptime;
  uint8_t fd;
  int8_t fdmain;
  int8_t fdback;
  uint16_t len[MAXRAWDEV];
  wfb_sync_com_t com[MAXRAWDEV];
} wfb_sync_init_t;

void wfb_sync_async(uint8_t rawcpt, wfb_sync_init_t *s, wfb_netlink_init_t *n, wfb_log_init_t *l); 
void wfb_sync_periodic(wfb_sync_init_t *s, wfb_netlink_init_t *n, wfb_log_init_t *l); 
void wfb_sync_init(wfb_sync_init_t *s, wfb_netlink_init_t *n); 

#endif //  WFB_SYNC_H
