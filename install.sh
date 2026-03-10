#!/bin/bash
PROJ=$PWD
DKMS=false
mkdir $PROJ/obj $PROJ/bin
if uname -r | grep -cs "5.10.110-15-rockchip"> /dev/null 2>&1; then DKMS=true; fi
if $DKMS; then
  apt-get install -y dkms
  git clone https://github.com/aircrack-ng/rtl8812au.git
  cd rtl8812au
  git checkout v5.6.4.2
  make dkms_install
else
  echo "Using driver kernel"
fi
