#include <net/udp.h>
#include <linux/unaligned.h>
#include <linux/rtnetlink.h>

#include <linux/ip.h>
#include <linux/udp.h>

#include "mon_module.h"

/******************************************************************************/
#define TARGET_UDP_PORT 5700

/******************************************************************************/
static __sum16 fast_ip_csum(struct iphdr *iph) {
  iph->check = 0;
  return ip_fast_csum((unsigned char *)iph, iph->ihl);
}

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

  pr_info("(%d) pay  droneid(%u) msglen(%u) backfreq(%u) seq(%llu)\n", skb->len,
          pph->droneid, ntohs(pph->msglen), pph->backfreq, pph->seq);

  u16 paylen = ntohs(pph->msglen);

  struct sock *sk = __udp4_lib_lookup(&init_net,
    htonl(INADDR_LOOPBACK), htons(TARGET_UDP_PORT),
    htonl(INADDR_LOOPBACK), htons(TARGET_UDP_PORT),
    0, 0, ppriv->pnet->ipv4.udp_table, NULL);
  if (!sk) return RX_HANDLER_CONSUMED;

  skb_pull(skb, wifilen + sizeof(pph_t));

  struct iphdr *iph = (struct iphdr *)skb->data;
  memset((void *)iph, 0, sizeof(struct iphdr));
  iph->version  = 4;
  iph->tot_len  = htons(sizeof(struct iphdr) + sizeof(struct udphdr) + paylen);
  iph->protocol = IPPROTO_UDP;
  iph->saddr    = htonl(INADDR_LOOPBACK);
  iph->daddr    = htonl(INADDR_LOOPBACK);
  iph->ttl      = 64;
  iph->check    = fast_ip_csum(iph);

  struct udphdr *udph = (struct udphdr *)(skb->data + sizeof(struct iphdr));
  memset((void *)udph, 0, sizeof(struct udphdr));
  udph->source  = htons(TARGET_UDP_PORT);
  udph->dest    = htons(TARGET_UDP_PORT);
  udph->len     = htons(sizeof(struct udphdr) + paylen);
  udph->check   = 0; // Pas de checksum pour UDP local 
		   
  skb_reset_network_header(skb);
  skb_set_transport_header(skb, sizeof(struct iphdr));
  skb->ip_summed = CHECKSUM_UNNECESSARY;

  if (sock_queue_rcv_skb(sk, skb) < 0) { sock_put(sk); return RX_HANDLER_CONSUMED; }

  sock_put(sk);
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
