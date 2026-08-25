/*

gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420  ! x265enc bitrate=2048 ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5600 host=127.0.0.1

gst-launch-1.0 udpsrc port=5700 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false

https://github.com/YanayGoor/MyRootkit/blob/master/src/networking.c
*/
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/inet.h>

#include <net/dst_metadata.h>

#include <net/ip.h>

/******************************************************************************/
uint8_t *localname = "lo";
uint8_t *wifiname = "enp5s0";
uint16_t outdestport = 5600, lineport = 5650, indestport = 5700;

typedef struct {
  uint8_t padding;
  uint8_t droneid;
  uint16_t msglen;
  int32_t backfreq;
  uint64_t seq;
  uint16_t dummy;
} __attribute__((packed)) pph_t;

typedef struct {
  uint32_t localipint;
  struct net_device *localdev;
  struct net_device *wifidev;
} priv_t;

static priv_t mypriv;

static struct nf_hook_ops *output_hk = NULL;

static uint64_t curseq = 0;

/******************************************************************************/
static rx_handler_result_t input_proc(struct sk_buff **pskb) {

  if (!*pskb) return RX_HANDLER_CONSUMED;
  struct sk_buff *skb = *pskb;
  if (!skb) return RX_HANDLER_CONSUMED;

//  pr_info("IN input_proc kb->len (%d)\n",skb->len);

  struct iphdr *iph = ip_hdr(skb);

  if ((iph->version != 4) || (iph->protocol != IPPROTO_UDP)) return RX_HANDLER_CONSUMED;

  skb->transport_header = skb->network_header + iph->ihl*4;

  struct udphdr* uph = udp_hdr(skb);

  if ((ntohs(uph->dest) != lineport)) return RX_HANDLER_CONSUMED;

  pph_t *pph = (pph_t *)(skb->data + sizeof(struct iphdr) + sizeof(struct udphdr));
/*
  pr_info("pay  droneid(%u) msglen(%u) backfreq(%u) seq(%llu)\n",
          pph->droneid, ntohs(pph->msglen), pph->backfreq, pph->seq);
*/

  skb_pull(skb, sizeof(struct iphdr) + sizeof(struct udphdr) + sizeof(pph_t));

  skb_push(skb, sizeof(*uph));
  skb_reset_transport_header(skb);
  uph = udp_hdr(skb);
  memset((void *)uph, 0,sizeof(*uph));
  uph->dest = htons(indestport);
  uph->len = pph->msglen;

  skb_push(skb, sizeof(*iph));
  skb_reset_network_header(skb);
  iph = ip_hdr(skb);
  memset((void *)iph, 0,sizeof(*iph));
  iph->version = IPVERSION;
  iph->ihl = sizeof(struct iphdr) / 4;
  iph->protocol = IPPROTO_UDP;
  iph->ttl = 64;
  iph->tot_len = htons(20+ntohs(uph->len));

  iph->check = 0;
  iph->check = ip_fast_csum((uint8_t *)iph, iph->ihl);

  skb->dev = mypriv.localdev;
  skb->pkt_type = PACKET_HOST;

/*
  pr_info("OUT input_proc  tot_len(%hu) ips(%pI4) ipd(%pI4) ulen(%hu) ups(%hu) upd(%hu) \n",
          ntohs(iph->tot_len),
          &(iph->saddr), &(iph->daddr),
          ntohs(uph->len),
          ntohs(uph->source), ntohs(uph->dest));
*/
  return RX_HANDLER_PASS; // RX_HANDLER_ANOTHER duplicated on lo
}

/******************************************************************************/
static unsigned int output_proc(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if(skb != NULL) {

//    pr_info("IN output_proc kb->len (%d)\n",skb->len);

    struct iphdr *iph = ip_hdr(skb);

    if(iph && iph->protocol == IPPROTO_UDP) {

      skb->transport_header = skb->network_header + iph->ihl*4;

      struct udphdr* uph = udp_hdr(skb);

      if ((mypriv.localipint == iph->saddr) && (mypriv.localipint == iph->daddr) &&  (ntohs(uph->dest)== outdestport)) {
/*
        pr_info("output_proc  tot_len(%hu) ips(%pI4) ipd(%pI4) ulen(%hu) ups(%hu) upd(%hu) \n",
          ntohs(iph->tot_len),
          &(iph->saddr), &(iph->daddr),
          ntohs(uph->len),
          ntohs(uph->source), ntohs(uph->dest));
*/
        struct sk_buff *nskb = skb_clone(skb, GFP_KERNEL);
        skb_pull(nskb, sizeof(struct iphdr) + sizeof (struct udphdr));
        pskb_expand_head(nskb, ETH_ALEN + sizeof(pph_t), 0, GFP_KERNEL);

	skb_push(nskb, sizeof(pph_t));
	pph_t *pph = (pph_t *)nskb->data;
        memset((void *)pph, 0, sizeof(pph_t));
        pph->droneid =  0xff;
        pph->seq = curseq;
        pph->msglen = uph->len;

        curseq++;

	skb_push(nskb, sizeof(*uph));
        skb_reset_transport_header(nskb);
	uph = udp_hdr(nskb);
	memset((void *)uph, 0,sizeof(*uph));
        uph->dest = htons(lineport);        
	uph->len = htons(ntohs(pph->msglen) + sizeof(pph_t));

        skb_push(nskb, sizeof(*iph));
        skb_reset_network_header(nskb);
        iph = ip_hdr(nskb);
        memset((void *)iph, 0,sizeof(*iph));
        iph->version = IPVERSION;
        iph->ihl = sizeof(struct iphdr) / 4;
	iph->protocol = IPPROTO_UDP;
	iph->ttl = 64;
        iph->tot_len = htons(20 + ntohs(pph->msglen) + sizeof(pph_t));

	struct ethhdr *neth = (struct ethhdr *)skb_push(nskb, ETH_HLEN);
        skb_reset_mac_header(nskb);
	memset((void *)neth, 0,sizeof(*neth));
	memcpy(neth->h_source, nskb->dev->dev_addr, ETH_ALEN);
        neth->h_proto = htons(ETH_P_IP);

        nskb->protocol = htons(ETH_P_IP);

        nskb->dev = mypriv.wifidev;
	dev_direct_xmit(nskb, 0);
      }
    }
  }
  return NF_ACCEPT;
}

/******************************************************************************/
static int __init wfb_nfkernel_init(void) {

  mypriv.localdev = dev_get_by_name(&init_net, localname);
  mypriv.wifidev  = dev_get_by_name(&init_net, wifiname);

  in4_pton("127.0.0.1", 9, (u8 *)&(mypriv.localipint), '\n', NULL);

  dev_set_promiscuity(mypriv.wifidev,1);
  netdev_rx_handler_register(mypriv.wifidev, input_proc, NULL);

  output_hk = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(output_hk != NULL) {
    output_hk->hook     = (nf_hookfn*)output_proc;
    output_hk->hooknum  = NF_INET_POST_ROUTING;
    output_hk->pf       = NFPROTO_IPV4;
    output_hk->priority = NF_IP_PRI_FIRST;
    nf_register_net_hook(&init_net, output_hk);
  }

  return 0;
}

/******************************************************************************/
static void __exit wfb_nfkernel_exit(void) {

  dev_set_promiscuity(mypriv.wifidev,0);
  netdev_rx_handler_unregister(mypriv.wifidev);

  if(output_hk != NULL) {
    nf_unregister_net_hook(&init_net, output_hk);
    kfree(output_hk);
  }

}

/******************************************************************************/
module_init(wfb_nfkernel_init);
module_exit(wfb_nfkernel_exit);

MODULE_LICENSE("GPL");
