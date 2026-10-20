#!/bin/bash
PROJ=$PWD

sudo rm -Rf $PROJ/build

if uname -a | grep -cs "6.1.84-10-rk2410-nocsf"> /dev/null 2>&1; then
  sudo modprobe -r rtw_8812au
  cd $PROJ/rtw88
  sudo make uninstall
  sudo dkms remove rtw88/0.6
  cd $PROJ
  sudo rm -R $PROJ/rtw88
  sudo systemctl start  wpa_supplicant
  sudo systemctl enable wpa_supplicant.service
  sudo rm /etc/modprobe.d/rtw88.conf
fi

sudo systemctl stop wfb.service
sudo systemctl disable wfb.service
sudo rm /etc/systemd/system/wfb.service

sudo rm /etc/NetworkManager/conf.d/unmanaged.conf
sudo systemctl reload NetworkManager.service
sudo systemctl daemon-reload

sudo rm /etc/udev/rules.d/60-wfb.rules
sudo udevadm control --reload-rules
