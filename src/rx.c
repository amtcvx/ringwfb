#include <net/udp.h>
#include <linux/unaligned.h>
#include <linux/rtnetlink.h>

#include <linux/ip.h>
#include <linux/udp.h>

#include "mon_module.h"

/******************************************************************************/
#define TARGET_UDP_PORT 5700

static struct socket *udpsock = NULL;
static struct sockaddr_in gstaddr;


/******************************************************************************/
static rx_handler_result_t input_proc(struct sk_buff **pskb) {

  struct sk_buff *skb = *pskb;

  if (!skb) return RX_HANDLER_CONSUMED;

  u8 *ptr = skb->data;
  u16 rtlen = le16_to_cpup((__le16 *)(ptr + 2));

  if (skb->len < rtlen + 26) return RX_HANDLER_PASS;

  if (ptr[rtlen] != 0x88) return RX_HANDLER_PASS;

  u16 wifilen = rtlen + 26;
  pph_t *pph = (pph_t *)(skb->data + wifilen);

  if (pph->droneid == DRONEID) return RX_HANDLER_CONSUMED;
  if ((pph->droneid > 5) || ntohs(pph->msglen) > skb->len) return RX_HANDLER_CONSUMED;

//  pr_info("(%d) pay  droneid(%u) msglen(%u) backfreq(%u) seq(%llu)\n", skb->len,
//          pph->droneid, ntohs(pph->msglen), pph->backfreq, pph->seq);

  u16 paylen = ntohs(pph->msglen);

  struct kvec iov;
  iov.iov_base = (void *)(skb->data + wifilen + sizeof(pph_t));
  iov.iov_len = paylen;

  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_name = &gstaddr;
  msg.msg_namelen = sizeof(gstaddr);
  msg.msg_flags = MSG_DONTWAIT;

  if (kernel_sendmsg(udpsock, &msg, &iov, 1, paylen)) ppriv->wifidev[0].log.rxbytes += paylen;
  else ppriv->wifidev[0].log.rxpktko ++;

  return RX_HANDLER_PASS;
}

/*****************************************************************************/
bool input_init(void) {
  bool ret = false;

  if (sock_create_kern(ppriv->pnet, PF_INET, SOCK_DGRAM, IPPROTO_UDP, &udpsock) < 0)
    return ret;
  memset(&gstaddr, 0, sizeof(gstaddr));
  gstaddr.sin_family = AF_INET;
  gstaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  gstaddr.sin_port = htons(TARGET_UDP_PORT);

  for (uint8_t i=0;i<ppriv->wifinb;i++) {
    struct net_device *ndev = ppriv->wifidev[i].ndev;
    dev_set_promiscuity(ndev,1);
    rtnl_lock();
    ret = netdev_rx_handler_register(ndev, input_proc, NULL);
    rtnl_unlock();
    ret = true;
  }

  return(ret);
}

/*****************************************************************************/
void input_exit(void) {

  if (udpsock) sock_release(udpsock);

  for (uint8_t i=0;i<ppriv->wifinb;i++) {
    struct net_device *ndev = ppriv->wifidev[i].ndev;
    dev_set_promiscuity(ndev,0);
    rtnl_lock();
    netdev_rx_handler_unregister(ndev);
    rtnl_unlock();
  }
}
