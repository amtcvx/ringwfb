#ifndef WFB_LOG_H
#define WFB_LOG_H

#include <stdint.h>
#include <netinet/in.h>

#define BUFSIZE_LOG 5000

typedef struct {
  uint8_t fd;
  uint16_t len;
  char buf[BUFSIZE_LOG];
  struct sockaddr_in addr;
} wfb_log_init_t;

void wfb_log_send(wfb_log_init_t *l);
void wfb_log_init(wfb_log_init_t *l);

#endif //  WFB_LOG_H
