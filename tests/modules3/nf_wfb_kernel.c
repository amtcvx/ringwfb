#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/inet.h>

#include <net/dst_metadata.h>

typedef struct {
  uint8_t droneid;
  uint8_t seq; // uint64_t seq;
  uint8_t msglen; //uint16_t msglen;
  uint8_t backfreq; //int32_t backfreq;
} __attribute__((packed)) payhd_t;

/******************************************************************************/
uint8_t *wifiname = "enp5s0";//"wlx3c7c3fa9bdca";
uint16_t destport = 5600;

static uint32_t ip1,ip2;

/******************************************************************************/
static struct nf_hook_ops *nf_wfb_hook_pre_routing = NULL;
/*
static struct nf_hook_ops *nf_wfb_hook_local_in = NULL;
static struct nf_hook_ops *nf_wfb_hook_local_out = NULL;
*/
static struct nf_hook_ops *nf_wfb_hook_post_routing = NULL;

typedef struct {
  uint32_t localipint;
  struct net_device *localdev;
  struct net_device *wifidev;
} priv_t;

static priv_t mypriv;

/******************************************************************************/
static unsigned int nf_wfb_handler_pre_routing(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb != NULL) {
    struct iphdr * iph;
    iph = ip_hdr(skb);
    if(iph && iph->protocol == IPPROTO_UDP) {
      struct udphdr *udph = udp_hdr(skb);
      if ((mypriv.localipint != iph->saddr) && (mypriv.localipint == iph->daddr) &&  (ntohs(udph->dest)== destport)) {

        pr_info("PRE len(%d)(%d)  %pI4 |  %pI4 | %hu | %hu \n",
		   skb->len, ntohs(udph->len),
		   &(iph->saddr),&(iph->daddr),
		   ntohs(udph->source),ntohs(udph->dest));

        skb_pull(skb, sizeof(struct iphdr));
        skb_pull(skb, sizeof(struct udphdr));
        payhd_t *ptrpay = (payhd_t *)skb->data;
        pr_info("droneid(%d) seq(%d) msglen(i%d) backfreq(%d)\n", 
	  ptrpay->droneid, ptrpay->seq, ptrpay->msglen, ptrpay->backfreq);

        struct rtable *rt = ip_route_output(dev_net(mypriv.localdev), iph->daddr, 0, RT_TOS(iph->tos), 0);
        skb_dst_set(skb, &(rt->dst));
      }
    }
  }
  return NF_ACCEPT;
}

/******************************************************************************/
/*
static unsigned int nf_wfb_handler_local_in(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb != NULL) {
    struct iphdr * iph;
    iph = ip_hdr(skb);
    if(iph && iph->protocol == IPPROTO_UDP) {
      struct udphdr *udph = udp_hdr(skb);
      pr_info("IN len(%d)(%d)  %pI4 |  %pI4 | %hu | %hu \n",
		   skb->len, ntohs(udph->len),
		   &(iph->saddr),&(iph->daddr),
		   ntohs(udph->source),ntohs(udph->dest));
    }
  }
  return NF_ACCEPT;
}

static unsigned int nf_wfb_handler_local_out(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb != NULL) {
    struct iphdr * iph;
    iph = ip_hdr(skb);
    if(iph && iph->protocol == IPPROTO_UDP) {
      struct udphdr *udph = udp_hdr(skb);
      pr_info("OUT len(%d)(%d)  %pI4 |  %pI4 | %hu | %hu \n",
		   skb->len, ntohs(udph->len),
		   &(iph->saddr),&(iph->daddr),
		   ntohs(udph->source),ntohs(udph->dest));
    }
  }
  return NF_ACCEPT;
}
*/
static unsigned int nf_wfb_handler_post_routing(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb != NULL) {
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
/*
        struct udphdr* uh = udp_hdr(skb);
	struct iphdr* iph = ip_hdr(skb);
*/
        struct sk_buff * nskb = skb_clone(skb, GFP_KERNEL);

        pskb_expand_head(nskb, sizeof(struct ethhdr), 0, GFP_KERNEL);
/*
        pskb_expand_head(nskb, sizeof(struct ethhdr) + sizeof(payhd_t), 0, GFP_KERNEL);

	skb_pull_data(nskb, sizeof(payhd_t)+sizeof(struct iphdr)+sizeof(struct udphdr)); // 

	skb_pull_data(nskb,sizeof(struct iphdr)+sizeof(struct udphdr)); //

        payhd_t* pah = (payhd_t*)skb_push(nskb, sizeof(payhd_t));
        pah->droneid = 1;
        pah->seq =2;
        pah->msglen = 3;
        pah->backfreq = 4;

        struct udphdr* nuh = (struct udphdr*)skb_push(nskb, sizeof(struct udphdr));

        nuh->source = htons(59976);
        nuh->dest = htons(5600);

//	nuh->len = uh->len + ntohs(sizeof(payhd_t));
	nuh->len = uh->len; 

        struct iphdr* niph = (struct iphdr*)skb_push(nskb, sizeof(struct iphdr));

        long unsigned int dum = sizeof(struct iphdr) / 4;
        niph->ihl = dum;
        niph->version = 4;
        niph->tos = 0;
        niph->check = 0;
        niph->frag_off = 0;
        niph->ttl = 64; // Set a TTL.
        niph->protocol = IPPROTO_UDP;
*/
        struct udphdr* nuh = udp_hdr(nskb);
	struct iphdr* niph = ip_hdr(nskb);
        niph->saddr = ip1;
        niph->daddr = ip2;
/*
//      niph->tot_len = iph->tot_len + ntohs(sizeof(payhd_t));
        niph->tot_len = iph->tot_len;

	nskb->no_fcs = 0;
*/
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

/******************************************************************************/
static int __init nf_wfb_kernel_init(void) {

  mypriv.localdev = dev_get_by_name(&init_net, "lo");
  mypriv.wifidev = dev_get_by_name(&init_net, wifiname);
  in4_pton("127.0.0.1", 9, (u8 *)&(mypriv.localipint), '\n', NULL);

  in4_pton("192.168.3.100", 13, (u8 *)&ip1, '\n', NULL);
  in4_pton("192.168.3.200", 13, (u8 *)&ip2, '\n', NULL);

  nf_wfb_hook_pre_routing = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(nf_wfb_hook_pre_routing != NULL) {
    nf_wfb_hook_pre_routing->hook = (nf_hookfn*)nf_wfb_handler_pre_routing;
//    nf_tracer_ops->priv = (void *)&mypriv;
//    nf_wfb_hook_pre_routing->dev = wifidev;
    nf_wfb_hook_pre_routing->hooknum = NF_INET_PRE_ROUTING;
    nf_wfb_hook_pre_routing->pf = NFPROTO_IPV4;
    nf_wfb_hook_pre_routing->priority = NF_IP_PRI_FIRST;

    nf_register_net_hook(&init_net, nf_wfb_hook_pre_routing);
  }
/*
  nf_wfb_hook_local_in = (struct nf_hook_ops*)kcalloc(1, sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(nf_wfb_hook_local_in != NULL) {
    nf_wfb_hook_local_in->hook = (nf_hookfn*)nf_wfb_handler_local_in;
//    nf_tracer_ops->priv = (void *)&mypriv;
//    nf_wfb_hook_local_out->dev = wifidev;
    nf_wfb_hook_local_in->hooknum = NF_INET_LOCAL_IN;
    nf_wfb_hook_local_in->pf = NFPROTO_IPV4;
    nf_wfb_hook_local_in->priority = NF_IP_PRI_FIRST;

    nf_register_net_hook(&init_net, nf_wfb_hook_local_in);
  }

  nf_wfb_hook_local_out = (struct nf_hook_ops*)kcalloc(1, sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(nf_wfb_hook_local_out != NULL) {
    nf_wfb_hook_local_out->hook = (nf_hookfn*)nf_wfb_handler_local_out;
//    nf_tracer_ops->priv = (void *)&mypriv;
//    nf_wfb_hook_local_out->dev = wifidev;
    nf_wfb_hook_local_out->hooknum = NF_INET_LOCAL_OUT;
    nf_wfb_hook_local_out->pf = NFPROTO_IPV4;
    nf_wfb_hook_local_out->priority = NF_IP_PRI_FIRST;

    nf_register_net_hook(&init_net, nf_wfb_hook_local_out);
  }
*/
  nf_wfb_hook_post_routing = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(nf_wfb_hook_post_routing != NULL) {
    nf_wfb_hook_post_routing->hook = (nf_hookfn*)nf_wfb_handler_post_routing;
//    nf_tracer_ops->priv = (void *)&mypriv;
//    nf_wfb_hook_pre_routing->dev = wifidev;
    nf_wfb_hook_post_routing->hooknum = NF_INET_POST_ROUTING;
    nf_wfb_hook_post_routing->pf = NFPROTO_IPV4;
    nf_wfb_hook_post_routing->priority = NF_IP_PRI_FIRST;

    nf_register_net_hook(&init_net, nf_wfb_hook_post_routing);
  }

  return 0;
}

/******************************************************************************/
static void __exit nf_wfb_kernel_exit(void) {

  if(nf_wfb_hook_pre_routing != NULL) {
    nf_unregister_net_hook(&init_net, nf_wfb_hook_pre_routing);
    kfree(nf_wfb_hook_pre_routing);
  }
/*
  if(nf_wfb_hook_local_in != NULL) {
    nf_unregister_net_hook(&init_net, nf_wfb_hook_local_in);
    kfree(nf_wfb_hook_local_in);
  }

  if(nf_wfb_hook_local_out != NULL) {
    nf_unregister_net_hook(&init_net, nf_wfb_hook_local_out);
    kfree(nf_wfb_hook_local_out);
  }

  if(nf_wfb_hook_local_out != NULL) {
    nf_unregister_net_hook(&init_net, nf_wfb_hook_local_out);
    kfree(nf_wfb_hook_local_out);
  }

  if(nf_wfb_hook_post_routing != NULL) {
    nf_unregister_net_hook(&init_net, nf_wfb_hook_post_routing);
    kfree(nf_wfb_hook_post_routing);
  }
*/
}

/******************************************************************************/
module_init(nf_wfb_kernel_init);
module_exit(nf_wfb_kernel_exit);

MODULE_LICENSE("GPL");
