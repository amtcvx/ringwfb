#include <linux/inet.h>
#include <net/dst_metadata.h>
#include <net/cfg80211.h>

/******************************************************************************/
uint16_t indestport = 5700;

typedef struct {
  uint8_t padding;
  uint8_t droneid;
  uint16_t msglen;
  int32_t backfreq;
  uint64_t seq;
  uint16_t dummy;
} __attribute__((packed)) pph_t;

typedef struct {
  uint8_t name[20]; 
  uint16_t channb;
  uint32_t chan[70];
  struct net_device *dev;
} wifidev_t;

typedef struct {
  uint8_t wifinb;
  uint32_t localipint; 
  struct net_device *localdev;
  wifidev_t  wifidev[4];
} priv_t;

static  priv_t mypriv;

/******************************************************************************/
static rx_handler_result_t input_proc(struct sk_buff **pskb) {

  if (!*pskb) return RX_HANDLER_CONSUMED;
  struct sk_buff *skb = *pskb;
  if (!skb) return RX_HANDLER_CONSUMED;

  pr_info("INi1 input_proc kb->len (%d)\n",skb->len);

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

  pr_info("OUT input_proc  tot_len(%hu) ips(%pI4) ipd(%pI4) ulen(%hu) ups(%hu) upd(%hu) \n",
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
  sprintf(shell_cmd[1], "/usr/sbin/iw reg set US");
  for (uint8_t cpt=0; cpt<2; cpt++) {
    char *argv[] = { "/bin/sh", "-c", shell_cmd[cpt], NULL };
    call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
  }

  struct wiphy *wiphy;  struct ieee80211_channel *chan;
  for (uint8_t cpt=0; cpt<mypriv.wifinb; cpt++) {
    wiphy = mypriv.wifidev[cpt].dev->ieee80211_ptr->wiphy;
    for (int i = 0; i < wiphy->bands[NL80211_BAND_2GHZ]->n_channels; i++) {
      chan = &wiphy->bands[NL80211_BAND_2GHZ]->channels[i];
      if ((IEEE80211_CHAN_DISABLED & chan->flags) && (IEEE80211_CHAN_NO_IR & chan->flags))
        mypriv.wifidev[cpt].chan[mypriv.wifidev[cpt].channb++] = 
		wiphy->bands[NL80211_BAND_2GHZ]->channels[i].hw_value;
    }
  }

  for (uint8_t cpt=0; cpt<mypriv.wifinb; cpt++) {
    uint8_t *name = mypriv.wifidev[cpt].name;
    uint32_t chan =  mypriv.wifidev[cpt].chan[cpt];

    sprintf(shell_cmd[0], "/usr/sbin/ip link set %s down",name);
    sprintf(shell_cmd[1], "/usr/sbin/iw dev %s set type monitor",name);
    sprintf(shell_cmd[2], "/usr/sbin/ip link set %s up",name);
    sprintf(shell_cmd[3], "/sbin/sysctl -w net.ipv6.conf.%s.disable_ipv6=1",name);
    sprintf(shell_cmd[4], "/usr/sbin/iw dev %s set channel %d",name,chan);
    for (uint8_t cpt=0; cpt<5; cpt++) {
      char *argv[] = { "/bin/sh", "-c", shell_cmd[cpt], NULL };
      call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
    }

    dev_set_promiscuity(mypriv.wifidev[cpt].dev,1);
    netdev_rx_handler_register(mypriv.wifidev[cpt].dev, input_proc, (void  *)&mypriv);
  }

  return 0;
}

/******************************************************************************/
static void __exit wfb_nfkernel_exit(void) {

  static char *envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
  for (uint8_t cpt=0; cpt < mypriv.wifinb; cpt++) {
    char *argv[] = { "/usr/sbin/ip", "link", "set", mypriv.wifidev[cpt].name, "down", NULL };
    call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
    dev_set_promiscuity(mypriv.wifidev[cpt].dev,0);
    netdev_rx_handler_unregister(mypriv.wifidev[cpt].dev);
  }
}

/******************************************************************************/
module_init(wfb_nfkernel_init);
module_exit(wfb_nfkernel_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver");
