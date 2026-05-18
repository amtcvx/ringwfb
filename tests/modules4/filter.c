#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/string.h>
#include <linux/byteorder/generic.h>

uint32_t localhost_IntIP = 16777343; // "127.0.0.1"
uint16_t destport = 5600;

struct sk_buff *sock_buff;                              
static struct nf_hook_ops *nf_filter_ops = NULL;
static struct nf_hook_ops *nf_filter_out_ops = NULL;

/******************************************************************************/
static unsigned int nf_filter_handler(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb==NULL) return NF_ACCEPT;
  struct iphdr * iph;
  iph = ip_hdr(skb);
  if(iph && iph->protocol == IPPROTO_UDP) {
    //pr_info("source : %pI4 | dest : %pI4 | %d  ", &(iph->saddr),&(iph->daddr),iph->saddr);
    struct udphdr *udph = udp_hdr(skb); 
    //pr_info("source port : %hu | dest port : %hu\n", ntohs(udph->source),ntohs(udph->dest));
    if ((localhost_IntIP == iph->saddr) && (localhost_IntIP == iph->daddr) &&  (ntohs(udph->dest)== destport)) {
      pr_info("len : %hu\n", ntohs(udph->len));
    }
  }
  return NF_ACCEPT;
}

/******************************************************************************/
static int __init nf_filter_init(void) {

  nf_filter_ops = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(nf_filter_ops!=NULL) {
    nf_filter_ops->hook = (nf_hookfn*)nf_filter_handler;
    nf_filter_ops->hooknum = NF_INET_PRE_ROUTING;
    nf_filter_ops->pf = PF_INET; //NFPROTO_IPV4;
    nf_filter_ops->priority = NF_IP_PRI_FIRST;
    nf_register_net_hook(&init_net, nf_filter_ops);
  }
  nf_filter_out_ops = (struct nf_hook_ops*)kcalloc(1, sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(nf_filter_out_ops!=NULL) {
    nf_filter_out_ops->hook = (nf_hookfn*)nf_filter_handler;
    nf_filter_out_ops->hooknum = NF_INET_LOCAL_OUT;
    nf_filter_out_ops->pf = PF_INET; // NFPROTO_IPV4;
    nf_filter_out_ops->priority = NF_IP_PRI_FIRST;
    nf_register_net_hook(&init_net, nf_filter_out_ops);
 }
 return 0;
}

/******************************************************************************/
static void __exit nf_filter_exit(void) {

  if(nf_filter_ops != NULL) {
    nf_unregister_net_hook(&init_net, nf_filter_ops);
    kfree(nf_filter_ops);
  }

  if(nf_filter_out_ops != NULL) {
    nf_unregister_net_hook(&init_net, nf_filter_out_ops);
    kfree(nf_filter_out_ops);
  }
}

/******************************************************************************/
module_init(nf_filter_init);
module_exit(nf_filter_exit);
MODULE_LICENSE("GPL");
