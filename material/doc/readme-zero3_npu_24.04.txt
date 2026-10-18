https://github.com/Qengineering/Radxa-Zero-3-NPU-Ubuntu24

sudo nmtui
connect to wifi hotspot

sudo nmap -sn 10.48.231.162/24

ssh radxa@10.48.231.73
radxa

plug usb/eth
sudo nmtui

sudo radxa@192.168.3.2
radxa


/etc/default/u-boot
U_BOOT_FDT_OVERLAYS="rk3568-npu-enable.dtbo"
U_BOOT_FDT_OVERLAYS="rk3568-npu-enable.dtbo radxa-zero3-rpi-camera-v2.dtbo rk3588-uart4-m0.dtbo"
sudo u-boot-update

sudo apt-get install iw
 
mkdir Project
cd Projects
git clone https://github.com/amtcvx/ringwfb.git

radxa-zero3-disabled-wireless.dtbo

sudo systemctl stop wpa_supplicant.service
sudo systemctl stop NetworkManager

/etc/sysctl.conf
net.ipv6.conf.all.disable_ipv6 = 1
net.ipv6.conf.default.disable_ipv6 = 1
net.ipv6.conf.lo.disable_ipv6 = 1

plug usb wifi

cd /home/radxa/Projects/ringwfb/tests/modules7
...
  
gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw, width=1920, height=1080, framerate=30/1, format='NV12' ! mpph265enc rc-mode=vbr bps=3000000 bps-max=3172000  ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5600 host=192.168.3.1
