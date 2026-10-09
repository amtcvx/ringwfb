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
on radxa ubuntu 24.04

/etc/apt/apt.conf.d/20auto-upgrades

APT::Periodic::Update-Package-Lists "1";
APT::Periodic::Unattended-Upgrade "1";
=>
APT::Periodic::Update-Package-Lists "0";
APT::Periodic::Download-Upgradeable-Packages "0";
APT::Periodic::Unattended-Upgrade "0";
APT::Periodic::AutocleanInterval "0";

------------------
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
sudo apt updatea
sudo rm -rf /var/cache/snapd/ /var/snap/ /var/lib/snapd/ /snap/ ~/snap/

------------------
sudo systemctl set-default multi-user.target
sudo reboot

sudo systemctl set-default graphical.target
sudo reboot


