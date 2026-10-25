/*

sudo rfkill

export DEVICE=wlxfc349725a317
sudo ip link set $DEVICE down
sudo iw dev $DEVICE set type monitor
sudo ip link set $DEVICE up
sudo iw dev $DEVICE set freq 5300

gst-launch-1.0 udpsrc port=5700 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false

*/
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/inet.h>

#include <net/dst_metadata.h>

#include <net/mac80211.h>
#include <net/cfg80211.h>

#include <net/ip.h>

/******************************************************************************/
uint8_t *localname = "lo";
uint16_t indestport = 5700;

uint16_t ethport = 5650;

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
  struct wifidev_t  wifidev[4];
} priv_t;

static priv_t mypriv;

/******************************************************************************/
static rx_handler_result_t input_proc(struct sk_buff **pskb) {

  if (!*pskb) return RX_HANDLER_CONSUMED;
  struct sk_buff *skb = *pskb;
  if (!skb) return RX_HANDLER_CONSUMED;

  pr_info("INi1 input_proc kb->len (%d)\n",skb->len);

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

  nskb->dev = mypriv.localdev;
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
    if (dev->flags & IFF_LOOPBACK) { mypriv.localdev = dev, continue; }

    struct device *extparent = dev->dev.parent;
    if (extparent && extparent->driver) {
      if (strcmp(extparent->driver->name, "rtw88_8812au")) continue;
      if (mypriv.wifinb == 4) continue;
      struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
      const char *phy_name = wiphy_name(wiphy);
      sprintf(phy_str + strlen(phy_str), "%s\\|", phy_name);

      strcpy(&mypriv.wifidev[mypriv.wifinb].name, dev->name);
      mypriv.wifidev[mypriv.wifinb++].dev = dev;
    }
  }
  if (!(mypriv.wifinb)) return -1;
  phy_str[strlen(phy_str)-2] = '\0';
 
  static char *envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };

  char shell_cmd[100];
  sprintf(shell_cmd, "rfkill unblock $(rfkill -n -o ID,DEVICE | grep \"%s\" | cut -c2)",phy_str);

  char *argv[6][80];
  argv[0][] = { "/bin/sh", "-c", shell_cmd, NULL };
  argv[1][] = { "/usr/sbin/iw", "reg", "set","US", NULL };
  for (uint8_t cmd=0; cmd<2; cmd++) 
    call_usermodehelper(argv[], argv, envp, UMH_WAIT_PROC);

  struct wiphy *wiphy;  struct ieee80211_channel *chan;
  for (uint8_t cpt=0; cpt<mypriv.wifinb; cpt++) {
    wiphy = mypriv.wifidev[cpt].dev->ieee80211_ptr->wiphy;
    for (int i = 0; i < wiphy->bands[NL80211_BAND_2GHZ]->n_channels; i++) {
      chan = &wiphy->bands[NL80211_BAND_2GHZ]->channels[i];
      if ((IEEE80211_CHAN_DISABLED & chan->flag) && (IEEE80211_CHAN_NO_IR & chan->flags))
        mypriv.wifidev[cpt].chan[mypriv.wifidev[cpt].channb++] = 
		wiphy->bands[NL80211_BAND_2GHZ]->channels[i].hw_value;
      }
    }
  }

  for (uint8_t cpt=0; cpt<mypriv.wifinb; cpt++) {
    uint8_t *name = mypriv.wifidev[cpt].name;
    uint32_t chan =  mypriv.wifidev[cpt].chan;
    argv[0] = { "/usr/sbin/ip", "link", "set",         name, "down", NULL };
    argv[1] = { "/usr/sbin/iw", "dev",                 name, "set", "type", "monitor", NULL };
    argv[2] = { "/usr/sbin/ip", "link", "set",         name, "up", NULL };
    argv[3] = { "/sbin/sysctl", "-w", "net.ipv6.conf.",name, ".disable_ipv6=1", NULL };
    argv[4] = { "/usr/sbin/iw", "dev",                 name, "set", "channel", chan, NULL };
    for (uint8_t cmd=0; cmd<5; cmd++)   call_usermodehelper(argv[cmd][0], argv[cmd], envp, UMH_WAIT_PROC);
    dev_set_promiscuity(mypriv.wifidev[cpt],1);
    netdev_rx_handler_register(mypriv.wifidev[cpt], input_proc[cpt], NULL);
  }

  return 0;
}

/******************************************************************************/
static void __exit wfb_nfkernel_exit(void) {

  if (strlen(devname)>0) {

    static char *envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
    char *argv[] = { "/usr/sbin/ip", "link", "set", devname, "down", NULL };
    call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);

    dev_set_promiscuity(mypriv.wifidev,0);
    netdev_rx_handler_unregister(mypriv.wifidev);
  }
}

/******************************************************************************/
module_init(wfb_nfkernel_init);
module_exit(wfb_nfkernel_exit);

MODULE_LICENSE("GPL");
