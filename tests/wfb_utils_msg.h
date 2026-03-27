#ifndef WFB_UTILS_MSG_H
#define WFB_UTILS_MSG_H

#include <stdbool.h>
#include <sys/socket.h>

typedef struct {
  struct msghdr msg_in;
  struct msghdr msg_out;
} wfb_utils_msg_init_t;

void wfb_utils_msg_init(wfb_utils_msg_init_t *u);

#endif // WFB_UTILS_MSG_H
