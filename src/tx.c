#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/etherdevice.h>
#include <linux/moduleparam.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Developer");
MODULE_DESCRIPTION("Injection Radiotap avec Modulation MCS (802.11n) sous Noyau 6.8");
MODULE_VERSION("1.4");

/*****************************************************************************/
/* Paramètres configurables dynamiquement via Sysfs
MCS 7,  20 MHz GI standard  (65 Mbps) 
echo "7" | sudo tee /sys/module/udp_rt_mod/parameters/mcs_index
echo "0" | sudo tee /sys/module/udp_rt_mod/parameters/mcs_bw
echo "0" | sudo tee /sys/module/udp_rt_mod/parameters/mcs_sgi

MCS 7, 40 MHz et Short GI mono-flux (150 Mbps) 
echo "1" | sudo tee /sys/module/udp_rt_mod/parameters/mcs_bw
echo "1" | sudo tee /sys/module/udp_rt_mod/parameters/mcs_sgi
*/
static int mcs_index_param = 7;    // MCS 7 par défaut (Le plus rapide en flux unique HT20/HT40)
static int mcs_bw_param    = 0;    // 0 = 20 MHz (HT20), 1 = 40 MHz (HT40)
static int mcs_sgi_param   = 1;    // 1 = Short Guard Interval activé, 0 = Long GI
static int rssi_param      = -45; 
static char *ifname_param  = "mon0"; 

module_param_named(mcs_index, mcs_index_param, int, 0644);
MODULE_PARM_DESC(mcs_index, "Index MCS 802.11n (0 à 31)");

module_param_named(mcs_bw, mcs_bw_param, int, 0644);
MODULE_PARM_DESC(mcs_bw, "Largeur de bande : 0 = 20MHz, 1 = 40MHz");

module_param_named(mcs_sgi, mcs_sgi_param, int, 0644);
MODULE_PARM_DESC(mcs_sgi, "Short Guard Interval : 0 = Désactivé, 1 = Activé");

module_param_named(rssi, rssi_param, int, 0644);
module_param_named(ifname, ifname_param, charp, 0644);

/*****************************************************************************/
#define IEEE80211_RADIOTAP_DBM_ANTSIGNAL  (1 << 5)
#define IEEE80211_RADIOTAP_TX_FLAGS       (1 << 15)
#define IEEE80211_RADIOTAP_MCS            (1 << 19) // Bit 19 : Champ MCS (802.11n High Throughput)

#define IEEE80211_RADIOTAP_F_TX_NOACK     (1 << 3)

#define RADIOTAP_MCS_KNOWN_INDEX   (1 << 0) // On connaît l'index MCS
#define RADIOTAP_MCS_KNOWN_BW      (1 << 1) // On connaît la bande passante
#define RADIOTAP_MCS_KNOWN_SGI     (1 << 2) // On connaît le type de Guard Interval

#define RADIOTAP_MCS_FLAG_BW_40    (1 << 0) // Bit à 1 pour 40 MHz
#define RADIOTAP_MCS_FLAG_SGI      (1 << 2) // Bit à 1 pour Short GI

/*****************************************************************************/
struct __packed radiotap_hdr {
    u8     it_version;
    u8     it_pad;
    __le16 it_len;
    __le32 it_present;
    s8     rssi_dbm;
    u8     pad_align;
    __le16 tx_flags;
    u8     mcs_known;
    u8     mcs_flags;
    u8     mcs_index;
    u8     pad_final;
};

struct __packed ieee80211_qos_data_hdr {
    __le16 frame_control;
    __le16 duration_id;
    u8     addr1;
    u8     addr2;
    u8     addr3;
    __le16 seq_ctrl;
    __le16 qos_ctrl;
};

struct __packed llc_snap_hdr {
    u8     dsap;
    u8     ssap;
    u8     ctrl;
    u8     oui;
    __be16 ethertype;
};

#define TOTAL_ENCAP_SZ (sizeof(struct radiotap_hdr) + sizeof(struct ieee80211_qos_data_hdr) + sizeof(struct llc_snap_hdr))

/*****************************************************************************/
static unsigned int udp_to_radiotap_hook_fn(void *priv,
                                            struct sk_buff *skb,
                                            const struct nf_hook_state *state) {
  struct iphdr *iph;
  struct udphdr *udph;
  struct net_device *monitor_dev;
    
  struct radiotap_hdr *rt_hdr;
  struct ieee80211_qos_data_hdr *wifi_hdr;
  struct llc_snap_hdr *llc_hdr;

 if (!skb) return NF_ACCEPT;

 iph = ip_hdr(skb);
 if (!iph || iph->protocol != IPPROTO_UDP) return NF_ACCEPT;

  udph = udp_hdr(skb);
  if (!udph || ntohs(udph->dest) != 9999) return NF_ACCEPT;

  monitor_dev = dev_get_by_name(state->net, ifname_param);
  if (!monitor_dev) return NF_ACCEPT; 

  if (skb_headroom(skb) < TOTAL_ENCAP_SZ) {
    struct sk_buff *nskb = skb_realloc_headroom(skb, TOTAL_ENCAP_SZ);
    if (!nskb) {
      dev_put(monitor_dev);
      return NF_DROP;
    }
    consume_skb(skb);
    skb = nskb;
  }

  skb_push(skb, TOTAL_ENCAP_SZ);
  skb_reset_mac_header(skb);

  rt_hdr   = (struct radiotap_hdr *)skb->data;
  wifi_hdr = (struct ieee80211_qos_data_hdr *)((u8 *)rt_hdr + sizeof(struct radiotap_hdr));
  llc_hdr  = (struct llc_snap_hdr *)((u8 *)wifi_hdr + sizeof(struct ieee80211_qos_data_hdr));

  rt_hdr->it_version = 0;
  rt_hdr->it_pad     = 0;
  rt_hdr->it_len     = cpu_to_le16(sizeof(struct radiotap_hdr));
    
  rt_hdr->it_present = cpu_to_le32(IEEE80211_RADIOTAP_DBM_ANTSIGNAL | 
                                     IEEE80211_RADIOTAP_TX_FLAGS | 
                                     IEEE80211_RADIOTAP_MCS);
    
  rt_hdr->rssi_dbm   = (s8)rssi_param;
  rt_hdr->pad_align  = 0;
  rt_hdr->tx_flags   = cpu_to_le16(IEEE80211_RADIOTAP_F_TX_NOACK);

  rt_hdr->mcs_known = (RADIOTAP_MCS_KNOWN_INDEX | RADIOTAP_MCS_KNOWN_BW | RADIOTAP_MCS_KNOWN_SGI);
    
  rt_hdr->mcs_flags = 0;
  if (mcs_bw_param == 1) rt_hdr->mcs_flags |= RADIOTAP_MCS_FLAG_BW_40;
  if (mcs_sgi_param == 1) rt_hdr->mcs_flags |= RADIOTAP_MCS_FLAG_SGI;
    
  rt_hdr->mcs_index = (u8)mcs_index_param;
  rt_hdr->pad_final = 0;

  wifi_hdr->frame_control = cpu_to_le16(0x0088); //  QoS Data Frame
 
  wifi_hdr->duration_id   = 0;
    
  eth_broadcast_addr(wifi_hdr->addr1); 
  eth_zero_addr(wifi_hdr->addr2);      
  eth_zero_addr(wifi_hdr->addr3);      
  wifi_hdr->seq_ctrl      = 0;
  wifi_hdr->qos_ctrl      = cpu_to_le16(0x0020); // Ack Policy: No Ack

  llc_hdr->dsap      = 0xAA; // IPV4 ...
  llc_hdr->ssap      = 0xAA;
  llc_hdr->ctrl      = 0x03;
  llc_hdr->oui    = 0x00;
  llc_hdr->oui    = 0x00;
  llc_hdr->oui    = 0x00;
  llc_hdr->ethertype = htons(ETH_P_IP); 

  skb->dev      = monitor_dev;
  skb->protocol = htons(ETH_P_802_2); 
  skb->pkt_type = PACKET_OUTGOING;

  dev_queue_xmit(skb);

  dev_put(monitor_dev);
  return NF_STOLEN;
}

/*****************************************************************************/
static struct nf_hook_ops udp_rt_hook_ops = {
    .hook     = udp_to_radiotap_hook_fn,
    .pf       = NFPROTO_IPV4,
    .hooknum  = NF_INET_LOCAL_OUT,
    .priority = NF_IP_PRI_FIRST,
};

static int __init radiotap_mod_init(void) {
  return nf_register_net_hook(&init_net, &udp_rt_hook_ops);
}

static void __exit radiotap_mod_exit(void) {
  nf_unregister_net_hook(&init_net, &udp_rt_hook_ops);
}

module_init(radiotap_mod_init);
module_exit(radiotap_mod_exit);
