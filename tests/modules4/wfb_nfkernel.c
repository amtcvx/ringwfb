/*

gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420  ! x265enc bitrate=2048 ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5600 host=127.0.0.1

gst-launch-1.0 udpsrc port=5600 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false

*/
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/inet.h>

#include <net/dst_metadata.h>

/******************************************************************************/
uint8_t *localname = "lo";
uint8_t *wifiname = "enp5s0";//"wlx3c7c3fa9bdca";
uint16_t outdestport = 5600, indestport = 5700;

static uint32_t ip1,ip2;

typedef struct {
  uint32_t localipint;
  struct net_device *localdev;
  struct net_device *wifidev;
} priv_t;

static priv_t mypriv;

static struct nf_hook_ops *wfb_nfkernel_hook_post = NULL;

/******************************************************************************/
static unsigned int wfb_nfkernel_handler_post(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb != NULL) {
    struct iphdr * iph;
    iph = ip_hdr(skb);
    if(iph && iph->protocol == IPPROTO_UDP) {
      struct udphdr *udph = udp_hdr(skb);
      if ((mypriv.localipint == iph->saddr) && (mypriv.localipint == iph->daddr) &&  (ntohs(udph->dest)== outdestport)) {

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

        int ret = dev_queue_xmit(nskb);
      }
    }
  }
  return NF_ACCEPT;
}

/******************************************************************************/
static rx_handler_result_t handle_frame(struct sk_buff **pskb) {

  struct sk_buff *skb = *pskb;

  if(skb != NULL) {

    if (skb->len > 0) {
      struct iphdr * iph;
      iph = ip_hdr(skb);

      if ((iph->version != 4) || (iph->protocol != IPPROTO_UDP)) return RX_HANDLER_CONSUMED;

      uint8_t *ptr = skb_network_header(skb);
      struct udphdr *udph = (struct udphdr *)(ptr + sizeof(struct iphdr));// udp_hdr(skb);

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


      *pskb = skb;
      skb->dev = mypriv.localdev;
      udph->dest = htons(indestport);

      return RX_HANDLER_ANOTHER;
    }
  }
  return RX_HANDLER_CONSUMED;
}

/******************************************************************************/
static int __init wfb_nfkernel_init(void) {

  mypriv.localdev = dev_get_by_name(&init_net, localname);
  mypriv.wifidev  = dev_get_by_name(&init_net, wifiname);

  in4_pton("127.0.0.1", 9, (u8 *)&(mypriv.localipint), '\n', NULL);

  in4_pton("192.168.3.100", 13, (u8 *)&ip1, '\n', NULL);
  in4_pton("192.168.3.200", 13, (u8 *)&ip2, '\n', NULL);

  dev_set_promiscuity(mypriv.wifidev,1);
  netdev_rx_handler_register(mypriv.wifidev, handle_frame, NULL);

  wfb_nfkernel_hook_post = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(wfb_nfkernel_hook_post != NULL) {
    wfb_nfkernel_hook_post->hook     = (nf_hookfn*)wfb_nfkernel_handler_post;
    wfb_nfkernel_hook_post->hooknum  = NF_INET_POST_ROUTING;
    wfb_nfkernel_hook_post->pf       = NFPROTO_IPV4;
    wfb_nfkernel_hook_post->priority = NF_IP_PRI_FIRST;
    nf_register_net_hook(&init_net, wfb_nfkernel_hook_post);
  }
  return 0;
}

/******************************************************************************/
static void __exit wfb_nfkernel_exit(void) {

  dev_set_promiscuity(mypriv.wifidev,0);
  netdev_rx_handler_unregister(mypriv.wifidev);

  if(wfb_nfkernel_hook_post != NULL) {
    nf_unregister_net_hook(&init_net, wfb_nfkernel_hook_post);
    kfree(wfb_nfkernel_hook_post);
  }
}

/******************************************************************************/
module_init(wfb_nfkernel_init);
module_exit(wfb_nfkernel_exit);

MODULE_LICENSE("GPL");
