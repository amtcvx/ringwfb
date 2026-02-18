#ifndef WFB_UTILS_H
#define WFB_UTILS_H

#include <poll.h>
#include <arpa/inet.h>

typedef enum { WFB_PRO, WFB_VID, WFB_NB } type_d;

#define EXT_NB 2
#define DEV_NB EXT_NB + WFB_NB

#define PAY_MTU 1400

typedef struct {
  uint8_t id;
  struct sockaddr_in outaddr;
  uint8_t ipstr[15];
} wfb_utils_fd_t;

typedef struct {
  wfb_utils_fd_t fd;
  uint16_t len;
  uint8_t buf[PAY_MTU];
} wfb_utils_log_t;

typedef struct {
  wfb_utils_fd_t fd;
  uint8_t nbelt; 
  uint8_t sockelt;
} wfb_utils_dev_t;

typedef struct {
  wfb_utils_dev_t devtab[DEV_NB];
} wfb_utils_drone_t;

typedef struct {
  wfb_utils_log_t log;
#if DRONEID == 0
  wfb_utils_drone_t dronetab[MAXDRONE];
#else
  wfb_utils_drone_t dronetab[1];
#endif
  struct pollfd readsets[DEV_NB];
  uint8_t readnb;
} wfb_utils_init_t;

void wfb_utils_init(wfb_utils_init_t *u);
void wfb_utils_loop(wfb_utils_init_t *u);

#endif // WFB_UTILS_H
