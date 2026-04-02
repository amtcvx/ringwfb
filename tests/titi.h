#ifndef TITI_H
#define TITI_H

#include <sys/socket.h>

//void titi_init(struct msghdr **param);

typedef struct {
  struct msghdr **msg;
} titi_init_t;
void titi_init(titi_init_t **param1, struct msghdr **param2 );


#endif // TITI_H
