

make clean;make

sudo dmesg -w

sudo insmod filter.ko

sudo rmmod filter.ko

gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420  ! x265enc bitrate=2048 ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5600 host=127.0.0.1

https://www.linuxjournal.com/article/7184
