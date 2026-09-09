/*

sudo rfkill

export DEVICE=wlx3c7c3fa9c1e4
sudo ip link set $DEVICE down
sudo iw dev $DEVICE set type monitor
sudo ip link set $DEVICE up
sudo iw dev $DEVICE set freq 5300


gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420  ! x265enc bitrate=2048 ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5600 host=127.0.0.1

*/
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/etherdevice.h>

#include <linux/inet.h>

/******************************************************************************/
uint8_t *localname = "lo";
uint8_t *devname = "wlx3c7c3fa9c1e4";
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

  struct sk_buff *skb = *pskb;
  struct udphdr *uph;
  struct iphdr  *iph;

  if (unlikely(!skb))
    return RX_HANDLER_CONSUMED;

  skb = skb_share_check(skb, GFP_ATOMIC);
  if (unlikely(!skb))
    return RX_HANDLER_CONSUMED;

  *pskb = skb;

  uint16_t radiotap_len = (uint16_t)skb->data[2];
  if (!((radiotap_len == 35) || (radiotap_len == 41))) return RX_HANDLER_CONSUMED;

  uint16_t ieee80211_len = 24; // Standard 3-address data frame header
  uint16_t total_l2_len = radiotap_len + ieee80211_len;

  pph_t *pph = (pph_t *)(skb->data + total_l2_len);
  if ((pph->droneid != 255) || htons(pph->msglen) > skb->len) return RX_HANDLER_CONSUMED;
  pr_info("pay  droneid(%u) msglen(%u) backfreq(%u) seq(%llu)\n",
    pph->droneid, htons(pph->msglen), pph->backfreq, pph->seq);

  if (skb->len < total_l2_len + sizeof(struct iphdr) + sizeof(struct udphdr)) {
    return RX_HANDLER_PASS;
  }

  uint16_t total_pay_len = total_l2_len + sizeof(pph_t);
  skb_trim(skb, skb->len-4);
  skb_pull(skb, total_pay_len);

  skb_reset_network_header(skb);
  iph = ip_hdr(skb);

  if (iph->version != 4 || iph->protocol != IPPROTO_UDP) {
    skb_push(skb, total_l2_len);
    return RX_HANDLER_PASS;
  }

  skb_set_transport_header(skb, iph->ihl * 4);
  uph = udp_hdr(skb);
  uph->dest = htons(indestport);

  skb->pkt_type = PACKET_HOST;
  skb->protocol = htons(ETH_P_IP);

  skb->ip_summed = CHECKSUM_NONE;

  pr_info("OUT input_proc  tot_len(%hu) ips(%pI4) ipd(%pI4) ulen(%hu) ups(%hu) upd(%hu) \n",
          ntohs(iph->tot_len),
          &(iph->saddr), &(iph->daddr),
          ntohs(uph->len),
          ntohs(uph->source), ntohs(uph->dest));

  return RX_HANDLER_PASS;
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
                                                           
