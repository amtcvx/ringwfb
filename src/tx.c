#include <net/ip.h>
#include <linux/netfilter_ipv4.h>
#include <net/ieee80211_radiotap.h>


#include "mon_module.h"

/*****************************************************************************/
#define MCS_INDEX  2
#define ETH_P_802_11 0x0019
uint8_t rtap[24];

// Structure de l'en-tête MAC 802.11 QoS Data (26 octets au lieu de 24)
struct ieee80211_qos_header {
    u8 frame_control[2]; // Mis à 0x88, 0x00 (QoS Data)
    u8 duration_id[2];   // Généralement 0x00, 0x00 ou calculé par le chipset
    u8 addr1[6];         // Adresse MAC Destination (BSSID ou Récepteur)
    u8 addr2[6];         // Adresse MAC Source (Émetteur)
    u8 addr3[6];         // Adresse MAC Filtrage (BSSID)
    u8 seq_ctrl[2];      // Numéro de séquence (laissé à 0, rtw88 le remplit)
    u8 qos_ctrl[2];      // Contrôle QoS (ex: 0x07, 0x00 pour priorité Voice TID 7)
} __attribute__((packed));

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

  uint8_t headroom = sizeof(rtap) + sizeof(struct ieee80211_qos_header) 
	             + sizeof(pph_t);
				      
  skb_pull(skb, sizeof(struct iphdr) + sizeof (struct udphdr));

  struct sk_buff *nskb;
  if (!(nskb = skb_copy_expand(skb, headroom, 0, GFP_ATOMIC))) return NF_DROP;

  skb_push(nskb, sizeof(pph_t));
  pph_t *pph = (pph_t *)nskb->data;
  memset((void *)pph, 0, sizeof(pph_t));
  pph->droneid = 0x01;
  pph->daddr = daddr; 
  pph->seq = curseq;
  pph->msglen = htons(paylen);

  curseq++;


/*
  u8 *ptr = skb_push(nskb, sizeof(struct ieee80211_qos_header));

  u8 tid_priority = 0x07;

  // Remplissage du Frame Control : QoS Data
  ptr[0] = 0x88;
  ptr[1] = 0x00;

  // Duration ID
  ptr[2] = 0x00; ptr[3] = 0x00;

  // Adresses MAC (Exemple factice, à remplacer par vos variables)
  memset(&ptr[4], 0xFF, 6);  // Addr1: Broadcast (FF:FF:FF:FF:FF:FF)
  memset(&ptr[10], 0x11, 6); // Addr2: Source (11:11:11:11:11:11)
  memset(&ptr[16], 0x22, 6); // Addr3: BSSID (22:22:22:22:22:22)

  // Sequence Control
  ptr[22] = 0x00; ptr[23] = 0x00;

  // Remplissage du champ QoS Control dynamique (TID 0-7)
  ptr[24] = tid_priority & 0x07; // Assure que le TID ne dépasse pas 3 bits
  ptr[25] = 0x00;                // Flags additionnels (TXOP, etc.) à 0
*/


  u8 mcs_nss_octet = (u8)((MCS_INDEX << 4) | 1);

  // 1. En-tête fixe Radiotap + Flags d'injection (12 octets)
  rtap[0] = 0x00; rtap[1] = 0x00;                         // it_version, it_pad
  rtap[2] = sizeof(rtap); rtap[3] = 0x00;             // it_len (24 octets en Little-Endian)
  rtap[4] = 0x00; rtap[5] = 0x40; rtap[6] = 0x20; rtap[7] = 0x00; // it_present: TX_FLAGS + VHT
  rtap[8] = 0x08; rtap[9] = 0x00;                         // TX_FLAGS: RADIOTAP_F_TX_NOACK
  rtap[10] = 0x00; rtap[11] = 0x00;                       // Internal Padding (Alignement)

  // 2. Bloc VHT dynamique (12 octets) basé sur vos spécifications
  rtap[12] = 0x27; rtap[13] = 0x24;                       // known: Masque VHT (0x2427)
  rtap[14] = 0x02;                                        // flags: Short GI activé
  rtap[15] = 0x00;                                        // bandwidth: Forcer le mode 20 MHz

  // Insertion du MCS calculé dynamiquement
  rtap[16] = mcs_nss_octet;                               // User 0: MCS_NSS
  rtap[17] = 0x00; rtap[18] = 0x00; rtap[19] = 0x00;       // Users 1, 2, 3 à zéro

  rtap[20] = 0x00;                                        // coding: BCC
  rtap[21] = 0x00;                                        // group_id: Single User
  rtap[22] = 0x00; rtap[23] = 0x00;                       // partial_aid

  u8 *ptr = skb_push(nskb, sizeof(rtap));
  memset(ptr, 0, sizeof(rtap));
  memcpy(ptr, rtap, sizeof(rtap));


  skb_shinfo(nskb)->tx_flags = 0;
  nskb->priority = 7;  //  TC_PRIO_CONTROL / Voice (Priorité maximale)

  nskb->ip_summed = CHECKSUM_NONE;
  nskb->dev = ppriv->wifidev[0].ndev;
  nskb->protocol = htons(ETH_P_802_11);

  int queue_index = skb_get_queue_mapping(skb);
  if (dev_direct_xmit(nskb, queue_index) != NETDEV_TX_OK) kfree_skb(nskb); 

  pr_info("OUT_B output_proc msglen (%d) (%d) (%lu)\n",ntohs(pph->msglen),ntohs(iph->tot_len),sizeof(pph_t));

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
