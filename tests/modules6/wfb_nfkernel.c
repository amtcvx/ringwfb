/*

gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420  ! x265enc bitrate=2048 ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5600 host=127.0.0.1

gst-launch-1.0 udpsrc port=5700 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false

*/
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/inet.h>

#include <net/dst_metadata.h>


#include <net/ip.h>
//#include <net/udp.h>

/******************************************************************************/
uint8_t *localname = "lo";
uint8_t *wifiname = "enp5s0";
uint16_t outdestport = 5600, lineport = 5650, indestport = 5700;
/*
typedef struct {
  uint8_t droneid;
  uint8_t seq; //uint64_t seq;
  uint8_t msglen; //uint16_t msglen;
  uint8_t backfreq; //int32_t backfreq;
} __attribute__((packed)) phdr_t;
*/
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

    struct iphdr* iph = (struct iphdr*)(skb->data);

    if(iph && iph->protocol == IPPROTO_UDP) {

      struct udphdr* uph = (struct udphdr*)(skb->data + sizeof(struct iphdr));

      if ((mypriv.localipint == iph->saddr) && (mypriv.localipint == iph->daddr) &&  (ntohs(uph->dest)== outdestport)) {

        struct sk_buff *nskb = skb_clone(skb, GFP_KERNEL);

//        pskb_expand_head(nskb, sizeof(struct ethhdr) + sizeof(phdr_t), 0, GFP_KERNEL);
        pskb_expand_head(nskb, sizeof(struct ethhdr), 0, GFP_KERNEL);

	skb_pull(nskb, sizeof(struct iphdr) + sizeof (struct udphdr));
/*
        phdr_t *npay = (phdr_t *)skb_push(nskb, sizeof (phdr_t));
        npay->droneid = 1;
        npay->seq =2;
        npay->msglen = 3;
        npay->backfreq = 4;
*/
        struct udphdr *nuph = (struct udphdr *)skb_push(nskb, sizeof(struct udphdr));
	memcpy((void *)nuph, (void *)uph, sizeof(struct udphdr));
        nuph->dest = htons(lineport);
//        nuph->len += htons(sizeof(phdr_t));

	struct iphdr *niph = (struct iphdr *)skb_push(nskb, sizeof(struct iphdr));
	memcpy((void *)niph, (void *)iph, sizeof(struct iphdr));
        memset(&niph->saddr, 0, sizeof(iph->saddr));
        memset(&niph->daddr, 0, sizeof(iph->saddr));
//        niph->tot_len += htons(sizeof(phdr_t));

	struct ethhdr *neth = skb_push(nskb, sizeof (struct ethhdr));
        memcpy(neth->h_source, nskb->dev->dev_addr, ETH_ALEN);

        nskb->dev = mypriv.wifidev;
        nskb->pkt_type = PACKET_OUTGOING;
        nskb->protocol = neth->h_proto = htons(ETH_P_IP);

        dev_queue_xmit(nskb);

        pr_info("POST out tot_len(%hu) ips(%pI4) ipd(%pI4) ulen(%hu) ups(%hu) upd(%hu) \n",
          ntohs(niph->tot_len),
          &(niph->saddr), &(niph->daddr),
          ntohs(nuph->len),
          ntohs(nuph->source), ntohs(nuph->dest));
      }
    }
  }
  return NF_ACCEPT;
}

/******************************************************************************/
static rx_handler_result_t handle_frame(struct sk_buff **pskb) {

  if (!*pskb) return RX_HANDLER_CONSUMED;
  struct sk_buff *skb = *pskb;
  if (!skb) return RX_HANDLER_CONSUMED;

  pr_info("IN kb->len (%d)\n",skb->len);

  struct iphdr* iph = (struct iphdr*)(skb->data);

  if ((iph->version != 4) || (iph->protocol != IPPROTO_UDP)) return RX_HANDLER_CONSUMED;

  struct udphdr* uph = (struct udphdr*)(skb->data + sizeof(struct iphdr));

  if ((ntohs(uph->dest) != lineport)) return RX_HANDLER_CONSUMED;
/*
  phdr_t *pay = (phdr_t *)(skb->data + sizeof(struct iphdr) + sizeof(struct udphdr));

  pr_info("pay  droneid(%d) seq(%d) msglen(%d) backfres(%d)\n",
    pay->droneid, pay->seq, pay->msglen, pay->backfreq);
*/
  pr_info("%d %d %d %d %d %d IN  handle_frame  tot_len(%hu) ips(%pI4) ipd(%pI4) ulen(%hu) ups(%hu) upd(%hu) \n",
          skb->ip_summed, skb->csum, skb->csum_start, skb->csum_offset,
          iph->check, uph->check,
          ntohs(iph->tot_len),
          &(iph->saddr), &(iph->daddr),
          ntohs(uph->len),
          ntohs(uph->source), ntohs(uph->dest));

  struct iphdr refiph;
  memcpy((void  *)&refiph, (void *)iph, sizeof(struct iphdr));
  struct udphdr refuph;
  memcpy((void *)&refuph, (void *)uph, sizeof(struct udphdr));

//  skb_pull_rcsum(skb, sizeof(struct iphdr) + sizeof(struct udphdr) + sizeof(phdr_t)); 
//  skb_pull(skb, sizeof(struct iphdr) + sizeof(struct udphdr) + sizeof(phdr_t)); 
  skb_pull(skb, sizeof(struct iphdr) + sizeof(struct udphdr));
//  skb_pull_rcsum(skb, sizeof(struct iphdr) + sizeof(struct udphdr));

  uint8_t *ptr = skb_push(skb, sizeof(struct udphdr));
  refuph.dest = ntohs(indestport);
//  refuph.len -= ntohs(sizeof(phdr_t));

  memcpy(ptr, (void *)&refuph, sizeof(struct udphdr));

  ptr = skb_push(skb, sizeof(struct iphdr));
//  refiph.tot_len -= ntohs(sizeof(phdr_t));

  memcpy(ptr, (void *)&refiph, sizeof(struct iphdr));

  skb->dev = mypriv.localdev;
  skb->pkt_type = PACKET_HOST;

//  skb->ip_summed = CHECKSUM_UNNECESSARY;  

  iph = (struct iphdr*)(skb->data);
  uph = (struct udphdr*)(skb->data + sizeof(struct iphdr));


  uph->check = 0;
//  uph->check = ~csum_tcpudp_magic((iph->saddr), (iph->daddr), uph->len, IPPROTO_UDP, 0);

  iph->ttl = 1;
  iph->check = 0;
  iph->check = ip_fast_csum((uint8_t *)iph, iph->ihl);

  pr_info("%d %d %d %d %d %d OUT  handle_frame  tot_len(%hu) ips(%pI4) ipd(%pI4) ulen(%hu) ups(%hu) upd(%hu) \n",
          skb->ip_summed, skb->csum, skb->csum_start, skb->csum_offset,
          iph->check, uph->check,
          ntohs(iph->tot_len),
          &(iph->saddr), &(iph->daddr),
          ntohs(uph->len),
          ntohs(uph->source), ntohs(uph->dest));

  return RX_HANDLER_PASS; // RX_HANDLER_ANOTHER duplicated on lo
}

/******************************************************************************/
static int __init wfb_nfkernel_init(void) {

  mypriv.localdev = dev_get_by_name(&init_net, localname);
  mypriv.wifidev  = dev_get_by_name(&init_net, wifiname);

  in4_pton("127.0.0.1", 9, (u8 *)&(mypriv.localipint), '\n', NULL);

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
