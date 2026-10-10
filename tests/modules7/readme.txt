sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio

sudo apt-get install iw make
sudo apt-get install gcc-13

-------------------------------------------------------------------------------

sudo systemctl stop NetworkManager
sudo systemctl stop wpa_supplicant.service 

/etc/modprobe.d/rtw88.conf
options rtw88_usb switch_usb_mode=n

plugUSB

sudo modprobe -r rtw88_8812au
sudo modprobe rtw88_8812au

sudo rfkill

sudo iw reg set US

export DEVICE=wlxfc349725a317
sudo ip link set $DEVICE down
sudo iw dev $DEVICE set type monitor
sudo ip link set $DEVICE up
sudo iw dev $DEVICE set freq 5300

sudo mdk4 $DEVICE b

sudo wireshark -i $DEVICE -k

/etc/sysctl.conf
net.ipv6.conf.all.disable_ipv6 = 1
net.ipv6.conf.default.disable_ipv6 = 1
net.ipv6.conf.lo.disable_ipv6 = 1

-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
ON RADXA ZERO 3W

https://github.com/Qengineering/Radxa-Zero-3-NPU-Ubuntu24

External HDMI display, usb keyboard and usb mouse

Radxa wifi connect to hotspot
sudo nmtui radio on
sudo nmtui

sudo apt-get update
sudo apt-get upgrade
software updater ?
500 Mb !

-----------------
PC wifi connect to hotspot

PC ip address => Radxa wifi address

sudo nmap -sn 10.48.231.162/24

ssh radxa@10.48.231.26

-----------------
sudo apt-get install iw make
sudo apt-get install camera-engine-rkaiq-rk3588

mkdir Projects
cd Projects
git clone https://github.com/amtcvx/ringwfb.git

-----------------
/etc/sysctl.conf
net.ipv6.conf.all.disable_ipv6 = 1
net.ipv6.conf.default.disable_ipv6 = 1
net.ipv6.conf.lo.disable_ipv6 = 1

-----------------
/etc/apt/apt.conf.d/20auto-upgrades

APT::Periodic::Update-Package-Lists "1";
APT::Periodic::Unattended-Upgrade "1";
=>
APT::Periodic::Update-Package-Lists "0";
APT::Periodic::Download-Upgradeable-Packages "0";
APT::Periodic::Unattended-Upgrade "0";
APT::Periodic::AutocleanInterval "0";

snap list
=>
firefox            146.0.1-1                       7563   latest/stable  mozilla✓    -
...
sudo snap remove --purge firefox
sudo snap remove --purge gnome-42-2204
sudo snap remove --purge gtk-common-themes
sudo snap remove --purge lxd
sudo snap remove --purge bare
sudo snap remove --purge core22
sudo snap remove --purge core24
sudo snap remove --purge snapd

sudo apt purge --autoremove snapd -y
echo -e "Package: snapd\nPin: release a=*\nPin-Priority: -10" | sudo tee /etc/apt/preferences.d/nosnap.pref
sudo apt update
sudo rm -rf /var/cache/snapd/ /var/snap/ /var/lib/snapd/ /snap/ ~/snap/

-----------------
On PC nmtui
wired connection 
192.168.3.1

-----------------
On radxa
plug usb adpater
sudo nmtui 
wired connection 2
192.168.3.2
sudo nmtui 
activate wire connection 2

-----------------
On PC
ssh radxa@192.168.3.2

/etc/default/u-boot
U_BOOT_FDT_OVERLAYS="rk3568-npu-enable.dtbo"
U_BOOT_FDT_OVERLAYS="rk3568-npu-enable.dtbo radxa-zero3-rpi-camera-v2.dtbo rk3588-uart4-m0.dtbo radxa-zero3-disabled-wireless.dtbo"
sudo u-boot-update

-----------------
sudo systemctl stop wpa_supplicant
sudo systemctl disable wpa_supplicant

/etc/NetworkManager/conf.d/unmanaged.conf
[keyfile]
unmanaged-devices=interface-name:wlx*

-----------------
sudo systemctl set-default multi-user.target
sudo reboot

#sudo systemctl set-default graphical.target
#sudo reboot

-----------------
sudo systemctl start rkaiq_3A
sudo systemctl enable rkaiq_3A
