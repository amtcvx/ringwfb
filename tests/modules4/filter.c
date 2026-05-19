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

/*
export DEVICE=wlx3c7c3fa9bdca
sudo ip link set $DEVICE down
sudo iw dev $DEVICE set type monitor
sudo ip link set $DEVICE up
sudo iw dev $DEVICE set channel 3
*/
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>

uint8_t *wifiname = "wlx3c7c3fa9bdca";
uint32_t localhost_IntIP = 16777343; // "127.0.0.1"
uint16_t destport = 5600;

/************************************************************************************************/
#define IEEE80211_RADIOTAP_MCS_HAVE_BW    0x01
#define IEEE80211_RADIOTAP_MCS_HAVE_MCS   0x02
#define IEEE80211_RADIOTAP_MCS_HAVE_GI    0x04

#define IEEE80211_RADIOTAP_MCS_HAVE_STBC  0x20

#define IEEE80211_RADIOTAP_MCS_BW_20    0
#define IEEE80211_RADIOTAP_MCS_SGI      0x04

#define IEEE80211_RADIOTAP_MCS_STBC_1  1
#define IEEE80211_RADIOTAP_MCS_STBC_SHIFT 5

#define MCS_KNOWN (IEEE80211_RADIOTAP_MCS_HAVE_MCS | IEEE80211_RADIOTAP_MCS_HAVE_BW | IEEE80211_RADIOTAP_MCS_HAVE_GI | IEEE80211_RADIOTAP_MCS_HAVE_STBC )

#define MCS_FLAGS  (IEEE80211_RADIOTAP_MCS_BW_20 | IEEE80211_RADIOTAP_MCS_SGI | (IEEE80211_RADIOTAP_MCS_STBC_1 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT))

#define MCS_INDEX  2


uint8_t radiotaphd[] = {
        0x00, 0x00, // <-- radiotap version
        0x0d, 0x00, // <- radiotap header length
        0x00, 0x80, 0x08, 0x00, // <-- radiotap present flags:  RADIOTAP_TX_FLAGS + RADIOTAP_MCS
        0x08, 0x00,  // RADIOTAP_F_TX_NOACK
        MCS_KNOWN , MCS_FLAGS, MCS_INDEX // bitmap, flags, mcs_index
};
uint8_t ieeehd[] = {
        0x08, 0x01,                         // Frame Control : Data frame from STA to DS
        0x00, 0x00,                         // Duration
        0x36, 0x35, 0x34, 0x33, 0x32, 0x31, // Receiver MAC
        0x26, 0x25, 0x24, 0x23, 0x22, 0x21, // Transmitter MAC
        0x16, 0x15, 0x14, 0x13, 0x12, 0x11, // Destination MAC
        0x10, 0x86                          // Sequence control
};

/******************************************************************************/
static struct nf_hook_ops *nf_filter_ops = NULL;

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

//      struct ieee80211_hdr *whdr = NULL;
//      struct ieee80211_radiotap_header *rthdr = NULL;

      struct net_device *wifidev = dev_get_by_name(&init_net,wifiname);
      if (wifidev!=NULL) {
        struct sk_buff *nskb;
        nskb = skb_copy(skb, GFP_ATOMIC);
	//nskb->dev = wifidev;
	nskb->pkt_type = PACKET_OUTGOING;

	int ret = dev_queue_xmit(nskb);
	kfree_skb(nskb);
        pr_info("[%s](%d) len : %hu\n",wifidev->name,ret,ntohs(udph->len));
      }
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
