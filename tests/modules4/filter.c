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

#define WFB_PAY_MTU 1500

/******************************************************************************/
static struct nf_hook_ops *nf_filter_ops = NULL;

/******************************************************************************/
/*
static unsigned short my_csum(unsigned short *buf, int nwords) {
  unsigned long sum;
  for(sum=0; nwords>0; nwords--) sum += *buf++;
  sum = (sum >> 16) + (sum &0xffff);
  sum += (sum >> 16);
  return (unsigned short)(~sum);
}
*/
static unsigned int my_inet_addr(char *str) {
  int a, b, c, d;
  char arr[4];
  sscanf(str, "%d.%d.%d.%d", &a, &b, &c, &d);
  arr[0] = a; arr[1] = b; arr[2] = c; arr[3] = d;
  return *(unsigned int *)arr;
}

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
      if ((wifidev!=NULL)&&(wifidev->flags & IFF_UP)) {

        struct sk_buff* nskb = alloc_skb(WFB_PAY_MTU, GFP_ATOMIC);
        skb_reserve(nskb, sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr));//adjust headroom
										
	uint16_t tx_len = sizeof (struct ethhdr);

        struct ethhdr* neth = (struct ethhdr*)skb_push(nskb, sizeof (struct ethhdr));
        nskb->protocol = neth->h_proto = htons(ETH_P_IP);
        nskb->no_fcs = 1;
        memcpy(neth->h_dest, wifidev->dev_addr, ETH_ALEN);
        memset(neth->h_source, 0, ETH_ALEN);

        tx_len += sizeof(struct iphdr);
        struct iphdr* niph = (struct iphdr*)skb_push(nskb, sizeof(struct iphdr));
	long unsigned int dum = sizeof(struct iphdr);
        niph->ihl = dum;
        niph->version = 4; // IPv4u
        niph->tos = 0;
        niph->frag_off = 0;
        niph->ttl = 64;
        niph->protocol = IPPROTO_UDP;
        niph->check = 0;
        niph->saddr = my_inet_addr("192.168.0.1");
        niph->daddr = my_inet_addr("192.168.0.2");

	tx_len += sizeof(struct udphdr);
        struct udphdr* nuh = (struct udphdr*)skb_push(nskb, sizeof(struct udphdr));
        nuh->source = htons(15934);
        nuh->dest = htons(15904);

	uint8_t *data_buf="HELLO"; uint16_t data_len = 5;
        memcpy(skb_put(nskb,data_len), data_buf, data_len);
	tx_len += data_len;

	nuh->len = htons(tx_len - sizeof(struct ethhdr) - sizeof(struct iphdr));
	niph->tot_len=htons(tx_len - sizeof(struct ethhdr));
/*
        nskb->dev = wifidev;
        nskb->pkt_type = PACKET_OUTGOING;
	uint8_t ret = dev_queue_xmit(nskb);
*/
        kfree_skb(nskb);

        struct udphdr* uh = (struct udphdr *)skb_pull(skb, sizeof(struct udphdr));
        pr_info("len : %hu\n",ntohs(udph->len));
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
