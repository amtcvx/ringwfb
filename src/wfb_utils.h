#ifndef WFB_UTILS_H
#define WFB_UTILS_H

#include <poll.h>
#include <arpa/inet.h>

typedef enum { WFB_VID, WFB_NB } type_d;

#define EXT_NB 3

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
#if DRONEID == 0
  wfb_utils_dev_t devdrone[MAXDRONE][WFB_NB];
#define READSETS_NB (EXT_NB + (MAXDRONE * WFB_NB))
#else
#define READSETS_NB (EXT_NB + WFB_NB)
#endif
  wfb_utils_dev_t devtab[EXT_NB];
  struct pollfd readsets[READSETS_NB];
  uint8_t readnb;
  wfb_utils_log_t log;
} wfb_utils_init_t;

void wfb_utils_init(wfb_utils_init_t *u);
void wfb_utils_loop(wfb_utils_init_t *u); 

#endif // WFB_UTILS_H
