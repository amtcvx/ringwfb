https://github.com/Qengineering/Radxa-Zero-3-NPU-Ubuntu24
Radxa_Zero3_NPU_Ubuntu24.img.xz (3.9Gb)
Radxa_Zero3_NPU_Ubuntu24.img  (15.9Gb)
Flash SD 16Gb 

Radxa connect to HDMI + USB keyboard + USB mouse
(Caution with USB Hub !)

Radxa setup using on board wireless chip

connect radxa to wifi hotspot
(using desktop)

connect pc to wifi hotspot
ip address
=> 192.168.42.113/24

sudo nmap -sn 192.168.42.113/24
=> 192.168.42.156

ssh radxa@192.168.42.156
radxa

mkdir Projects
cd Projects
git clone https://github.com/amtcvx/ringwfb.git

cd ringwfb
install devourer described in install.txt
git clone https://github.com/OpenIPC/devourer.git
...

-------------------------------------------------------------------------------
sudo apt update
(DO NOT UPGRADE !)

https://github.com/roju/rkaiq_3A_server-rk356x.git
cd rkaiq_3A_server-rk356x/
mkdir build && cd build
cmake .. -DRKAIQ_TARGET_SOC=rk356x -DISP_HW_VERSION=-DISP_HW_V21 -DARCH=aarch64 -DCMAKE_INSTALL_PREFIX=/usr
make 
cmake ../rkaiq_3A_server 
make

sudo systemctl stop rkaiq_3A.service
sudo cp rkaiq/all_lib/Release/librkaiq.so /usr/lib/librkaiq.so
sudo cp rkaiq_3A_server/rkaiq_3A_server /usr/bin/rkaiq_3A_server
sudo ldconfig
sudo systemctl start rkaiq_3A.service

-------------------------------------------------------
sudo apt install -y zram-config
sudo systemctl set-default multi-user.target
(sudo systemctl set-default graphical.target)

/etc/systemd/journald.conf
SystemMaxUse=50M

/etc/fstab
tmpfs /tmp tmpfs defaults,noatime,nosuid,nodev,mode=1777,size=256M 0 0

sudo systemctl disable --now cups cups-browsed
sudo systemctl disable --now snapd.service snapd.socket
sudo systemctl disable --now ModemManager bluetooth avahi-daemon

sudo systemctl stop packagekit.service
sudo systemctl mask packagekit.service

-------------------------------------------------------------------------------
plug usb/eth

on pc
sudo nmcli connection add type ethernet con-name myusbconnection match.interface-name "enx*" ipv4.addresses "192.168.3.1/24" ipv4.method manual connection.autoconnect yes
sudo nmcli connection up myusbconnection

on radxa
sudo nmcli connection add type ethernet con-name myusbconnection match.interface-name "enx*" ipv4.addresses "192.168.3.2/24" ipv4.method manual connection.autoconnect yes
sudo nmcli connection up myusbconnection


/etc/default/u-boot
U_BOOT_FDT_OVERLAYS="rk3568-npu-enable.dtbo"
U_BOOT_FDT_OVERLAYS="rk3568-npu-enable.dtbo radxa-zero3-rpi-camera-v2.dtbo rk3588-uart4-m0.dtbo radxa-zero3-disabled-wireless.dtbo"
sudo u-boot-update
sudo reboot

Unplug HDMI + USB Keyboard Mouse
keep usb/eth
ssh radxa@192.168.3.2


-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
sudo systemctl stop wpa_supplicant.service
sudo systemctl stop NetworkManager

/etc/sysctl.conf
net.ipv6.conf.all.disable_ipv6 = 1
net.ipv6.conf.default.disable_ipv6 = 1
net.ipv6.conf.lo.disable_ipv6 = 1

plug usb wifi

cd /home/radxa/Projects/ringwfb/tests/modules7
...
  
gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw, width=1920, height=1080, framerate=30/1, format='NV12' ! mpph265enc rc-mode=vbr bps=3000000 bps-max=3172000  ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5600 host=192.168.42.113
