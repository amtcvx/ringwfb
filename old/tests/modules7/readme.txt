sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio

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
ON PC 

sudo apt-get install gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio

sudo apt-get install iw make
sudo apt-get install gcc-13

/etc/modprobe.d/rtw88.conf
options rtw88_usb switch_usb_mode=n

/etc/NetworkManager/conf.d/unmanaged.conf
[keyfile]
unmanaged-devices=interface-name:wlx*

/etc/sysctl.conf
net.ipv6.conf.all.disable_ipv6 = 1
net.ipv6.conf.default.disable_ipv6 = 1
net.ipv6.conf.lo.disable_ipv6 = 1

sudo cp wfb_nfkernel.ko /lib/modules/$(uname -r)/kernel/net

/etc/modprobe.d/rtw88.conf
softdep wfb_nfkernel pre: rtw88_8812au

sudo depmod -a

#sudo modprobe wfb_nfkernel
#sudo modprobe -r wfb_nfkernel

wfb_usb.sh

/etc/udev/rules.d/60-wfb.rules
pprz@darkvador:~/Projects/ringwfb/scripts$ cat /etc/udev/rules.d/60-wfb.rules SUBSYSTEM=="net", ACTION=="add|remove", ATTRS{idVendor}=="0b05", ATTRS{idProduct}=="17d2", ENV{NM_UNMANAGED}="1", RUN+="/home/pprz/Projects/ringwfb/scripts/wfb_usb.sh"

plugUSB


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
400 Mb !
software updater
680 Mb !

-----------------
PC wifi connect to hotspot

PC ip address => Radxa wifi address

sudo nmap -sn 10.48.231.162/24

ssh radxa@10.48.231.26

-----------------
sudo apt-get update
sudo apt-get upgrade
sudo apt-get install iw make socat

sudo apt-get install camera-engine-rkaiq-rk3588
https://github.com/Joshua-Riek/ubuntu-rockchip/issues/1127

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
switchoff wifi, bluetooth
switchoff session

sudo dd if=/dev/sdb of=ubuntu24-lite.img bs=4M status=progress conv=fsync

-----------------
On PC nmtui
plug usb/eth adpater
sudo nmcli connection add type ethernet con-name "myusbconnection" ifname enx* 
sudo nmtui
set 192.168.3.1 and activate

-----------------
On radxa
plug usb/eth adpater
sudo nmcli connection add type ethernet con-name "myusbconnection" ifname enx* ip4 192.168.3.2/24
sudo nmcli connection up "myusbconnection"

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


-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
ON RADXA PRE FLASH

sudo dd if=/dev/sdb of=ubuntu24-full.img bs=4M status=progress conv=fsync
=> 32 Go (15 Go)

_____________
SHRINK

sudo losetup -f -P --show ubuntu24-full.img
=> /dev/loop8

sudo losetup -a
=> /dev/loop8: [66306]:4459410 (/home/pprz/Téléchargements/ubuntu24-full.img)
sudo gparted /dev/loop8

device /dev/loop8
ext4 resize
apply

close gparted
sudo losetup -d /dev/loop8

_____________
TRUNCATE

sudo gdisk -l ubuntu24-full.img
=>
Found valid GPT with protective MBR; using GPT.
Disk ubuntu24-full.img: 62333952 sectors, 29.7 GiB
Sector size (logical): 512 bytes

Number  Start (sector)    End (sector)  Size       Code  Name
   1           32768        32946175   15.7 GiB    EF00  primary


NewSize = (EndSector + 1 + 33) x SectorSize
(32946175 + 1 + 33) x 512 = 16868459008

sudo truncate --size=<NewSizeInBytes> ubuntu24-full.img
=> 16 Go

sudo gdisk ubuntu24-full.img
w

#xz -k ubuntu24-full.img
#=> 5.1 Go

_____________
COPY TO SD
unplug & plug
dmesg -w
=>
[ 4702.591102] GPT:Primary header thinks Alt. header is not at the end of the disk.

sudo gparted
repare

_____________
REBOOT

sudo partprobe

_____________
RESTORE

imager_2.0.12_amd64.AppImage
ubuntu24-full.img.xz
(5.4 Go)

dmesg -w
plug & unplug
=>
[   75.788840] GPT:Primary header thinks Alt. header is not at the end of the disk.
[   75.788851] GPT:32946208 != 61951999
[   75.788858] GPT:Alternate GPT header not at the end of the disk.
[   75.788862] GPT:32946208 != 61951999
[   75.788867] GPT: Use GNU Parted to correct GPT errors.

sudo gparted
Repare

Boot
login: radxa
password: radxa

~            

