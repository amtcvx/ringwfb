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

sudo tc qdisc add dev $DEV handle ffff: ingress
(same as sudo tc qdisc add dev $DEV ingress)

sudo tc qdisc show dev $DEV ingress
=>
qdisc ingress ffff: parent ffff:fff1 ---------------- 

sudo tc filter add dev $DEV parent ffff: protocol ip prio 1 handle 22 fw   action police rate 1000kbit burst 10k drop 

sudo tc filter show dev $DEV ingress
=>
filter parent ffff: protocol ip pref 1 fw chain 0 
filter parent ffff: protocol ip pref 1 fw chain 0 handle 0x16 

        action order 1:  police 0x2 rate 1Mbit burst 10Kb mtu 2Kb action drop overhead 0b 
        ref 1 bind 1 


TODO 


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
