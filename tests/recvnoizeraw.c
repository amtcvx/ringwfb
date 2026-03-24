/*
sudo apt-get install libnl-3-dev libnl-genl-3-dev libnl-route-3-dev

gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c recvnoizeraw.c -o recvnoizeraw.o

cc recvnoizeraw.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o exe_recvnoizeraw

plug & unbplug for each trial
sudo rfkill unblock ...

export DEVICE=wlx3c7c3fa9c1e4

sudo ip link set $DEVICE down
sudo iw dev $DEVICE set type monitor
sudo ip link set $DEVICE up
sudo iw dev $DEVICE set freq 2484
*/

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <poll.h>
#include <arpa/inet.h>

#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <net/ethernet.h> 

#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <netlink/route/link.h>
#include <linux/nl80211.h>


/*****************************************************************************/
void init(char *name, uint16_t *index) {
  struct nl_sock *socknl;
  uint8_t sockid;

  if  (!(socknl = nl_socket_alloc()))  exit(-1);
  nl_socket_set_buffer_size(socknl, 8192, 8192);
  if (genl_connect(socknl)) exit(-1);
  if ((sockid = genl_ctrl_resolve(socknl, "nl80211")) < 0) exit(-1);

  struct nl_sock *sockrt;
  if (!(sockrt = nl_socket_alloc())) exit(-1);
  if (nl_connect(sockrt, NETLINK_ROUTE)) exit(-1);

  struct rtnl_link *ltap;
  struct nl_cache *cache;
  if ((rtnl_link_alloc_cache(sockrt, sockid, &cache)) < 0) exit(-1);
  if (!(ltap = rtnl_link_get_by_name(cache, name))) exit(-1);

  *index = rtnl_link_get_ifindex(ltap);
}

/*****************************************************************************/
int main(int argc, char **argv) {

  if ((argc != 2)) exit(-1);

  uint16_t index;
  init(argv[1],&index);

  uint8_t fd;
  uint16_t protocol = htons(ETH_P_ALL);
  if (-1 == (fd = socket(AF_PACKET,SOCK_RAW,protocol))) exit(-1);

  struct sockaddr_ll sll;
  memset( &sll, 0, sizeof( sll ) );
  sll.sll_family   = AF_PACKET;
  sll.sll_ifindex  = index;
  sll.sll_protocol = protocol;
  if (-1 == bind(fd, (struct sockaddr *)&sll, sizeof(sll))) exit(-1); // BIND must be AFTER wifi setting

  const int32_t sock_qdisc_bypass = 1;
  if (-1 == setsockopt(fd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass))) exit(-1);

  uint8_t dumbuf[1500];
  struct iovec iov_dum = { .iov_base = dumbuf, .iov_len = sizeof(dumbuf)};
  struct iovec iovtab[1] = { iov_dum };
  struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 1 };

  struct pollfd readsets[1] = { { .fd = fd, .events = POLLIN } };

  for(;;) {
    if (0 != poll(readsets, 1, -1)) {
      if (readsets[0].revents == POLLIN) {
        ssize_t rawlen = recvmsg(fd, &msg, MSG_DONTWAIT);
        printf("(%ld)\n",rawlen);
      }
    }
  }
}
