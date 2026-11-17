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

reboot

sudo nmap -sn 10.13.74.162/24
ssh radxa@10.13.74.26
radxa

git clone https://github.com/amtcvx/ringwfb.git
cd ringwfb
./install.sh

plug usb eth adapter

sudo nmcli connection add type ethernet con-name myusbconnection match.interface-name "enx*" ipv4.addresses "192.168.3.2/24" ipv4.method manual connection.autoconnect yes

sudo nmcli connection up myusbconnection

reboot
ssh radxa@192.168.3.2

sudo rsetup
System / Change default boot target : multi-user.target
Overlays / Manage overlays /
- disable zero wireless
- camera
- uart4

reboot

ssh radxa@192.168.3.2

sudo killall rknn_server

