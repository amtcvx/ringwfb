#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/inet.h>

#include <net/dst_metadata.h>

/******************************************************************************/
uint8_t *localname = "lo";
uint8_t *wifiname = "enp5s0";//"wlx3c7c3fa9bdca";
uint16_t outdestport = 5600, indestport = 5700;

typedef struct {
  uint32_t localipint;
  struct net_device *localdev;
  struct net_device *wifidev;
} priv_t;

static priv_t mypriv;

/******************************************************************************/
static rx_handler_result_t handle_frame(struct sk_buff **pskb) {

  struct sk_buff *skb = *pskb;
  skb = skb_share_check(skb, GFP_ATOMIC);

  if (!skb) return RX_HANDLER_CONSUMED;

  *pskb = skb;

  struct iphdr * iph;
  iph = ip_hdr(skb);

  if ((iph->version != 4) || (iph->protocol != IPPROTO_UDP)) return RX_HANDLER_PASS;

  uint8_t *ptr = skb_network_header(skb);
  struct udphdr *udph = (struct udphdr *)(ptr + sizeof(struct iphdr));// udp_hdr(skb);

  if ((ntohs(udph->dest) != outdestport)) return RX_HANDLER_PASS;

  uint8_t ch, *p;
  p = skb->data;
  for (uint16_t i = 0; i < skb->len; i++) {
    if (i == sizeof(struct iphdr) + sizeof(struct udphdr)) printk(KERN_CONT "In pay\n");
    ch = p[i];
    printk(KERN_CONT "%02x ", (uint32_t) ch);
  }
  printk(KERN_CONT "\n");

  udph->dest = htons(indestport);

  skb->dev = mypriv.localdev;
  skb->pkt_type = PACKET_HOST;

  pr_info("POST out skb->len(%d) ips(%pI4) ipd(%pI4) ulen(%hu) ups(%hu) upd(%hu) \n",
    skb->len,
    &(iph->saddr), &(iph->daddr),
    ntohs(udph->len),
    ntohs(udph->source), ntohs(udph->dest));

  return RX_HANDLER_ANOTHER;
}

/******************************************************************************/
static int __init wfb_nfkernel_init(void) {

  in4_pton("127.0.0.1", 9, (u8 *)&(mypriv.localipint), '\n', NULL);

  mypriv.localdev = dev_get_by_name(&init_net, "lo");
  mypriv.wifidev = dev_get_by_name(&init_net, wifiname);

  dev_set_promiscuity(mypriv.wifidev,1);
  netdev_rx_handler_register(mypriv.wifidev, handle_frame, NULL);

  return 0;
}

/******************************************************************************/
static void __exit wfb_nfkernel_exit(void) {

  dev_set_promiscuity(mypriv.wifidev,0);
  netdev_rx_handler_unregister(mypriv.wifidev);
}

/******************************************************************************/
module_init(wfb_nfkernel_init);
module_exit(wfb_nfkernel_exit);

MODULE_LICENSE("GPL");
