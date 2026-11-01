/************************************************************************************************/
static char *envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
static struct timer_list wfb_timer;
static struct work_struct wfb_work;

static priv_t mypriv;
priv_t *ppriv = &mypriv;

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
static int utils_init(void) {

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
    ssleep(2);
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

    if (input_init(mypriv.wifidev[i].ndev)) return -1;
  }
}

/******************************************************************************/
static int utils_init(void) {

  INIT_WORK(&wfb_work, wfb_work_handler);
  timer_setup(&wfb_timer, wfb_timer_callback, 0);
  mod_timer(&wfb_timer, jiffies + msecs_to_jiffies(TIMEOUT));
}

/******************************************************************************/
static int utils_exit(void) {

  timer_shutdown_sync(&wfb_timer);
  cancel_work_sync(&wfb_work);
}

