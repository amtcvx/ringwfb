#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/string.h>
#include <linux/byteorder/generic.h>

static unsigned char *ip_address = "\xC0\xA8\x00\x01"; 
static char *interface = "lo";                          
unsigned char *port = "\x00\x17";                       

struct sk_buff *sock_buff;                              
static struct nf_hook_ops *nf_filter_ops = NULL;
static struct nf_hook_ops *nf_filter_out_ops = NULL;

static unsigned int nf_filter_handler(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {
//  if(strcmp(in->name,interface) == 0){ return NF_DROP; }     
        
  if(skb==NULL) return NF_ACCEPT;
  struct iphdr * iph;
  iph = ip_hdr(skb);

  if(iph && iph->protocol == IPPROTO_UDP) {
    pr_info("source : %p | dest : %p\n", &(iph->saddr),&(iph->saddr));
  }
  return NF_ACCEPT;
}
//  sock_buff = *skb;
//  if(!sock_buff){ return NF_ACCEPT; }                   
//  if(!(sock_buff->nh.iph)){ return NF_ACCEPT; }              
//  if(sock_buff->nh.iph->saddr == *(unsigned int*)ip_address){ return NF_DROP; }
//
//if(sock_buff->nh.iph->protocol != 17){ return NF_ACCEPT; }                 
//udp_header = (struct udphdr *)(sock_buff->data + (sock_buff->nh.iph->ihl *4)); 
//if((udp_header->dest) == *(unsigned short*)port){ return NF_DROP; }
//return NF_ACCEPT;
/*
  if(skb==NULL) return NF_ACCEPT;
  struct iphdr * iph;
  iph = ip_hdr(skb);
  if(iph && iph->protocol == IPPROTO_TCP) {
    struct tcphdr *tcph = tcp_hdr(skb);
    pr_info("source : %pI4:%hu | dest : %pI4:%hu | seq : %u | ack_seq : %u | window : %hu | csum : 0x%hx | urg_ptr %hu\n", &(iph->saddr),ntohs(tcph->source),&(iph->saddr),ntohs(tcph->dest), ntohl(tcph->seq), ntohl(tcph->ack_seq), ntohs(tcph->window), ntohs(tcph->check), ntohs(tcph->urg_ptr));
  }
  return NF_ACCEPT;
*/

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

module_init(nf_filter_init);
module_exit(nf_filter_exit);

MODULE_LICENSE("GPL");

