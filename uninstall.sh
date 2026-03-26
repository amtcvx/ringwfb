#!/bin/bash
PROJ=$PWD
DKMS=false
if uname -r | grep -cs "6.8.0-060800-generic"> /dev/null 2>&1; then DKMS=true; fi
if uname -r | grep -cs "5.10.110-15-rockchip"> /dev/null 2>&1; then DKMS=true; fi
if !($DKMS); then
  echo "Kernel version NOT supported !"
  exit
else
  rm -rf $PROJ/obj $PROJ/bin
  cd rtl8812au
  sudo -E make dkms_remove
  rm -Rf $PROJ/rtl8812au 
fi
