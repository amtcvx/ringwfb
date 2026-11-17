/*
gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -c wfb_log.c -o wfb_log.o
*/

#include "wfb_log.h"

#include <sys/socket.h>
#include <stdlib.h>
#include <arpa/inet.h>

/******************************************************************************/
void wfb_log_send(wfb_log_init_t *l) {
 
  sendto(l->fd, l->buf, l->len, 0, (const struct sockaddr *)&l->addr, sizeof(l->addr));
  l->len = 0;
}

/******************************************************************************/
void wfb_log_init(wfb_log_init_t *l) {

#define IP_LOCAL    "127.0.0.1"
#define PORT_LOG    5000

  if (-1 == (l->fd = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  l->addr.sin_family = AF_INET;
  l->addr.sin_port = htons(PORT_LOG);
  l->addr.sin_addr.s_addr = inet_addr(IP_LOCAL);
  l->len = 0;
}
