--------------------------------------------------------------
--------------------------------------------------------------
https://github.com/radxa-build/radxa-zero3/releases/tag/rsdk-b1
https://github.com/radxa-build/radxa-zero3/releases/download/rsdk-b1/radxa-zero3_bookworm_kde_b1.output_512.img.xz

Flash SD
sudo gparted
=> repare

mount config
hello.txt
wifi hotspot password

Wait 2 minutes first boot

(
sudo apt update && sudo apt install openssh-server -y
sudo systemctl start ssh
sudo systemctl enable ssh
)

login : radxa
passw : radxa

uname -r
6.1.84-10-rk2410-nocsf

sudo apt-get udpate
DO NOT UPGRADE
#sudo apt-get upgrade 
#400 Mo !

#Discover - Updates
#300 Mo !

rsetup
- camera
- uart4
reboot

sudo nmap -sn 10.13.74.162/24
ssh radxa@10.13.74.26
radxa

Plug usb wifi adapter
=> NO DRIVER

/etc/NetworkManager/conf.d/99-unmanage-enx.conf
[device-enx-wildcard]
match-device=interface-name:enx*
managed=0

reboot

Plug usb eth adapter
sudo nmcli connection add type ethernet con-name "myusbconnection" ifname enx* ip4 192.168.3.2/24
sudo nmcli connection up "myusbconnection"


-------------------------------------------------------------------------------
https://armbian.com/boards/radxa-zero3

Minimal / IOT
Build date: Aug 12, 2026

Ubuntu 26.04 (RESOLUTE) Minimal (CLI) vendor6.1.115 387 MB 
https://dl.armbian.com/radxa-zero3/Resolute_vendor_minimal

sudo apt update && sudo apt install -y software-properties-common

sudo add-apt-repository ppa:jjriek/rockchip-multimedia


sudo add-apt-repository 
  ppa:liujianfeng1994/rockchip-multimedia
#  ppa:jjriek/rockchip-multimedia

sudo apt update
sudo apt install rockchip-multimedia-config

-------------------------------------------------------------------------------
Debian 13 Minimal CLI 6.11.115 (TRIXIE)
https://dl.armbian.com/radxa-zero3/Trixie_vendor_minimal

login root 
password 12345

create user
connect via wireless

ssh pprz@10.13.74.26

uname -r
=> 6.1.115-vendor-rk35xx

sudo apt-get update
sudo apt-get upgrade

sudo apt-get install gstreamer1.0-tools
gst-inspect-1.0 | grep 265
=> NO 

sudo apt-get install socat vim git

sudo apt-get install mdk4
sudo apt install -y python3-scapy

mkdir Projects
cd Projects
git clone https://github.com/amtcvx/ringwfb.git

sudo mkdir /boot/overlay-user
cd /boot/dtb-6.1.115-vendor-rk35xx/rockchip/overlay/
sudo cp radxa-zero3-rpi-camera-v2.dtbo /boot/overlay-user
sudo cp rk3588-uart4-m0.dtbo /boot/overlay-user

/boot/armbianEnv.txt
user_overlays=radxa-zero3-rpi-camera-v2 rk3588-uart4-m0 radxa-zero3-disabled-wireless

