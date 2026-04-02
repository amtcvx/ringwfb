#ifndef TITI_H
#define TITI_H

#include <sys/socket.h>

typedef struct {
  struct msghdr *msg;
} titi_init_t;

void titi_init(titi_init_t *param1);


#endif // TITI_H
