https://armbian.com/fr/boards/radxa-zero3


Debian 13 Minimal CLI 6.18.44
https://dl.armbian.com/radxa-zero3/Trixie_current_minimal

login root 
password 12345

create user
connect via wireless

ssh pprz@10.13.74.26

sudo apt-get update
sudo apt-get upgrade

sudo apt-get install socat vim git

sudo apt-get install mdk4
sudo apt install -y python3-scapy

mkdir Projects
cd Projects
git clone https://github.com/amtcvx/ringwfb.git

/boot/armbianEnv.txt

sudo armbian-add-overlay radxa-zero3-rpi-camera-v2.dtbo


