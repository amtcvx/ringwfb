#include <net/ip.h>
#include <linux/netfilter_ipv4.h>
#include <net/ieee80211_radiotap.h>


#include "mon_module.h"

/*****************************************************************************/
#define ETH_P_802_11 0x0019

struct radiotap_hdr {
    u8  it_version;
    u8  it_pad;
    u16 it_len;
    u32 present;
    u16 tx_flags;
    u8 mcs_known;
    u8 mcs_flags;
    u8 mcs_index;
    u8 final_pad[3];
} __attribute__((packed));

struct ieee80211_data_hdr {
    u16 frame_control;
    u16 duration_id;
    u8  addr1[6];
    u8  addr2[6];
    u8  addr3[6];
    u16 seq_ctrl;
    u16 qos_ctrl;
} __attribute__((packed));
/*
struct llc_snap_hdr {
    u8  dsap;
    u8  ssap;
    u8  ctrl;
    u8  oui[3];
    u16 proto;
} __attribute__((packed));
*/

/*****************************************************************************/

static struct nf_hook_ops *output_hk = NULL;
static uint16_t outdestport = 5600;
static uint64_t curseq = 0;

/*****************************************************************************/
static unsigned int output_proc(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  struct net_device *monitor_dev = ppriv->wifidev[0].ndev;
  if (!monitor_dev) return NF_ACCEPT; 
    
  if (!skb) return NF_ACCEPT;

  pr_info("OUT_A (%d)\n",skb->len);

  struct iphdr *iph = ip_hdr(skb);
  if (!iph) return NF_ACCEPT;

  struct udphdr *uph = udp_hdr(skb);

  if (state->out && strcmp(state->out->name, "tun0") == 0) uph->dest = tunport;
  else if (iph->protocol == IPPROTO_UDP) uph->dest = outdestport;
  else return NF_ACCEPT;

  pr_info("OUT_C (%d)\n",ntohs(uph->dest));

  if (!uph || ntohs(uph->dest) != outdestport) return NF_ACCEPT;

  uint16_t paylen = skb->len;
  uint8_t headroom = sizeof(struct radiotap_hdr) + sizeof(struct ieee80211_data_hdr) 
	             + sizeof(pph_t); // + sizeof(struct llc_snap_hdr)

  skb_pull(skb, sizeof(struct iphdr) + sizeof (struct udphdr));

  struct sk_buff *nskb;
  if (!(nskb = skb_copy_expand(skb, headroom, 0, GFP_ATOMIC))) return NF_DROP;

  skb_push(nskb, sizeof(pph_t));
  pph_t *pph = (pph_t *)nskb->data;
  memset((void *)pph, 0, sizeof(pph_t));
  pph->droneid = 0x01;
  pph->seq = curseq;
  pph->msglen = htons(paylen);

  curseq++;
/*
  skb_push(nskb, sizeof(struct llc_snap_hdr));
  struct llc_snap_hdr *llc = (struct llc_snap_hdr *)nskb->data;
  memset((void *)llc, 0, sizeof(struct llc_snap_hdr));
  llc->proto = htons(ETH_P_IP); 
*/
  skb_push(nskb, sizeof(struct ieee80211_data_hdr));
  struct ieee80211_data_hdr *wfi = (struct ieee80211_data_hdr *)nskb->data;
  memset((void *)wfi, 0, sizeof(struct ieee80211_data_hdr));
  wfi->frame_control = cpu_to_le16(0x0088);
  wfi->qos_ctrl = cpu_to_le16(0x0020);

  skb_push(nskb, sizeof(struct radiotap_hdr));
  struct radiotap_hdr *rtap = (struct radiotap_hdr *)nskb->data;
  memset((void *)rtap, 0, sizeof(struct radiotap_hdr));
  rtap->it_len = cpu_to_le16(sizeof(struct radiotap_hdr));
  rtap->present = cpu_to_le32((1 << 15) | (1 << 19));
  rtap->tx_flags = cpu_to_le16(0x0008);
  rtap->mcs_known = 0x07;
  rtap->mcs_flags = 0x10;
  rtap->mcs_index = 0x07;

  nskb->dev = monitor_dev;
  nskb->protocol = htons(ETH_P_802_11);

  dev_direct_xmit(nskb, 0);
  //dev_queue_xmit(nskb);


  pr_info("OUT_B output_proc msglen (%d) (%d) (%d) (%lu)\n",ntohs(pph->msglen),ntohs(uph->len),ntohs(iph->tot_len),sizeof(pph_t));

  kfree_skb(skb);
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
