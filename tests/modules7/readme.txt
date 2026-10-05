sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio

-------------------------------------------------------------------------------

if rtw88 
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
