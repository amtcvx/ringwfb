#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/inet.h>

uint8_t *wifiname = "enp5s0";//"wlx3c7c3fa9bdca";

static struct nf_hook_ops *nf_tracer_ops = NULL;
static struct nf_hook_ops *nf_tracer_out_ops = NULL;

typedef struct {
  struct in_addr ipint;
} priv_t;

static priv_t mypriv;

static unsigned int nf_tracer_handler(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb==NULL) return NF_ACCEPT;

  struct iphdr * iph;
  iph = ip_hdr(skb);

 if(iph && iph->protocol == IPPROTO_UDP) {
   struct udphdr *udph = udp_hdr(skb);

//   if (iph->daddr == ((priv_t *)priv)->ip) {


//     priv = (void *)&mypriv;

//     pr_info("%pI4 In len(%d)(%d)  %pI4 |  %pI4 | %hu | %hu \n",(priv_t *)priv->ipint,
     pr_info("In len(%d)(%d)  %pI4 |  %pI4 | %hu | %hu \n",
		   skb->len, ntohs(udph->len),
		   &(iph->saddr),&(iph->daddr),
		   ntohs(udph->source),ntohs(udph->dest));
 //  }
 }

  return NF_ACCEPT;
}


static int __init nf_tracer_init(void) {

  struct net_device *wifidev = dev_get_by_name(&init_net,wifiname);

  in4_pton("192.168.3.200", 13, (u8 *)&(mypriv.ipint), '\n', NULL);

  nf_tracer_ops = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(nf_tracer_ops!=NULL) {
    nf_tracer_ops->hook = (nf_hookfn*)nf_tracer_handler;
//    nf_tracer_ops->priv = (void *)&mypriv;
//    nf_tracer_ops->dev = wifidev;
    nf_tracer_ops->hooknum = NF_INET_PRE_ROUTING;
    nf_tracer_ops->pf = NFPROTO_IPV4;
    nf_tracer_ops->priority = NF_IP_PRI_FIRST;
    nf_register_net_hook(&init_net, nf_tracer_ops);
  }

  nf_tracer_out_ops = (struct nf_hook_ops*)kcalloc(1, sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(nf_tracer_out_ops!=NULL) {
    nf_tracer_out_ops->hook = (nf_hookfn*)nf_tracer_handler;
//    nf_tracer_ops->priv = (void *)&mypriv;
//    nf_tracer_ops->dev = wifidev;
    nf_tracer_out_ops->hooknum = NF_INET_LOCAL_OUT;
    nf_tracer_out_ops->pf = NFPROTO_IPV4;
    nf_tracer_out_ops->priority = NF_IP_PRI_FIRST;
    nf_register_net_hook(&init_net, nf_tracer_out_ops);
  }

  return 0;
}

static void __exit nf_tracer_exit(void) {

  if(nf_tracer_ops != NULL) {
    nf_unregister_net_hook(&init_net, nf_tracer_ops);
    kfree(nf_tracer_ops);
  }

  if(nf_tracer_out_ops != NULL) {
    nf_unregister_net_hook(&init_net, nf_tracer_out_ops);
    kfree(nf_tracer_out_ops);
  }

}

module_init(nf_tracer_init);
module_exit(nf_tracer_exit);

MODULE_LICENSE("GPL");
