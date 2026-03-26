#ifndef MSG_UTILS_H
#define MSG_UTILS_H

#include <stdbool.h>
#include <sys/socket.h>

typedef struct {
  struct msghdr msg_in;
  struct msghdr msg_out;
} msg_utils_init_t;

void msg_utils_init(msg_utils_init_t *u);

#endif // MSG_UTILS_H
