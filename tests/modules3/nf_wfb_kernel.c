#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/inet.h>

#include <net/dst_metadata.h>

uint8_t *wifiname = "enp5s0";//"wlx3c7c3fa9bdca";

/******************************************************************************/
static struct nf_hook_ops *nf_wfb_hook_pre_routing = NULL;
static struct nf_hook_ops *nf_wfb_hook_local_in = NULL;
/*
static struct nf_hook_ops *nf_wfb_hook_local_out = NULL;
static struct nf_hook_ops *nf_wfb_hook_post_routing = NULL;
i*/
typedef struct {
  struct in_addr localipint;
  struct net_device *localdev;
} priv_t;

static priv_t mypriv;

/******************************************************************************/
static unsigned int nf_wfb_handler_pre_routing(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb != NULL) {
    struct iphdr * iph;
    iph = ip_hdr(skb);
    if(iph && iph->protocol == IPPROTO_UDP) {
      struct udphdr *udph = udp_hdr(skb);
      pr_info("PRE len(%d)(%d)  %pI4 |  %pI4 | %hu | %hu \n",
		   skb->len, ntohs(udph->len),
		   &(iph->saddr),&(iph->daddr),
		   ntohs(udph->source),ntohs(udph->dest));

      struct rtable *rt = ip_route_output(dev_net(mypriv.localdev), iph->daddr, 0, RT_TOS(iph->tos), 0);
      skb_dst_set(skb, &(rt->dst));
    }
  }
  return NF_ACCEPT;
}

/******************************************************************************/
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
/*
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

static unsigned int nf_wfb_handler_post_routing(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb != NULL) {
    struct iphdr * iph;
    iph = ip_hdr(skb);
    if(iph && iph->protocol == IPPROTO_UDP) {
      struct udphdr *udph = udp_hdr(skb);
      pr_info("POST len(%d)(%d)  %pI4 |  %pI4 | %hu | %hu \n",
		   skb->len, ntohs(udph->len),
		   &(iph->saddr),&(iph->daddr),
		   ntohs(udph->source),ntohs(udph->dest));
    }
  }
  return NF_ACCEPT;
}
*/
/******************************************************************************/
static int __init nf_wfb_kernel_init(void) {

  struct net_device *wifidev = dev_get_by_name(&init_net,wifiname);
  mypriv.localdev = dev_get_by_name(&init_net, "lo");
  in4_pton("127.0.0.1", 9, (u8 *)&(mypriv.localipint), '\n', NULL);

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
/*
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
*/
  return 0;
}

/******************************************************************************/
static void __exit nf_wfb_kernel_exit(void) {

  if(nf_wfb_hook_pre_routing != NULL) {
    nf_unregister_net_hook(&init_net, nf_wfb_hook_pre_routing);
    kfree(nf_wfb_hook_pre_routing);
  }

  if(nf_wfb_hook_local_in != NULL) {
    nf_unregister_net_hook(&init_net, nf_wfb_hook_local_in);
    kfree(nf_wfb_hook_local_in);
  }
/*
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
