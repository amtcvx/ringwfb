#include <net/ieee80211_radiotap.h>
#include <net/ip.h>

#include "mon_module.h"

/*****************************************************************************/
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
        0x88, 0x00,                         // Data (type=2) with QOS (subtype=8)
        0x00, 0x00,
        0x36, 0x35, 0x34, 0x33, 0x32, 0x31, // Receiver MAC
        0x26, 0x25, 0x24, 0x23, 0x22, 0x21, // Transmitter MAC
        0x16, 0x15, 0x14, 0x13, 0x12, 0x11, // Destination MAC
        0x00, 0x00, 0x20, 0x00
};

static uint16_t outdestport = 5600;
static uint64_t curseq = 0;

/*****************************************************************************/
unsigned int output_proc(void *priv,
                                struct sk_buff *skb,
                                const struct nf_hook_state *state) {

  struct net_device *monitor_dev = ppriv->wifidev[0].ndev;
  if (!monitor_dev) return NF_ACCEPT; 
    
  if (!skb) return NF_ACCEPT;

  pr_info("OUT_A (%d)\n",skb->len);

  struct iphdr *iph = ip_hdr(skb);
  if (!iph || iph->protocol != IPPROTO_UDP) return NF_ACCEPT;

  struct udphdr *uph = udp_hdr(skb);
  if (!uph || ntohs(uph->dest) != outdestport) return NF_ACCEPT;

  struct sk_buff *nskb = skb_clone(skb, GFP_KERNEL);
  skb_pull(nskb, sizeof(struct iphdr) + sizeof (struct udphdr));
  uint16_t paylen = nskb->len;

  pskb_expand_head(nskb, sizeof(radiotaphd) + sizeof(ieeehd) + sizeof(pph_t), 0, GFP_KERNEL);

  skb_push(nskb, sizeof(pph_t));
  pph_t *pph = (pph_t *)nskb->data;
  memset((void *)pph, 0, sizeof(pph_t));
  pph->droneid =  0xff;
  pph->seq = curseq;
  pph->msglen = htons(paylen);

  curseq++;

  uint8_t *ptr = skb_push(nskb, sizeof(ieeehd));
  memcpy(nskb->data, ieeehd, sizeof(ieeehd));
  ptr = skb_push(nskb, sizeof(radiotaphd));
  memcpy(nskb->data, radiotaphd, sizeof(radiotaphd));
  nskb->dev = monitor_dev;

  dev_direct_xmit(nskb, 0);

  pr_info("IN output_proc msglen (%d) (%d) (%d) (%lu)\n",ntohs(pph->msglen),ntohs(uph->len),ntohs(iph->tot_len),sizeof(pph_t));

  return NF_STOLEN;
}
