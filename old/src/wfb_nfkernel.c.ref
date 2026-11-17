#include <linux/inet.h>
#include <linux/netfilter_ipv4.h>
#include <linux/timer.h>
#include <linux/device.h>
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
  uint8_t curchan; 
  uint16_t pkrcv; 
} log_t;

typedef struct {
  uint8_t channb;
  chan_t chan[40];
  log_t log;
  struct net_device *ndev;
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

static struct timer_list wfb_timer;
static struct work_struct wfb_work;
#define TIMEOUT 1000

#define MAGIC_MARK 0x10

static char *envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
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
static void wfb_work_handler(struct work_struct *work) {

  wifidev_t *wifidev = &mypriv.wifidev[0];
  char shell_cmd[40]; sprintf(shell_cmd, "iw dev %s set channel %d",
		  wifidev->ndev->name,
		  wifidev->chan[wifidev->log.curchan].value);
  char *argv[] = { "/bin/sh", "-c", shell_cmd, NULL };
  call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
}

/******************************************************************************/
static void wfb_timer_callback(struct timer_list *t) {

  wifidev_t *wifidev = &mypriv.wifidev[0];

  pr_info("beep (%d)(%d)(%d) (%d)\n", 
    wifidev->log.curchan, 
    wifidev->chan[wifidev->log.curchan].value, 
    wifidev->chan[wifidev->log.curchan].freq,
    wifidev->log.pkrcv); 
/*
  if (wifidev->log.curchan < (wifidev->channb - 1)) wifidev->log.curchan++;
  else wifidev->log.curchan = 0;
*/
  wifidev->log.pkrcv = 0;
/*
  schedule_work(&wfb_work);
*/
  mod_timer(&wfb_timer, jiffies + msecs_to_jiffies(TIMEOUT));
}

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

  uint16_t paylen = skb->len;
  skb_pull(skb, sizeof(struct iphdr) + sizeof (struct udphdr));
  
  uint8_t headroom = sizeof(radiotaphd) + sizeof(ieeehd) + sizeof(pph_t);
  pskb_expand_head(skb, headroom, 0, GFP_ATOMIC);

  skb_push(skb, sizeof(pph_t));
  pph_t *pph = (pph_t *)skb->data;
  memset((void *)pph, 0, sizeof(pph_t));
  pph->droneid =  0xff;
  pph->seq = curseq;
  pph->msglen = htons(paylen);

  curseq++;

  uint8_t *ptr = skb_push(skb, sizeof(ieeehd));
  memcpy(skb->data, ieeehd, sizeof(ieeehd));
  ptr = skb_push(skb, sizeof(radiotaphd));
  memcpy(skb->data, radiotaphd, sizeof(radiotaphd));


  skb->dev = mypriv.wifidev[0].ndev;
  skb->protocol = htons(ETH_P_802_2); // Mode moniteur brut
  skb->pkt_type = PACKET_OUTGOING;

//  skb->mark = MAGIC_MARK;
//  skb_dst_drop(skb);
//  skb->skb_iif = mypriv.wifidev[0].ndev->ifindex;
//  skb->sk = NULL;

  skb_reset_mac_header(skb);
  skb_set_network_header(skb, headroom);

  dev_direct_xmit(skb, 0);

  //dev_queue_xmit(skb); 
  //dev_direct_xmit(skb, 0);
  //u16 txq_index = 0;
  //struct netdev_queue *xq = netdev_get_tx_queue(mypriv.wifidev[0].ndev, txq_index);
  //dev_direct_xmit(skb, txq_index);


  pr_info("OUT_B (%d) (%d) (%d) (%lu)\n",
    ntohs(pph->msglen),ntohs(uph->len),ntohs(iph->tot_len),sizeof(pph_t));

  return NF_STOLEN;
}

/******************************************************************************/
static rx_handler_result_t input_proc(struct sk_buff **pskb) {

  if (!*pskb) return RX_HANDLER_CONSUMED;
  struct sk_buff *skb = *pskb;
  if (!skb) return RX_HANDLER_CONSUMED;

//  if (skb->pkt_type == PACKET_OUTGOING) return RX_HANDLER_CONSUMED;
//  if (skb->mark == MAGIC_MARK) return RX_HANDLER_CONSUMED;

  pr_info("IN_A (%d)\n",skb->len);

  priv_t *priv = (priv_t *)rcu_dereference(skb->dev->rx_handler_data);

  priv->wifidev[0].log.pkrcv++;

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

  uint8_t nb=0;
  struct net_device *pdev, *devtab[4];
  for_each_netdev_rcu(&init_net, pdev) {
    struct device *extparent = pdev->dev.parent;
    if (extparent && extparent->driver) {
      if (strcmp(extparent->driver->name, "rtw88_8812au")) continue;
      if (nb == 4) continue;
      devtab[nb++] = pdev;
    }
  }
  if (nb == 0) return -1;
  int8_t ret;
  for (uint8_t i=0;i<nb;i++) ret=device_reprobe(devtab[i]->dev.parent);

  for_each_netdev_rcu(&init_net, pdev) {
    if (pdev->flags & IFF_LOOPBACK) { mypriv.localdev = pdev; continue; }
    struct device *extparent = pdev->dev.parent;
    if (extparent && extparent->driver) {
      if (strcmp(extparent->driver->name, "rtw88_8812au")) continue;
      if (mypriv.wifinb == 4) continue;
      mypriv.wifidev[mypriv.wifinb++].ndev = pdev;
    }
  }

  char phy_str[4*5];
  for (uint8_t i=0;i<mypriv.wifinb;i++) {
    struct wireless_dev *wdev = mypriv.wifidev[i].ndev->ieee80211_ptr; 
    strcat(phy_str,wiphy_name(wdev->wiphy));
    if (i+1 < mypriv.wifinb) strcat(phy_str, "\\|");
  }
  char shell_cmd0[150], shell_cmd1[20];
  char *shell_tab[2]= {shell_cmd0, shell_cmd1};
  sprintf(shell_cmd0, "rfkill unblock $(rfkill -n -o ID,DEVICE | grep \"%s\" | "
    "sed -E 's/^([^0-9.]*)([0-9]+).*$/\\2/')",
    phy_str);
  sprintf(shell_cmd1, "iw reg set US");
  for (uint8_t i=0;i<2;i++) {
    char *argv[] = { "/bin/sh", "-c", shell_tab[i], NULL };
    call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
  }

  for (uint8_t i=0;i<mypriv.wifinb;i++) {
    struct wiphy *wiphy = mypriv.wifidev[i].ndev->ieee80211_ptr->wiphy;
    for (uint8_t j=0;j<wiphy->bands[NL80211_BAND_2GHZ]->n_channels;j++) {
      struct ieee80211_channel *chan = &wiphy->bands[NL80211_BAND_2GHZ]->channels[j];
      if (!(IEEE80211_CHAN_DISABLED & chan->flags) && !(IEEE80211_CHAN_NO_IR & chan->flags)) {
        mypriv.wifidev[i].chan[mypriv.wifidev[i].channb].value = wiphy->bands[NL80211_BAND_2GHZ]->channels[j].hw_value;
        mypriv.wifidev[i].chan[mypriv.wifidev[i].channb++].freq = wiphy->bands[NL80211_BAND_2GHZ]->channels[j].center_freq;
      }
    }
    for (uint8_t j=0;j<wiphy->bands[NL80211_BAND_5GHZ]->n_channels;j++) {
      mypriv.wifidev[i].chan[mypriv.wifidev[i].channb].value = wiphy->bands[NL80211_BAND_5GHZ]->channels[j].hw_value;
      mypriv.wifidev[i].chan[mypriv.wifidev[i].channb++].freq = wiphy->bands[NL80211_BAND_5GHZ]->channels[j].center_freq;
    }
  }

  char shell_cmd[5][60];
  for (uint8_t i=0;i<mypriv.wifinb;i++) {
    uint8_t *name = mypriv.wifidev[i].ndev->name;
    mypriv.wifidev[i].log.curchan = 22;
    uint8_t chan =  mypriv.wifidev[i].chan[mypriv.wifidev[i].log.curchan].value;
    sprintf(shell_cmd[0], "ip link set %s down",name);
    sprintf(shell_cmd[1], "iw dev %s set type monitor",name);
    sprintf(shell_cmd[2], "ip link set %s up",name);
    sprintf(shell_cmd[3], "sysctl -w net.ipv6.conf.%s.disable_ipv6=1",name);
    sprintf(shell_cmd[4], "iw dev %s set channel %d",name,chan);
    for (uint8_t j=0;j<5;j++) {
      char *argv[] = { "/bin/sh", "-c", shell_cmd[j], NULL };
      call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
    }

    dev_set_promiscuity(mypriv.wifidev[i].ndev,1);
    netdev_rx_handler_register(mypriv.wifidev[i].ndev, input_proc, (void  *)&mypriv);

  }

  output_hk = (struct nf_hook_ops*)kcalloc(1,  sizeof(struct nf_hook_ops), GFP_KERNEL);
  if(output_hk != NULL) {
    output_hk->hook     = (nf_hookfn*)output_proc;
    output_hk->hooknum  = NF_INET_LOCAL_OUT; // NF_INET_POST_ROUTING;
    output_hk->pf       = NFPROTO_IPV4;
    output_hk->priority = NF_IP_PRI_FIRST;
    output_hk->priv      = (void  *)&mypriv;     
    nf_register_net_hook(&init_net, output_hk);
  }

  INIT_WORK(&wfb_work, wfb_work_handler);
  timer_setup(&wfb_timer, wfb_timer_callback, 0);
  mod_timer(&wfb_timer, jiffies + msecs_to_jiffies(TIMEOUT));

  return 0;
}

/******************************************************************************/
static void __exit wfb_nfkernel_exit(void) {

  printk(KERN_EMERG "WFB ENDING\n");

  timer_shutdown_sync(&wfb_timer);
  cancel_work_sync(&wfb_work);

  if(output_hk != NULL) {
    nf_unregister_net_hook(&init_net, output_hk);
    kfree(output_hk); output_hk = NULL;
  }

  for (uint8_t i=0;i<mypriv.wifinb;i++) {

    dev_set_promiscuity(mypriv.wifidev[i].ndev,0);
    netdev_rx_handler_unregister(mypriv.wifidev[i].ndev);
//    dev_put(mypriv.wifidev[i].ndev);
//    dev_close(mypriv.wifidev[i].ndev);
//    unregister_netdev(mypriv.wifidev[i].ndev);

  }

}

/******************************************************************************/
module_init(wfb_nfkernel_init);
module_exit(wfb_nfkernel_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Router");
