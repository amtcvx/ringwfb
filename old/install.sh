#!/bin/bash
STR="-------------------------------------------------------------------------\n"
STR+="This script will setup the communication system \n"
STR+="It must be run on ground and board sides \n\n"
STR+="The system, will start as soon as the USB dongle is inserted, or while booting \n"
STR+="with an already inserted dongle \n\n"
STR+="From the ground side you can check the communication (according the configuration):\n\n"
STR+="ssh pprz@10.0.1.2 \n\n"
STR+="-------------------------------------------------------------------------\n"
STR+="When using a Virtual machine for the installation, make sure you set \n"
STR+="Virtual Machine Setting / Hardware / USB Controller / USB compatibility USB 3.1 \n\n"
STR+="-------------------------------------------------------------------------\n"
STR+="Do you want to proceed with the installation (Internet and sudo required) " 
echo -e $STR
USER=`basename $PWD`
PROJ=$PWD
read -p "for this side in $PROJ (y/n) ?" ANSWER
if [ ! $ANSWER = "y" ] || [ -z $ANSWER ]; then exit -1; fi
if ! groups | grep -q 'sudo'; then exit -1; fi

mkdir $PROJ/build
for f in $PROJ/material/60-wfb.rules $PROJ/material/wfb.service $PROJ/scripts/wfb_on.sh; do
  cp ${f} $PROJ/build/${f##*/}
  sed -i 's#TOBEUPDATEATINSTALLATION#'$PROJ'#' $PROJ/build/${f##*/}
done;
  
sudo apt-get install iw make gcc

if uname -a | grep -cs "6.1.84-10-rk2410-nocsf"> /dev/null 2>&1; then

  sudo apt-get install socat

  git clone https://github.com/lwfinger/rtw88
  cd $PWD/rtw88
  sudo dkms install $PWD
  sudo make install_fw
  sudo modprobe rtw_8812au
  sudo systemctl stop  wpa_supplicant
  sudo systemctl disable wpa_supplicant.service
#else
#  sudo apt-get install gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
fi

cd $PROJ/src
make
sudo cp $PROJ/src/wfb_nfkernel.ko /lib/modules/`uname -r`
sudo depmod -A


sudo cp $PROJ/material/rtw88.conf /etc/modprobe.d

sudo cp $PROJ/material/unmanaged.conf /etc/NetworkManager/conf.d
sudo cp $PROJ/material/60-wfb.rules /etc/udev/rules.d
#sudo cp $PROJ/material/wfb.service /etc/systemd/system

sudo systemctl reload NetworkManager.service
sudo systemctl daemon-reload
sudo udevadm control --reload-rules

sudo modprobe wfb_nfkernel
