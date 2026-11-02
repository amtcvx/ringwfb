#include <linux/unaligned.h>
#include <linux/rtnetlink.h>

#include <linux/ip.h>
#include <linux/udp.h>

#include "mon_module.h"

/******************************************************************************/
static uint16_t indestport = 5700;
static struct socket *udp_sock = NULL;
static struct sockaddr_in dest_addr;

/******************************************************************************/
static rx_handler_result_t input_proc(struct sk_buff **pskb) {

  struct sk_buff *skb = *pskb;

  if (unlikely(!skb)) return RX_HANDLER_CONSUMED;

//  pr_info("IN_A (%d)\n",skb->len);

  if (unlikely(skb->len < 36)) return RX_HANDLER_PASS;

  u16 rtlen = get_unaligned_le16(skb->data + 2);
  if (unlikely(rtlen + 24 > skb->len)) return RX_HANDLER_PASS;

  if (unlikely(rtlen == 0x0D || rtlen == 0x0E)) return RX_HANDLER_PASS;

  pph_t *pph = (pph_t *)(skb->data + rtlen + 26);

  if ((pph->droneid > 5) || ntohs(pph->msglen) > skb->len) return RX_HANDLER_CONSUMED;

  pr_info("(%d) pay  droneid(%u) msglen(%u) backfreq(%u) seq(%llu)\n", skb->len,
          pph->droneid, ntohs(pph->msglen), pph->backfreq, pph->seq);

//  unsigned int pull_len = 36 + 24 + 8; // Radiotap + 802.11 + LLC/SNAP


//  skb_pull(skb, rtlen + 26 + sizeof(pph_t));
  skb_pull(skb, rtlen);

  int len = skb->len;
  
  struct kvec iov;
  iov.iov_base = skb->data;
  iov.iov_len = len;
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_name = &dest_addr;
  msg.msg_namelen = sizeof(dest_addr);

  kernel_sendmsg(udp_sock, &msg, &iov, 1, len);

  return RX_HANDLER_CONSUMED;
}

/*****************************************************************************/
bool input_init(void) {
  bool ret = false;

  memset(&dest_addr, 0, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); 
  dest_addr.sin_port = htons(indestport);

  for (uint8_t i=0;i<ppriv->wifinb;i++) {
    sock_create_kern(&init_net, PF_INET, SOCK_DGRAM, IPPROTO_UDP, &udp_sock);
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

  for (uint8_t i=0;i<ppriv->wifinb;i++) {
    struct net_device *ndev = ppriv->wifidev[i].ndev;
    dev_set_promiscuity(ndev,0);
    rtnl_lock();
    netdev_rx_handler_unregister(ndev);
    rtnl_unlock();
    sock_release(udp_sock);
  }
}
