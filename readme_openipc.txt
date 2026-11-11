https://github.com/OpenIPC

git clone https://github.com/OpenIPC/devourer.git

/etc/modprobe.d/blacklist.conf
blacklist rtw88_8812au

/etc/udev/rules.d/99-openipc-devourer.rules
SUBSYSTEM=="usb", ATTR{idVendor}=="0b05", ATTR{idProduct}=="17d2", MODE="0666"

/sys/bus/usb/devices/usb1 => 1-0:1.0, usb2 => 2-0:1.0

sudo DEVOURER_VID=0x0b05 DEVOURER_PID=0x17d2 DEVOURER_USB_BUS=1 DEVOURER_USB_PORT=1 DEVOURER_CHANNEL=149 ./build/txdemo

sudo DEVOURER_VID=0x0b05 DEVOURER_PID=0x17d2 DEVOURER_USB_BUS=1 DEVOURER_USB_PORT=2 DEVOURER_CHANNEL=149 ./build/rxdemo
