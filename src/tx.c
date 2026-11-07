#include <net/ip.h>
#include <linux/netfilter_ipv4.h>

#include "mon_module.h"

/************************************************************************************************/
#include <net/ieee80211_radiotap.h>
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

/*****************************************************************************/
/*
// mcs index from 0 to 7
#define MCS_INDEX  		7

#define RADIOTAP_VHT_LEN  	24
#define IEEE80211_QOS_MAC_LEN 	26
*/
#define ETH_P_802_11 		0x0019

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
//  uint8_t headroom = RADIOTAP_VHT_LEN + IEEE80211_QOS_MAC_LEN + sizeof(pph_t);
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
  u8 *ptr = nskb->data;
  memcpy(ptr, ieeehd, sizeof(ieeehd));

/*
  ptr[0] = 0x88; ptr[1] = 0x00; // frame control Subtype: QoS Data (0x08), Type: Data (0x02) -> 0x88
  ptr[2] = 0x00; ptr[3] = 0x00; // mac duration id
  memset(ptr+4,  0x11, 6);    // Destination : Broadcast
  memset(ptr+10, 0x12, 6);    // Source
  memset(ptr+16, 0x23, 6);    // BSSID
  ptr[22] = 0x00; ptr[23] = 0x00; // Sequence
  ptr[24] = (u8)(7 & 0x07);       // Injection de la priorité TID (0-7) - 7 Voice
  ptr[24] = 0x20;                   // ?
  ptr[25] = 0x00;
*/
  ptr = skb_push(nskb, sizeof(radiotaphd));
  memcpy(nskb->data, radiotaphd, sizeof(radiotaphd));

/*
  ptr = skb_push(nskb, RADIOTAP_VHT_LEN);

  // 1. En-tête fixe Radiotap + Flags d'injection (12 octets)
  ptr[0] = 0x00; ptr[1] = 0x00;                         // it_version, it_pad
  ptr[2] = RADIOTAP_VHT_LEN; ptr[3] = 0x00;             // it_len (24 octets en Little-Endian)
  ptr[4] = 0x00; ptr[5] = 0x40; ptr[6] = 0x20; ptr[7] = 0x00; // it_present: TX_FLAGS + VHT
  ptr[8] = 0x08; ptr[9] = 0x00;                         // TX_FLAGS: RADIOTAP_F_TX_NOACK
  ptr[10] = 0x00; ptr[11] = 0x00;                       // Internal Padding (Alignement)
  // 2. Bloc VHT dynamique (12 octets) 
  ptr[12] = 0x27; ptr[13] = 0x24;                       // known: Masque VHT (0x2427)
  ptr[14] = 0x02;                                       // flags: Short GI activé
  ptr[15] = 0x00;                                       // bandwidth: Forcer le mode 20 MHz
  // Insertion du MCS calculé dynamiquement
  ptr[16] = (u8)((MCS_INDEX << 4) | 1);                 // User 0: MCS_NSS
  ptr[17] = 0x00; ptr[18] = 0x00; ptr[19] = 0x00;       // Users 1, 2, 3 à zéro
  ptr[20] = 0x00;                                       // coding: BCC
  ptr[21] = 0x00;                                       // group_id: Single User
  ptr[22] = 0x00; ptr[23] = 0x00;                       // partial_aid

  skb_reset_mac_header(nskb);
*/

  nskb->dev = ppriv->wifidev[0].ndev;
  nskb->protocol = htons(ETH_P_802_11);

  dev_direct_xmit(nskb, 0);

  pr_info("OUT_B output_proc msglen (%d) (%d) (%lu)\n",ntohs(pph->msglen),ntohs(iph->tot_len),sizeof(pph_t));

  kfree_skb(skb);
  return NF_STOLEN;

/*
  nskb->ip_summed = CHECKSUM_NONE;
  skb_shinfo(nskb)->tx_flags = -1;
  nskb->priority = 7;  //  TC_PRIO_CONTROL / Voice (Priorité maximale)
  int queue_index = skb_get_queue_mapping(skb);
  if (dev_direct_xmit(nskb, queue_index) != NETDEV_TX_OK) kfree_skb(nskb); 
  pr_info("OUT_B output_proc msglen (%d) (%d) (%lu)\n",ntohs(pph->msglen),ntohs(iph->tot_len),sizeof(pph_t));
  kfree_skb(skb);
  return NF_STOLEN;
*/
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
