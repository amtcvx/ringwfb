https://armbian.com/fr/boards/radxa-zero3


Debian 13 Minimal CLI 6.18.44
https://dl.armbian.com/radxa-zero3/Trixie_current_minimal

login root 
password 12345

create user
connect via wireless

ssh pprz@10.13.74.26

uname -r
=> 6.1.115-vendor-rk35xx

sudo apt-get update
sudo apt-get upgrade

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

