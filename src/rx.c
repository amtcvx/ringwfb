#include <net/udp.h>
#include <linux/unaligned.h>
#include <linux/rtnetlink.h>

#include <linux/ip.h>
#include <linux/udp.h>

#include "mon_module.h"

/******************************************************************************/
#define TARGET_UDP_PORT 5600
static uint16_t indestport = 5700;

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

  u16 udp_len = ntohs(pph->msglen);
  u8 *udp_pay = skb->data + rtlen + 26 + sizeof(pph_t);

  struct sock *sk = __udp4_lib_lookup(&init_net,
    htonl(INADDR_LOOPBACK), htons(TARGET_UDP_PORT),
    htonl(INADDR_LOOPBACK), htons(TARGET_UDP_PORT),
    0, 0, &udp_table, NULL);
  if (!sk) RX_HANDLER_CONSUMED;

  kfree_skb(skb);

  return RX_HANDLER_CONSUMED;
}

/*****************************************************************************/
bool input_init(void) {
  bool ret = false;

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

  for (uint8_t i=0;i<ppriv->wifinb;i++) {
    struct net_device *ndev = ppriv->wifidev[i].ndev;
    dev_set_promiscuity(ndev,0);
    rtnl_lock();
    netdev_rx_handler_unregister(ndev);
    rtnl_unlock();
  }
}
