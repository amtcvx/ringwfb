#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/inet.h>

#include <net/dst_metadata.h>

/******************************************************************************/
uint8_t *localname = "lo";
uint8_t *wifiname = "enp5s0";//"wlx3c7c3fa9bdca";
uint16_t outdestport = 5600, indestport = 5700;

static uint32_t ip1,ip2;

typedef struct {
  uint32_t localipint;
  struct net_device *localdev;
  struct net_device *wifidev;
} priv_t;

static priv_t mypriv;

static struct nf_hook_ops *wfb_nfkernel_hook_post = NULL;

/******************************************************************************/
static unsigned int wfb_nfkernel_handler_post(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb != NULL) {
    struct iphdr * iph;
    iph = ip_hdr(skb);
    if(iph && iph->protocol == IPPROTO_UDP) {
      struct udphdr *udph = udp_hdr(skb);
      if ((mypriv.localipint == iph->saddr) && (mypriv.localipint == iph->daddr) &&  (ntohs(udph->dest)== outdestport)) {

        struct sk_buff * nskb = skb_clone(skb, GFP_KERNEL);

        pskb_expand_head(nskb, sizeof(struct ethhdr), 0, GFP_KERNEL);
        struct iphdr* niph = ip_hdr(nskb);
        niph->saddr = ip1;
        niph->daddr = ip2;
        nskb->dev = mypriv.wifidev;
        nskb->pkt_type = PACKET_OUTGOING;

        struct ethhdr* neth = (struct ethhdr*)skb_push(nskb, sizeof (struct ethhdr));
        nskb->protocol = neth->h_proto = htons(ETH_P_IP);

        char destaddr[ETH_ALEN] = {0x90,0x1b,0xe,0x61,0x39,0x4f};
        memcpy(neth->h_dest, destaddr, ETH_ALEN);
        memcpy(neth->h_source, nskb->dev->dev_addr, ETH_ALEN);

        dev_queue_xmit(nskb);
      }
    }
  }
  return NF_ACCEPT;
}

/******************************************************************************/
static int __init wfb_nfkernel_init(void) {

  mypriv.localdev = dev_get_by_name(&init_net, localname);
  mypriv.wifidev  = dev_get_by_name(&init_net, wifiname);

  in4_pton("127.0.0.1", 9, (u8 *)&(mypriv.localipint), '\n', NULL);

  in4_pton("192.168.3.100", 13, (u8 *)&ip1, '\n', NULL);
  in4_pton("192.168.3.200", 13, (u8 *)&ip2, '\n', NULL);

  wfb_nfkernel_hook_post = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(wfb_nfkernel_hook_post != NULL) {
    wfb_nfkernel_hook_post->hook     = (nf_hookfn*)wfb_nfkernel_handler_post;
    wfb_nfkernel_hook_post->hooknum  = NF_INET_POST_ROUTING;
    wfb_nfkernel_hook_post->pf       = NFPROTO_IPV4;
    wfb_nfkernel_hook_post->priority = NF_IP_PRI_FIRST;
    nf_register_net_hook(&init_net, wfb_nfkernel_hook_post);
  }
  return 0;
}

/******************************************************************************/
static void __exit wfb_nfkernel_exit(void) {

  if(wfb_nfkernel_hook_post != NULL) {
    nf_unregister_net_hook(&init_net, wfb_nfkernel_hook_post);
    kfree(wfb_nfkernel_hook_post);
  }
}

/******************************************************************************/
module_init(wfb_nfkernel_init);
module_exit(wfb_nfkernel_exit);

MODULE_LICENSE("GPL");
