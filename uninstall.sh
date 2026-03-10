#!/bin/bash
PROJ=$PWD
DKMS=false
rm -rf $PROJ/obj $PROJ/bin
if uname -a | grep -cs "5.10.110-15-rockchip"> /dev/null 2>&1; then DKMS=true; fi
if $DKMS; then
  drivername=`dkms status | grep 8812 | awk '{print substr($1,1,length($1)-1)}'`
  driverversion=`dkms status | grep 8812 | awk '{print substr($2,1,length($2)-1)}'`
  driver=$drivername'/'$driverversion
  dkms uninstall $driver
  dkms remove $driver
  rm -Rf $PROJ/rtl8812au 
fi
