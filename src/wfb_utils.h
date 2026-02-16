#ifndef WFB_UTILS_H
#define WFB_UTILS_H

#include <poll.h>
#include <arpa/inet.h>

#define MAXDEV 2

typedef struct {
  uint8_t id;
  struct sockaddr_in outaddr;
  uint8_t ipstr[15];
} wfb_utils_fd_t;

typedef struct {
  wfb_utils_fd_t fd;
  uint8_t len;
  uint8_t buf[sizeof(uint8_t)];
} wfb_utils_log_t;

typedef struct {
  struct pollfd readsets[MAXDEV+1];
  wfb_utils_fd_t fd[MAXDEV+1];
  uint8_t readtab[MAXDEV+1];
  uint8_t socktab[MAXDEV+1];
  uint8_t readnb;
  wfb_utils_log_t log;
} wfb_utils_init_t;

void wfb_utils_init(wfb_utils_init_t *u);
void wfb_utils_loop(wfb_utils_init_t *u);

#endif // WFB_UTILS_H
