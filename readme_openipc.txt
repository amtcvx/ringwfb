https://github.com/OpenIPC

git clone https://github.com/OpenIPC/devourer.git

/etc/modprobe.d/blacklist.conf
blacklist rtw88_8812au

/etc/udev/rules.d/99-openipc-devourer.rules
SUBSYSTEM=="usb", ATTR{idVendor}=="0b05", ATTR{idProduct}=="17d2", MODE="0666"

/sys/bus/usb/devices/usb1 => 1-0:1.0, usb2 => 2-0:1.0

-------------------------
gst-launch-1.0 -q \
  videotestsrc is-live=true ! \
  video/x-raw,width=1280,height=720,framerate=60/1 ! \
  x265enc tune=zerolatency bitrate=4000 speed-preset=ultrafast key-int-max=30 ! \
  h265parse config-interval=-1 ! \
  rtph265pay pt=96 config-interval=1 mtu=1350 ! \
  fdsink fd=1 sync=false | \
DEVOURER_VID=0x0b05 DEVOURER_PID=0x17d2 DEVOURER_USB_BUS=1 DEVOURER_USB_PORT=1 DEVOURER_CHANNEL=149 DEVOURER_TX_RATE=MCS3/20/LDPC DEVOURER_TX_POWER=20  ./build/streamtx



-------------------------
DEVOURER_VID=0x0b05 DEVOURER_PID=0x17d2 DEVOURER_USB_BUS=1 DEVOURER_USB_PORT=1 DEVOURER_CHANNEL=149 ./build/txdemo

DEVOURER_VID=0x0b05 DEVOURER_PID=0x17d2 DEVOURER_USB_BUS=1 DEVOURER_USB_PORT=2 DEVOURER_CHANNEL=149 ./build/rxdemo


-------------------------------------------------------------------------------
https://github.com/OpenIPC/aviateur

devourer + libsalvage + player
windows and linuw exec


-------------------------------------------------------------------------------
https://github.com/OpenIPC/libsalvage

FlexFEc
Slicer
Depaquetage video (H264,265)
