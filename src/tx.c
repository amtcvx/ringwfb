#include <net/ip.h>
#include <linux/netfilter_ipv4.h>

#include "mon_module.h"

/************************************************************************************************/
#define ETH_P_802_11    0x0019
#define TXQUEUE		3

uint8_t radiotaphd[] = {
  0x00, 0x00,              // it_version, it_pad
  0x0e, 0x00,              // it_len (14 octets en Little-Endian)
  0x06, 0x04, 0x00, 0x00,  // it_present: : Flags + Rate + MCS
  0x10,                    // radioTap No ACK
  0x0c,                    // 6 Mbps fallback
  0x07,                    // mcs known
  0x04,                    // Short GI + 20 MHz
  0x01,                    // MCS 1 -> 14.4 Mbps
  0x00                     // padding
};
uint8_t ieeehd[] = {
  0x88, 0x00,              // frame control Subtype: QoS Data (0x08), Type: Data (0x02) -> 0x88
  0x00, 0x00,              // mac duration id
  0x36, 0x35, 0x34, 0x33, 0x32, 0x31, // Receiver MAC
  0x26, 0x25, 0x24, 0x23, 0x22, 0x21, // Transmitter MAC
  0x16, 0x15, 0x14, 0x13, 0x12, 0x11, // Destination MAC
  0x00, 0x00,              // Sequence
  0x27, 0x00               // Injection de la priorité TID (0-7) - 7 Voice
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

  struct netdev_queue *txq = netdev_get_tx_queue(ppriv->wifidev[0].ndev, TXQUEUE); //  AC_VO / TC_PRIO_CONTROL
  if (netif_xmit_stopped(txq)) { // DMA buffer full
    pr_info("TX DMA queue full\n");
    return NF_ACCEPT;
  }

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

  skb_push(nskb, sizeof(radiotaphd));
  memcpy(nskb->data, radiotaphd, sizeof(radiotaphd));

  skb_reset_mac_header(nskb);

  nskb->ip_summed = CHECKSUM_NONE;
  nskb->priority = TC_PRIO_CONTROL;
  nskb->dev = ppriv->wifidev[0].ndev;
  nskb->priority = TC_PRIO_CONTROL;
  skb_set_queue_mapping(skb, TXQUEUE); 

  skb_shinfo(nskb)->tx_flags = 0;

  dev_direct_xmit(nskb, TXQUEUE); 

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
