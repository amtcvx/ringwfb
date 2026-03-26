#!/bin/bash
PROJ=$PWD
DKMS=false
if uname -r | grep -cs "6.8.0-060800-generic"> /dev/null 2>&1; then DKMS=true; fi
if uname -r | grep -cs "5.10.110-15-rockchip"> /dev/null 2>&1; then DKMS=true; fi
if !($DKMS); then
  echo "Kernel version NOT supported !"
  exit
else
  mkdir $PROJ/obj $PROJ/bin
  sudo -E apt-get install -y dkms
  git clone https://github.com/aircrack-ng/rtl8812au.git
  cd rtl8812au
  git checkout v5.6.4.2
  sudo -E make dkms_install
  sudo -E apt-get install iw libnl-3-dev libnl-genl-3-dev libnl-route-3-dev
fi
