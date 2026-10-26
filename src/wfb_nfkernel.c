#include <linux/inet.h>
#include <linux/netfilter_ipv4.h>
#include <net/dst_metadata.h>
#include <net/cfg80211.h>
#include <net/ieee80211_radiotap.h>

/******************************************************************************/
uint16_t indestport = 5700;
uint16_t outdestport = 5600;

typedef struct {
  uint8_t padding;
  uint8_t droneid;
  uint16_t msglen;
  int32_t backfreq;
  uint64_t seq;
  uint16_t dummy;
} __attribute__((packed)) pph_t;

typedef struct {
  uint8_t value; 
  uint16_t freq; 
} chan_t;

typedef struct {
  uint8_t name[20]; 
  uint8_t channb;
  chan_t chan[40];
  struct net_device *dev;
} wifidev_t;

typedef struct {
  uint8_t wifinb;
  uint32_t localipint; 
  struct net_device *localdev;
  wifidev_t  wifidev[4];
} priv_t;

static uint64_t curseq = 0;
static  priv_t mypriv;
static struct nf_hook_ops *output_hk = NULL;

/************************************************************************************************/
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

/******************************************************************************/
static unsigned int output_proc(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {

  if (!skb) return NF_ACCEPT;

  pr_info("OUT_A (%d)\n",skb->len);

  struct iphdr *iph = ip_hdr(skb);

  if(iph && iph->protocol != IPPROTO_UDP) return NF_ACCEPT;

  skb->transport_header = skb->network_header + iph->ihl*4;

  struct udphdr* uph = udp_hdr(skb);

  if (!((mypriv.localipint == iph->saddr) && (mypriv.localipint == iph->daddr) &&  (ntohs(uph->dest)== outdestport)))
    return NF_ACCEPT;

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

  nskb->dev = mypriv.wifidev[0].dev;

  dev_direct_xmit(nskb, 0);

  pr_info("OUT_B (%d) (%d) (%d) (%lu)\n",
    ntohs(pph->msglen),ntohs(uph->len),ntohs(iph->tot_len),sizeof(pph_t));

  return NF_ACCEPT;
}

/******************************************************************************/
static rx_handler_result_t input_proc(struct sk_buff **pskb) {

  if (!*pskb) return RX_HANDLER_CONSUMED;
  struct sk_buff *skb = *pskb;
  if (!skb) return RX_HANDLER_CONSUMED;

  pr_info("IN_A (%d)\n",skb->len);

  priv_t *priv = (priv_t *)rcu_dereference(skb->dev->rx_handler_data);

  struct iphdr *iph;
  struct udphdr* uph;

  uint16_t radiotaplg = (uint16_t)skb->data[2];
  if (!((radiotaplg == 32) || (radiotaplg == 38) || (radiotaplg == 41))) return RX_HANDLER_CONSUMED;
  pph_t *pph = (pph_t *)(skb->data + radiotaplg + 26);
  if ((pph->droneid != 255) || htons(pph->msglen) > skb->len) return RX_HANDLER_CONSUMED;
  pr_info("pay  droneid(%u) msglen(%u) backfreq(%u) seq(%llu)\n",
          pph->droneid, htons(pph->msglen), pph->backfreq, pph->seq);
  uint16_t paylen = pph->msglen;
  struct sk_buff *nskb = skb_clone(skb, GFP_KERNEL);
  skb_trim(nskb,skb->len-4);
  skb_pull(nskb, radiotaplg + 26 + sizeof(pph_t));


  uph = (struct udphdr*)skb_push(nskb, sizeof(*uph));
  skb_reset_transport_header(nskb);
  memset((void *)uph, 0,sizeof(*uph));
  uph->dest = htons(indestport);
  uph->len = htons(8 + htons(paylen));

  iph = (struct iphdr*)skb_push(nskb, sizeof(*iph));
  skb_reset_network_header(nskb);
  memset((void *)iph, 0,sizeof(*iph));
  iph->version = IPVERSION;
  iph->ihl = sizeof(struct iphdr) / 4;
  iph->protocol = IPPROTO_UDP;
  iph->ttl = 64;
  iph->tot_len = htons(20+ntohs(uph->len));
  iph->check = 0;
  iph->check = ip_fast_csum((uint8_t *)iph, iph->ihl);

  skb_reset_mac_header(nskb);

  nskb->dev = priv->localdev;
  nskb->pkt_type = PACKET_HOST;
  nskb->protocol = htons(ETH_P_IP);

  netif_receive_skb(nskb);
  kfree_skb(skb);

  pr_info("IN_B (%hu) (%pI4) (%pI4) (%hu) (%hu) (%hu) \n",
          ntohs(iph->tot_len),
          &(iph->saddr), &(iph->daddr),
          ntohs(uph->len),
          ntohs(uph->source), ntohs(uph->dest));

  return RX_HANDLER_CONSUMED; 
}

/******************************************************************************/
static int __init wfb_nfkernel_init(void) {

  in4_pton("127.0.0.1", 9, (u8 *)&(mypriv.localipint), '\n', NULL);

  char phy_str[4*5];
  struct net_device *dev;
  for_each_netdev_rcu(&init_net, dev) {
    if (dev->flags & IFF_LOOPBACK) { mypriv.localdev = dev; continue; }

    struct device *extparent = dev->dev.parent;
    if (extparent && extparent->driver) {
      if (strcmp(extparent->driver->name, "rtw88_8812au")) continue;
      if (mypriv.wifinb == 4) continue;
      struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
      const char *phy_name = wiphy_name(wiphy);
      sprintf(phy_str + strlen(phy_str), "%s\\|", phy_name);

      strcpy(mypriv.wifidev[mypriv.wifinb].name, dev->name);
      mypriv.wifidev[mypriv.wifinb++].dev = dev;
    }
  }
  if (!(mypriv.wifinb)) return -1;
  phy_str[strlen(phy_str)-2] = '\0';

  static char *envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };

  char shell_cmd[5][80];

  sprintf(shell_cmd[0], "rfkill unblock $(rfkill -n -o ID,DEVICE | grep \"%s\" | cut -c2)",phy_str);
  sprintf(shell_cmd[1], "iw reg set US");
  for (uint8_t cpt=0; cpt<2; cpt++) {
    char *argv[] = { "/bin/sh", "-c", shell_cmd[cpt], NULL };
    call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
  }

  struct wiphy *wiphy;  struct ieee80211_channel *chan;
  for (uint8_t cpt=0; cpt<mypriv.wifinb; cpt++) {
    wiphy = mypriv.wifidev[cpt].dev->ieee80211_ptr->wiphy;
    for (uint8_t i = 0; i < wiphy->bands[NL80211_BAND_2GHZ]->n_channels; i++) {
      chan = &wiphy->bands[NL80211_BAND_2GHZ]->channels[i];
      if (!(IEEE80211_CHAN_DISABLED & chan->flags) && !(IEEE80211_CHAN_NO_IR & chan->flags)) {
        mypriv.wifidev[cpt].chan[mypriv.wifidev[cpt].channb].value = wiphy->bands[NL80211_BAND_2GHZ]->channels[i].hw_value;
        mypriv.wifidev[cpt].chan[mypriv.wifidev[cpt].channb++].freq = wiphy->bands[NL80211_BAND_2GHZ]->channels[i].center_freq;
      }
    }
    for (uint8_t i = 0; i < wiphy->bands[NL80211_BAND_5GHZ]->n_channels; i++) {
      mypriv.wifidev[cpt].chan[mypriv.wifidev[cpt].channb].value = wiphy->bands[NL80211_BAND_5GHZ]->channels[i].hw_value;
      mypriv.wifidev[cpt].chan[mypriv.wifidev[cpt].channb++].freq = wiphy->bands[NL80211_BAND_5GHZ]->channels[i].center_freq;
    }
  }
 
  for (uint8_t cpt=0; cpt<mypriv.wifinb; cpt++) {
    uint8_t *name = mypriv.wifidev[cpt].name;
    uint8_t chan =  mypriv.wifidev[cpt].chan[mypriv.wifidev[cpt].channb - 1].value;
    sprintf(shell_cmd[0], "ip link set %s down",name);
    sprintf(shell_cmd[1], "iw dev %s set type monitor",name);
    sprintf(shell_cmd[2], "ip link set %s up",name);
    sprintf(shell_cmd[3], "sysctl -w net.ipv6.conf.%s.disable_ipv6=1",name);
    sprintf(shell_cmd[4], "iw dev %s set channel %d",name,chan);
    for (uint8_t cpt=0; cpt<5; cpt++) {
      char *argv[] = { "/bin/sh", "-c", shell_cmd[cpt], NULL };
      call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
    }

    dev_set_promiscuity(mypriv.wifidev[cpt].dev,1);
    netdev_rx_handler_register(mypriv.wifidev[cpt].dev, input_proc, (void  *)&mypriv);
  }

  output_hk = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(output_hk != NULL) {
    output_hk->hook     = (nf_hookfn*)output_proc;
    output_hk->hooknum  = NF_INET_POST_ROUTING;
    output_hk->pf       = NFPROTO_IPV4;
    output_hk->priority = NF_IP_PRI_FIRST;
    output_hk->priv      = (void  *)&mypriv;     
    nf_register_net_hook(&init_net, output_hk);
  }

  return 0;
}

/******************************************************************************/
static void __exit wfb_nfkernel_exit(void) {

  static char *envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
  for (uint8_t cpt=0; cpt < mypriv.wifinb; cpt++) {
    char *argv[] = { "ip", "link", "set", mypriv.wifidev[cpt].name, "down", NULL };
    call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
    dev_set_promiscuity(mypriv.wifidev[cpt].dev,0);
    netdev_rx_handler_unregister(mypriv.wifidev[cpt].dev);
  }

  if(output_hk != NULL) {
    nf_unregister_net_hook(&init_net, output_hk);
    kfree(output_hk);
  }
}

/******************************************************************************/
module_init(wfb_nfkernel_init);
module_exit(wfb_nfkernel_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Router");
