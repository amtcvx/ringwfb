/*
https://emmanuelbashorun.medium.com/lifecycle-of-a-packet-through-the-linux-kernel-51301793df5d

https://gist.github.com/jonhoo/7780260?permalink_comment_id=3438455

https://www.codeembedded.com/blog/journey_of_packet_tcp_ip_vol_2/
    
https://olegkutkov.me/2019/10/17/printing-sk_buff-data

https://www.networkacademy.io/ccna/wireless/802-11-frame-format
      
https://bootlin.com/pub/conferences/2025/elce/lothore-80211.pdf     
     
https://android.googlesource.com/kernel/msm/+/android-7.1.1_r0.25/net/mac80211/rx.c      
      
https://github.com/dmytroshytyi/KERNEL-sk_buff-helloWorld/blob/master/lkm.c 
*/

#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>

uint8_t *wifiname = "wlx3c7c3fa9bdca";
uint32_t localhost_IntIP = 16777343; // "127.0.0.1"
uint16_t destport = 5600;

struct sk_buff *sock_buff;                              
static struct nf_hook_ops *nf_filter_ops = NULL;
static struct net_device *wifidev;

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
      //pr_info("len : %hu\n", ntohs(udph->len));

      struct ieee80211_hdr *whdr = NULL;
      struct ieee80211_radiotap_header *rthdr = NULL;

 //     if (skb_shared(skb) || skb_cloned(skb)) {

        struct sk_buff *nskb;
        nskb = skb_copy(skb, GFP_ATOMIC);
        if (!nskb) return NF_DROP;

	nskb->pkt_type = PACKET_OUTGOING;

	int ret = dev_queue_xmit(nskb);
	kfree_skb(nskb);
        pr_info("(%d) len : %hu\n", ret,ntohs(udph->len));
/*
        if ((skb)->sk) skb_set_owner_w(nskb, (skb)->sk);
        kfree_skb(skb);
        skb = nskb;
*/
    }
  }
  return NF_ACCEPT;
}

/******************************************************************************/
static int __init nf_filter_init(void) {

  wifidev = dev_get_by_name(&init_net,wifiname);

  nf_filter_ops = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(nf_filter_ops!=NULL) {
    nf_filter_ops->hook = (nf_hookfn*)nf_filter_handler;
    nf_filter_ops->hooknum = NF_INET_PRE_ROUTING;
    nf_filter_ops->pf = PF_INET; //NFPROTO_IPV4;
    nf_filter_ops->priority = NF_IP_PRI_FIRST;
    nf_register_net_hook(&init_net, nf_filter_ops);
  }
 return 0;
}

/******************************************************************************/
static void __exit nf_filter_exit(void) {

  if(nf_filter_ops != NULL) {
    nf_unregister_net_hook(&init_net, nf_filter_ops);
    kfree(nf_filter_ops);
  }
}

/******************************************************************************/
module_init(nf_filter_init);
module_exit(nf_filter_exit);
MODULE_LICENSE("GPL");
