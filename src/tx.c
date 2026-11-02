#include <net/ip.h>
#include <linux/netfilter_ipv4.h>
#include <net/ieee80211_radiotap.h>


#include "mon_module.h"

/*****************************************************************************/
#define ETH_P_802_11 0x0019  /* Trame IEEE 802.11 */

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

static struct nf_hook_ops *output_hk = NULL;
static uint16_t outdestport = 5600;
static uint64_t curseq = 0;

unsigned int output_proc(void *priv, struct sk_buff *skb, const struct nf_hook_state *state);

/*****************************************************************************/
unsigned int output_proc(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  struct net_device *monitor_dev = ppriv->wifidev[0].ndev;
  if (!monitor_dev) return NF_ACCEPT; 
    
  if (!skb) return NF_ACCEPT;

  pr_info("OUT_A (%d)\n",skb->len);

  struct iphdr *iph = ip_hdr(skb);
  if (!iph || iph->protocol != IPPROTO_UDP) return NF_ACCEPT;

  struct udphdr *uph = udp_hdr(skb);
  if (!uph || ntohs(uph->dest) != outdestport) return NF_ACCEPT;

  uint16_t paylen = skb->len;
  uint8_t headroom = sizeof(radiotaphd) + sizeof(ieeehd) + sizeof(pph_t);

  skb_pull(skb, sizeof(struct iphdr) + sizeof (struct udphdr));

  struct sk_buff *nskb;
  if (!(nskb = skb_copy_expand(skb, headroom, 0, GFP_ATOMIC))) return NF_DROP;

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
  nskb->protocol = htons(ETH_P_802_11);
  dev_queue_xmit(nskb);

  kfree_skb(skb);

  pr_info("OUT_B output_proc msglen (%d) (%d) (%d) (%lu)\n",ntohs(pph->msglen),ntohs(uph->len),ntohs(iph->tot_len),sizeof(pph_t));

  return NF_STOLEN;
}

/*****************************************************************************/
bool output_init(void) {
		
  output_hk = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(output_hk != NULL) {
    output_hk->hook     = (nf_hookfn*)output_proc;
    output_hk->hooknum  = NF_INET_LOCAL_OUT;
    output_hk->pf       = NFPROTO_IPV4;
    output_hk->priority = NF_IP_PRI_FIRST;
    nf_register_net_hook(&init_net, output_hk);
    return true;
  }
  return false;
}

/*****************************************************************************/
void output_exit(void) {

  nf_unregister_net_hook(&init_net, output_hk);
  kfree(output_hk); output_hk = NULL;
}
