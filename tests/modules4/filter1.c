/*
https://emmanuelbashorun.medium.com/lifecycle-of-a-packet-through-the-linux-kernel-51301793df5d

https://gist.github.com/jonhoo/7780260?permalink_comment_id=3438455

https://www.codeembedded.com/blog/journey_of_packet_tcp_ip_vol_2/
    
https://olegkutkov.me/2019/10/17/printing-sk_buff-data

https://www.networkacademy.io/ccna/wireless/802-11-frame-format
      
https://bootlin.com/pub/conferences/2025/elce/lothore-80211.pdf     
     
https://android.googlesource.com/kernel/msm/+/android-7.1.1_r0.25/net/mac80211/rx.c      
      
https://github.com/dmytroshytyi/KERNEL-sk_buff-helloWorld/blob/master/lkm.c 

https://github.com/bloomark/kernel-sniffer/tree/master

https://github.com/ptpt52/hstshack/tree/master
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
static struct net_device *wifidev;
struct my_head_t { int a; };
/*

        unsigned char* data;

        char *hello_world = ">>> KERNEL sk_buff Hello World <<< by Dmytro Shytyi";
        int data_len = 51;

        struct sk_buff* skb = alloc_skb(sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr), GFP_ATOMIC);
        skb->dev = wifidev;
        skb->pkt_type = PACKET_OUTGOING;
        skb_reserve(skb, sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr));//adjust headroom
        data = skb_put(skb,data_len);
        memcpy(data, hello_world, data_len);
        struct udphdr* uh = (struct udphdr*)skb_push(skb,sizeof(struct udphdr));
        uh->len = htons(data_len + sizeof(struct udphdr));
        struct iphdr* iph = (struct iphdr*)skb_push(skb,sizeof(struct iphdr));
	long unsigned int dum = sizeof(struct iphdr) / 4;
        iph->ihl = dum;
        iph->version = 4;
        iph->protocol = IPPROTO_UDP;
        struct ethhdr* eth = (struct ethhdr*)skb_push(skb, sizeof (struct ethhdr));//add data to the start of a buffer
        skb->protocol = eth->h_proto = htons(ETH_P_IP);

	struct sk_buff *copy_skb = skb_copy_expand(skb, sizeof(radiotaphd) + sizeof(ieeehd) + sizeof(struct my_head_t), 0, GFP_KERNEL);
	int ret = dev_queue_xmit(copy_skb);

	kfree(skb);

        pr_info("ret(%d)\n",ret);
*/
/******************************************************************************/
struct sk_buff* construct_udp_skb(struct net_device *dev,
    unsigned char * src_mac, unsigned char * dst_mac,
    uint32_t src_ip, uint32_t dst_ip,
    uint32_t src_port, uint32_t dst_port,
    uint32_t ttl, uint32_t tcp_seq,
    unsigned char * usr_data, uint16_t usr_data_len) {

    static struct ethhdr *ethh;
    static struct iphdr *iph;
    struct udphdr * udph;
    static struct sk_buff *skb;
    static uint16_t header_len = 300;
    unsigned char * p_usr_data;
    int udplen;


    skb = alloc_skb(1000, GFP_KERNEL);
    skb_reserve(skb, header_len);
    //-----------------------------------------------
    udph = (struct udphdr*) skb_push(skb, sizeof(struct udphdr));
    iph = (struct iphdr*) skb_push(skb, sizeof(struct iphdr));
    ethh = (struct ethhdr*) skb_push(skb, sizeof(struct ethhdr));

    memset(udph, 0 , sizeof(struct udphdr));
    memset(iph, 0 , sizeof(struct iphdr));

    skb_set_mac_header(skb, 0);
    skb_set_network_header(skb, sizeof(struct ethhdr));
    skb_set_transport_header(skb, sizeof(struct ethhdr) + sizeof(struct iphdr));

    //ETH -------------------------------------------
    memcpy(ethh->h_source, src_mac, 6);
    memcpy(ethh->h_dest, dst_mac, 6);
    ethh->h_proto = htons(ETH_P_IP);
    //IP --------------------------------------------
    iph->ihl = 5;
    iph->version = 4;
    iph->ttl = 128;
    iph->tos = 0;
    iph->protocol = IPPROTO_UDP;
    iph->saddr = src_ip;
    iph->daddr = dst_ip;

    iph->check = ip_fast_csum((u8 *)iph, iph->ihl);
    iph->id = htons(222);
    iph->frag_off = 0;
    iph->tot_len = htons(sizeof(struct iphdr) + sizeof(struct udphdr) + usr_data_len );
    ip_send_check(iph);

    //UDP--------------------------------------------
    udph->source = htons(src_port);
    udph->dest = htons(dst_port);

    skb->dev = dev;
    skb->protocol = IPPROTO_UDP;
    skb->priority = 0;
    skb->pkt_type = PACKET_OUTGOING;

    p_usr_data = skb_put(skb, usr_data_len);
    printk(KERN_INFO "Sending [%i]\n", *(int*)usr_data);
    skb->csum = csum_and_copy_from_user(usr_data, p_usr_data, usr_data_len, 0, &err);


    udplen = sizeof(struct udphdr) + usr_data_len;
    udph->len = htons(udplen);
    udph->check = udp_v4_check(udplen,
                               iph->saddr, iph->daddr,
                               0);


    return skb;
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

      if ((wifidev!=NULL)&&(wifidev->flags & IFF_UP)) {
        static struct tcphdr * tcph;
        static struct iphdr * iph;
        static struct ethhdr * ethh;
        struct sk_buff * skb1;
        iph = ip_hdr(target_skb);
        tcph = tcp_hdr(target_skb);
        ethh = eth_hdr(target_skb);
        int payload = 123456789;
        skb1 = construct_udp_skb(dev,
           dev->dev_addr,  mac,
           iph->saddr, iph->daddr,
           ntohs(tcph->source), dst_port,
           (unsigned char *)&payload, sizeof(int)
        );
        if (dev_queue_xmit(skb1) != NET_XMIT_SUCCESS) {
                printk(KERN_INFO "Sending failed\n");
        }
      }
    }
  }
  return NF_ACCEPT;
}

/******************************************************************************/
static int __init nf_filter_init(void) {

    wifidev = dev_get_by_name(&init_net,"enp5s0");

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
