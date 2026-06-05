#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/inet.h>

#include <net/dst_metadata.h>

/******************************************************************************/
uint8_t *wifiname = "eno1";//"wlx3c7c3fa9bdca";
uint16_t destport = 5600;

static uint32_t ip1,ip2;

typedef struct {
  uint32_t localipint;
  struct net_device *localdev;
  struct net_device *wifidev;
} priv_t;

static priv_t mypriv;

/******************************************************************************/
/*
static struct nf_hook_ops *wfb_nfkernel_hook_pre = NULL;
static struct nf_hook_ops *wfb_nfkernel_hook_post = NULL;
static struct nf_hook_ops *wfb_nfkernel_hook_locin = NULL;
static struct nf_hook_ops *wfb_nfkernel_hook_locout = NULL;


static unsigned int wfb_nfkernel_handler_locin(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb != NULL) {
    pr_info("LOC_IN len(%d)\n",skb->len);
  }
  return NF_ACCEPT;
}

static unsigned int wfb_nfkernel_handler_locout(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb != NULL) {
    pr_info("LOC_OUT len(%d)\n",skb->len);
  }
  return NF_ACCEPT;
}

static unsigned int wfb_nfkernel_handler_pre(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb != NULL) {
    pr_info("PRE len(%d)\n",skb->len);
  }
  return NF_ACCEPT;
}

static unsigned int wfb_nfkernel_handler_post(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb != NULL) {
    pr_info("POST len(%d)\n",skb->len);
  }
  return NF_ACCEPT;
}

    struct iphdr * iph;
    iph = ip_hdr(skb);
    if(iph && iph->protocol == IPPROTO_UDP) {
      struct udphdr *udph = udp_hdr(skb);
      if ((mypriv.localipint == iph->saddr) && (mypriv.localipint == iph->daddr) &&  (ntohs(udph->dest)== destport)) {

        uint8_t ch, *p;
        pr_info("In len(%d)\n",skb->len);
        p = skb->data;
        for (uint16_t i = 0; i < skb->len; i++) {
          if (i == sizeof(struct iphdr) + sizeof(struct udphdr)) printk(KERN_CONT "In pay\n");
          ch = p[i];
          printk(KERN_CONT "%02x ", (uint32_t) ch);
        }
        printk(KERN_CONT "\n");

        struct sk_buff * nskb = skb_clone(skb, GFP_KERNEL);

        pskb_expand_head(nskb, sizeof(struct ethhdr), 0, GFP_KERNEL);
        struct udphdr* nuh = udp_hdr(nskb);
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
	
	
	pr_info("OUT len(%d)\n",nskb->len);
        p = nskb->data;
        for (uint16_t i = 0; i < nskb->len; i++) {
          if (i == sizeof(struct iphdr) + sizeof(struct udphdr)) printk(KERN_CONT "In pay\n");
          ch = p[i];
          printk(KERN_CONT "%02x ", (uint32_t) ch);
        }
        printk(KERN_CONT "\n");

        int ret = dev_queue_xmit(nskb);
        pr_info("ret(%d)\n",ret);

        pr_info("POST len(%d)(%d)  %pI4 |  %pI4 | %hu | %hu \n",
		   nskb->len, ntohs(nuh->len),
		   &(niph->saddr),&(niph->daddr),
		   ntohs(nuh->source),ntohs(nuh->dest));

      }
    }
  }
  return NF_ACCEPT;
}
*/
/******************************************************************************/
static rx_handler_result_t handle_frame(struct sk_buff **pskb) {

  struct sk_buff *skb = *pskb;
/*
  struct net_device *whereto_dev;

  skb = skb_share_check(skb, GFP_ATOMIC);
  if (unlikely(!skb)) return RX_HANDLER_CONSUMED;
  *pskb = skb;

  whereto_dev = rcu_dereference(skb->dev->rx_handler_data);

  skb->dev = whereto_dev;

  return RX_HANDLER_ANOTHER;
*/
 
  if(skb != NULL) {
//    pr_info("FRAME len(%d)",skb->len);
    if (skb->len > 0) {
      struct iphdr * iph;
      iph = ip_hdr(skb);

      if ((iph->version != 4) || (iph->protocol != IPPROTO_UDP)) return RX_HANDLER_PASS;

      iph->daddr = mypriv.localipint;

      struct udphdr *udph = udp_hdr(skb);
      pr_info("POST out skb->len(%d) ips(%pI4) ipd(%pI4) ulen(%hu) ups(%hu) upd(%hu) \n", 
          skb->len, 
	  &(iph->saddr), &(iph->daddr),
	  ntohs(udph->len),
	  ntohs(udph->source), ntohs(udph->dest));

      uint8_t ch, *p;
      p = skb->data;
      for (uint16_t i = 0; i < skb->len; i++) {
        if (i == sizeof(struct iphdr) + sizeof(struct udphdr)) printk(KERN_CONT "In pay\n");
        ch = p[i];
        printk(KERN_CONT "%02x ", (uint32_t) ch);
      }
      printk(KERN_CONT "\n");

      skb->pkt_type = PACKET_HOST;
      return RX_HANDLER_PASS;
      //struct rtable *rt = ip_route_output(dev_net(mypriv.localdev), iph->daddr, 0, RT_TOS(iph->tos), 0);
      //skb_dst_set(skb, &(rt->dst));
    }
  }
  return RX_HANDLER_PASS; // RX_HANDLER_CONSUMED; kfree_skb(pkt).
}

/******************************************************************************/
static int __init wfb_nfkernel_init(void) {

  in4_pton("127.0.0.1", 9, (u8 *)&(mypriv.localipint), '\n', NULL);

  mypriv.localdev = dev_get_by_name(&init_net, "lo");
  mypriv.wifidev = dev_get_by_name(&init_net, wifiname);

  dev_set_promiscuity(mypriv.wifidev,1);
  netdev_rx_handler_register(mypriv.wifidev, handle_frame, NULL);

/*
  in4_pton("127.0.0.1", 9, (u8 *)&(mypriv.localipint), '\n', NULL);

  in4_pton("192.168.3.200", 13, (u8 *)&ip1, '\n', NULL);
  in4_pton("192.168.3.100", 13, (u8 *)&ip2, '\n', NULL);

  wfb_nfkernel_hook_pre = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(wfb_nfkernel_hook_pre != NULL) {
    wfb_nfkernel_hook_pre->hook     = (nf_hookfn*)wfb_nfkernel_handler_pre;
    wfb_nfkernel_hook_pre->hooknum  = NF_INET_PRE_ROUTING;
    wfb_nfkernel_hook_pre->pf       = NFPROTO_IPV4;
    wfb_nfkernel_hook_pre->priority = NF_IP_PRI_FIRST + 1;
    nf_register_net_hook(&init_net, wfb_nfkernel_hook_pre);
  }

  wfb_nfkernel_hook_post = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(wfb_nfkernel_hook_post != NULL) {
    wfb_nfkernel_hook_post->hook     = (nf_hookfn*)wfb_nfkernel_handler_post;
    wfb_nfkernel_hook_post->hooknum  = NF_INET_POST_ROUTING;
    wfb_nfkernel_hook_post->pf       = NFPROTO_IPV4;
    wfb_nfkernel_hook_post->priority = NF_IP_PRI_FIRST;
    nf_register_net_hook(&init_net, wfb_nfkernel_hook_post);
  }

  wfb_nfkernel_hook_locin = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(wfb_nfkernel_hook_locin != NULL) {
    wfb_nfkernel_hook_locin->hook     = (nf_hookfn*)wfb_nfkernel_handler_locin;
    wfb_nfkernel_hook_locin->hooknum  = NF_INET_LOCAL_IN;
    wfb_nfkernel_hook_locin->pf       = NFPROTO_IPV4;
    wfb_nfkernel_hook_locin->priority = NF_IP_PRI_FIRST + 2;
    nf_register_net_hook(&init_net, wfb_nfkernel_hook_locin);
  }

  wfb_nfkernel_hook_locout = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(wfb_nfkernel_hook_locout != NULL) {
    wfb_nfkernel_hook_locout->hook     = (nf_hookfn*)wfb_nfkernel_handler_locout;
    wfb_nfkernel_hook_locout->hooknum  = NF_INET_LOCAL_OUT;
    wfb_nfkernel_hook_locout->pf       = NFPROTO_IPV4;
    wfb_nfkernel_hook_locout->priority = NF_IP_PRI_FIRST + 3;
    nf_register_net_hook(&init_net, wfb_nfkernel_hook_locout);
  }
*/
  return 0;
}

/******************************************************************************/
static void __exit wfb_nfkernel_exit(void) {

  dev_set_promiscuity(mypriv.wifidev,0);
  netdev_rx_handler_unregister(mypriv.wifidev);
/*
  if(wfb_nfkernel_hook_pre != NULL) {
    nf_unregister_net_hook(&init_net, wfb_nfkernel_hook_pre);
    kfree(wfb_nfkernel_hook_pre);
  }

  if(wfb_nfkernel_hook_post != NULL) {
    nf_unregister_net_hook(&init_net, wfb_nfkernel_hook_post);
    kfree(wfb_nfkernel_hook_post);
  }

  if(wfb_nfkernel_hook_locin != NULL) {
    nf_unregister_net_hook(&init_net, wfb_nfkernel_hook_locin);
    kfree(wfb_nfkernel_hook_locin);
  }

  if(wfb_nfkernel_hook_locout != NULL) {
    nf_unregister_net_hook(&init_net, wfb_nfkernel_hook_locout);
    kfree(wfb_nfkernel_hook_locout);
  }
*/
}

/******************************************************************************/
module_init(wfb_nfkernel_init);
module_exit(wfb_nfkernel_exit);

MODULE_LICENSE("GPL");
