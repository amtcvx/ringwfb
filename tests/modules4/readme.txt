ISSUE : CAN ONLY WORK ONE SIDE ?

https://satishdotpatel.github.io/maximizing-packet-capture-performance-with-tcpdump/

-----------------------------------------------------------------------------------------
https://github.com/a3f/mitm0/blob/master/mitm.c
https://elixir.bootlin.com/linux/v7.0.12/source/drivers/net/bonding/bond_main.c

https://www.linuxjournal.com/article/7184

https://pages.sdu.dk/sdurobotics/linux-kernels/kernel/-/blob/4f1885a7b347a905cd9ed7deb6472a9688637432/drivers/net/wireless/virt_wifi.c


-----------------------------------------------------------------------------------------
export DEV=enp5s0

sudo tc qdisc delete dev $DEV ingress
sudo tc qdisc show dev $DEV ingress

sudo tc qdisc delete dev $DEV root
sudo tc qdisc show dev $DEV root
=> 
qdisc mq 0: root 
qdisc fq_codel 0: parent :4 limit 10240p flows 1024 quantum 1514 target 5ms interval 100ms memory_limit 32Mb ecn drop_batch 64 
...


sudo tc qdisc add dev $DEV handle ffff: ingress
(same as sudo tc qdisc add dev $DEV ingress)

sudo tc qdisc show dev $DEV ingress
=>
qdisc ingress ffff: parent ffff:fff1 ---------------- 

sudo tc filter add dev $DEV parent ffff: protocol all u32 match u32 0 0 action drop
! ONCE !

sudo tc filter show dev $DEV ingress
=>
filter parent ffff: protocol all pref 49152 u32 chain 0 
filter parent ffff: protocol all pref 49152 u32 chain 0 fh 800: ht divisor 1 
filter parent ffff: protocol all pref 49152 u32 chain 0 fh 800::800 order 2048 key ht 800 bkt 0 terminal flowid not_in_hw 
  match 00000000/00000000 at 0
        action order 1: gact action drop
         random type none pass val 0
         index 1 ref 1 bind 1

gst-launch-1.0 udpsrc port=5600 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !\
  videoconvert ! autovideosink sync=false
=> NOT RECEIVING 
gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420  ! x265enc bitrate=2048 !\
 rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5600 host=192.168.3.200
=> suspend sending
 

sudo tc filter del dev $DEV ingress
sudo tc filter show dev $DEV ingress

sudo tc filter add dev $DEV parent ffff: protocol all u32 match u32 0 0 action police rate 40Mbit burst 10k drop
! ONCE !

sudo tc filter show dev $DEV ingress
=>
filter parent ffff: protocol all pref 49152 u32 chain 0 
filter parent ffff: protocol all pref 49152 u32 chain 0 fh 800: ht divisor 1 
filter parent ffff: protocol all pref 49152 u32 chain 0 fh 800::800 order 2048 key ht 800 bkt 0 terminal flowid not_in_hw 
  match 00000000/00000000 at 0
        action order 1:  police 0x1 rate 40Mbit burst 10Kb mtu 2Kb action drop overhead 0b 
        ref 1 bind 1 

gst-launch-1.0 udpsrc port=5600 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !\
  videoconvert ! autovideosink sync=false
=> RECEIVING

-----------------------------------------------------------------------------------------
https://medium.com/eatclub-tech/traffic-controller-linux-a5a671afc34a

sudo tc qdisc add dev enp5s0 handle ffff: ingress
sudo tc filter add dev enp5s0 parent ffff: protocol ip prio 50 u32 match ip src 0.0.0.0/0 police rate 1mbit burst 32kbit drop flowid :1

sudo tc qdisc del dev enp5s0 root handle 1:
sudo tc qdisc del dev enp5s0 ingress

sudo tc filter add dev enp5s0 parent ffff: \
    protocol ip prio 20 \
    u32 match ip protocol 1 0xff \
    police rate 2kbit buffer 10k drop \
    flowid :1

-----------------------------------------------------------------------------------------
eth0 ingress -> ifb0 outgress

https://developers.redhat.com/articles/2026/04/03/introduction-to-linux-interfaces-for-virtual-networking#

sudo ip link add ifb0 type ifb
sudo ip link set ifb0 up
sudo tc qdisc add dev ifb0 root sfq
(*)
sudo tc qdisc add dev eth0 handle ffff: ingress
sudo tc filter add dev eth0 parent ffff: u32 match u32 0 0 action mirred egress redirect dev ifb0


https://wiki.linuxfoundation.org/networking/ifb

sudo tcpdump -n -i ifb0 -x -e -t

(*)
https://oneuptime.com/blog/post/2026-03-20-ifb-inbound-ipv4-traffic-shaping/view

sudo tc qdisc add dev eth0 ingress
sudo tc filter add dev eth0 parent ffff: protocol ip u32 \
  match u32 0 0 \
  action mirred egress redirect dev ifb0
sudo tc qdisc add dev ifb0 root tbf \
  rate 20mbit \
  burst 32kb \
  latency 400ms

watch -n 1 sudo tc -s qdisc show dev ifb0


sudo tc qdisc del dev eth0 ingress
sudo tc qdisc del dev ifb0 root
sudo ip link del ifb0

-----------------------------------------------------------------------------------------
sudo tc qdisc add dev enp5s0 handle 1: root htb
sudo tc filter add dev enp5s0 parent 1: protocol ip prio 0 u32 match ip src 192.168.0.0/16 match ip dst 192.168.101.0/24 flowid 1:1

sudo tc filter show dev enp5s0
=>
filter parent 1: protocol ip pref 49149 u32 chain 0 
filter parent 1: protocol ip pref 49149 u32 chain 0 fh 803: ht divisor 1 
...

sudo tc filter del dev enp5s0


-----------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------
sudo apt install dwarves

apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio

sudo apt install dwarves

uname -r
=> 6.8.0-060800-generic
cd /usr/lib/modules/6.8.0-060800-generic
ln -s /sys/kernel/btf/vmlinux .
