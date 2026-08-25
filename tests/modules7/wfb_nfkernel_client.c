/*

sudo rfkill

export DEVICE=wlx3c7c3fa9c1e8
sudo ip link set $DEVICE down
sudo iw dev $DEVICE set type monitor
sudo ip link set $DEVICE up
sudo iw dev $DEVICE set freq 5300

gst-launch-1.0 udpsrc port=5700 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false

*/
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/inet.h>

#include <net/dst_metadata.h>

#include <net/ip.h>

/******************************************************************************/
uint8_t *localname = "lo";
uint8_t *devname = "wlxfc349725a317";
uint16_t indestport = 5700;

uint16_t ethport = 5650;

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

/******************************************************************************/
static rx_handler_result_t input_proc(struct sk_buff **pskb) {

  if (!*pskb) return RX_HANDLER_CONSUMED;
  struct sk_buff *skb = *pskb;
  if (!skb) return RX_HANDLER_CONSUMED;

  pr_info("INi1 input_proc kb->len (%d)\n",skb->len);

  struct iphdr *iph;
  struct udphdr* uph;

/*
  uint16_t radiotaplg = (uint16_t)skb->data[2];
  if (!((radiotaplg == 35) || (radiotaplg == 41))) return RX_HANDLER_CONSUMED;
  skb_pull(skb, radiotaplg);
  skb_pull(skb, 24);
  pph_t *pph = (pph_t *)(skb->data);
  pr_info("pay  droneid(%u) msglen(%u) backfreq(%u) seq(%llu)\n",
          pph->droneid, htons(pph->msglen), pph->backfreq, pph->seq);
  if ((pph->droneid != 255) || htons(pph->msglen) > skb->len) return RX_HANDLER_CONSUMED;
  uint16_t ulen = pph->msglen;
  skb_pull(skb, sizeof(pph_t));
*/

  iph = ip_hdr(skb);
  if ((iph->version != 4) || (iph->protocol != IPPROTO_UDP)) return RX_HANDLER_CONSUMED;
  skb->transport_header = skb->network_header + iph->ihl*4;
  uph = udp_hdr(skb);
  if ((ntohs(uph->dest) != ethport)) return RX_HANDLER_CONSUMED;
  pph_t *pph = (pph_t *)(skb->data + sizeof(struct iphdr) + sizeof(struct udphdr));
  uint16_t ulen = pph->msglen;
  pr_info("pay  droneid(%u) msglen(%u) backfreq(%u) seq(%llu)\n",
          pph->droneid, htons(pph->msglen), pph->backfreq, pph->seq);

  skb_pull(skb, sizeof(struct iphdr) + sizeof(struct udphdr) + sizeof(pph_t));



  skb_push(skb, sizeof(*uph));
  skb_reset_transport_header(skb);
  uph = udp_hdr(skb);
  memset((void *)uph, 0,sizeof(*uph));
  uph->dest = htons(indestport);
  uph->len = ulen;

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

  pr_info("OUT input_proc  tot_len(%hu) ips(%pI4) ipd(%pI4) ulen(%hu) ups(%hu) upd(%hu) \n",
          ntohs(iph->tot_len),
          &(iph->saddr), &(iph->daddr),
          ntohs(uph->len),
          ntohs(uph->source), ntohs(uph->dest));

  return RX_HANDLER_PASS; // RX_HANDLER_ANOTHER duplicated on lo
}

/******************************************************************************/
static int __init wfb_nfkernel_init(void) {

  mypriv.localdev = dev_get_by_name(&init_net, localname);
  mypriv.wifidev  = dev_get_by_name(&init_net, devname);

  in4_pton("127.0.0.1", 9, (u8 *)&(mypriv.localipint), '\n', NULL);

  dev_set_promiscuity(mypriv.wifidev,1);
  netdev_rx_handler_register(mypriv.wifidev, input_proc, NULL);

  return 0;
}

/******************************************************************************/
static void __exit wfb_nfkernel_exit(void) {

  dev_set_promiscuity(mypriv.wifidev,0);
  netdev_rx_handler_unregister(mypriv.wifidev);

}

/******************************************************************************/
module_init(wfb_nfkernel_init);
module_exit(wfb_nfkernel_exit);

MODULE_LICENSE("GPL");
