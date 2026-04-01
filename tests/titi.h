#ifndef TITI_H
#define TITI_H

#include <sys/socket.h>

extern struct iovec iov_radiotaphd_tx;
extern struct iovec iov_ieeehd_tx;
extern struct iovec iov_llchd_tx;
/*
typedef struct {
  struct msghdr *msg;
} titi_init_t;

void titi_init(titi_init_t param);
*/
#endif // TITI_H
