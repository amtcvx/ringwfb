#include <net/ip.h>
#include <linux/netfilter_ipv4.h>

#include "mon_module.h"

/************************************************************************************************/
// mcs index from 0 to 7
#define MCS_INDEX 	2
#define ETH_P_802_11    0x0019

/*
#include <net/ieee80211_radiotap.h>
#define MCS_KNOWN (IEEE80211_RADIOTAP_MCS_HAVE_MCS | IEEE80211_RADIOTAP_MCS_HAVE_BW | IEEE80211_RADIOTAP_MCS_HAVE_GI | IEEE80211_RADIOTAP_MCS_HAVE_STBC )
#define MCS_FLAGS  (IEEE80211_RADIOTAP_MCS_BW_20 | IEEE80211_RADIOTAP_MCS_SGI | (IEEE80211_RADIOTAP_MCS_STBC_1 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT))
uint8_t radiotaphd[] = {
  0x00, 0x00, // <-- radiotap version
  0x0d, 0x00, // <- radiotap header length
  0x00, 0x80, 0x08, 0x00, // <-- radiotap present flags:  RADIOTAP_TX_FLAGS + RADIOTAP_MCS
  0x08, 0x00,  // RADIOTAP_F_TX_NOACK
  MCS_KNOWN , MCS_FLAGS, MCS_INDEX // bitmap, flags, mcs_index
};
*/
uint8_t radiotaphd[] = {
  // 1. En-tête fixe Radiotap + Flags d'injection (12 octets)
  0x00, 0x00,              // it_version, it_pad
  0x18, 0x00,              // it_len (24 octets en Little-Endian)
  0x00, 0x40, 0x20, 0x00,  // it_present: TX_FLAGS + VHT
  0x08, 0x00,              // TX_FLAGS: RADIOTAP_F_TX_NOACK
  0x00, 0x00,              // Internal Padding (Alignement)
  // 2. Bloc VHT dynamique (12 octets) 
  0x27, 0x24,              // known: Masque VHT (0x2427)
  0x02,                    // flags: Short GI activé
  0x00,                    // bandwidth: Forcer le mode 20 MHz
  // Insertion du MCS calculé dynamiquement
  0x00,                    // User 0: MCS_NSS
  0x00, 0x00, 0x00,        // Users 1, 2, 3 à zéro
  0x00,                    // coding: BCC
  0x00,                    // group_id: Single User
  0x00, 0x00               // partial_aid
};

uint8_t ieeehd[] = {
  0x88, 0x00,               // frame control Subtype: QoS Data (0x08), Type: Data (0x02) -> 0x88
  0x00, 0x00,                         // mac duration id
  0x36, 0x35, 0x34, 0x33, 0x32, 0x31, // Receiver MAC
  0x26, 0x25, 0x24, 0x23, 0x22, 0x21, // Transmitter MAC
  0x16, 0x15, 0x14, 0x13, 0x12, 0x11, // Destination MAC
  0x00, 0x00,                         // Sequence
  0x00, 0x00                     // Injection de la priorité TID (0-7) - 7 Voice
};


/*****************************************************************************/
static struct nf_hook_ops *output_hk = NULL;
static u16 outdestport = 0XE015; // 5600;
static uint64_t curseq = 0;

/*****************************************************************************/
static unsigned int output_proc(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if (!skb) return NF_DROP;

  struct iphdr *iph = ip_hdr(skb);
  struct udphdr *uph = udp_hdr(skb);

  u32 daddr = 0;
  if (iph->protocol != IPPROTO_UDP) {
    if ((iph->daddr != ppriv->ipint) && ((iph->daddr & 0x00FFFFFF) == 0x0000000A)) { daddr = iph->daddr; }
    else return NF_ACCEPT;
  } else if (uph->dest != outdestport) return NF_ACCEPT;

  uint16_t paylen = skb->len;

  skb_pull(skb, sizeof(struct iphdr) + sizeof (struct udphdr));

  struct sk_buff *nskb;
  uint8_t headroom = sizeof(radiotaphd) +  sizeof(ieeehd) + sizeof(pph_t);
  if (!(nskb = skb_copy_expand(skb, headroom, 0, GFP_ATOMIC))) return NF_DROP;

  skb_push(nskb, sizeof(pph_t));
  pph_t *pph = (pph_t *)nskb->data;
  memset((void *)pph, 0, sizeof(pph_t));
  pph->droneid = 0x01;
  pph->daddr = daddr; 
  pph->seq = curseq;
  pph->msglen = htons(paylen);

  curseq++;

  skb_push(nskb, sizeof(ieeehd));
  memcpy(nskb->data, ieeehd, sizeof(ieeehd));
  u8 *ptr = nskb->data;
  ptr[24] = (u8)(7 & 0x07);       // Injection de la priorité TID (0-7) - 7 Voice

  skb_push(nskb, sizeof(radiotaphd));
  memcpy(nskb->data, radiotaphd, sizeof(radiotaphd));
  ptr = nskb->data;
  ptr[16] = (u8)((MCS_INDEX << 4) | 1); // User 0: MCS_NSS

  skb_reset_mac_header(nskb);

  nskb->dev = ppriv->wifidev[0].ndev;
  nskb->protocol = htons(ETH_P_802_11);
  nskb->ip_summed = CHECKSUM_NONE;
  nskb->priority = 7;  //  TC_PRIO_CONTROL / Voice (Priorité maximale)

  skb_shinfo(nskb)->tx_flags = -1;

  int queue_index = skb_get_queue_mapping(skb);
  if (dev_direct_xmit(nskb, queue_index) != NETDEV_TX_OK) kfree_skb(nskb); 
  pr_info("OUT_B output_proc msglen (%d) (%d) (%lu)\n",ntohs(pph->msglen),ntohs(iph->tot_len),sizeof(pph_t));

  kfree_skb(skb);
  return NF_STOLEN; // keep sending

//  return NF_DROP; // client 127.0.0.1:5600, reason: Erreur d’envoi de message : Opération non permise
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
