#!/bin/bash
PROJ=$PWD
mkdir $PROJ/obj $PROJ/bin
if uname -a | grep -cs "5.10.160-legacy-rk35xx"> /dev/null 2>&1; then DKMS=true; fi
if $DKMS; then
  sudo apt-get install -y dkms
  git clone https://github.com/aircrack-ng/rtl8812au.git
  cd rtl8812au
  git checkout v5.6.4.2
  make dkms_install
else
  echo "Using driver kernel"
fi
