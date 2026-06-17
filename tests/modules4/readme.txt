ISSUE : CAN ONLY WORK ONE SIDE ?

https://satishdotpatel.github.io/maximizing-packet-capture-performance-with-tcpdump/

-----------------------------------------------------------------------------------------
https://github.com/a3f/mitm0/blob/master/mitm.c
https://elixir.bootlin.com/linux/v7.0.12/source/drivers/net/bonding/bond_main.c

https://www.linuxjournal.com/article/7184

https://pages.sdu.dk/sdurobotics/linux-kernels/kernel/-/blob/4f1885a7b347a905cd9ed7deb6472a9688637432/drivers/net/wireless/virt_wifi.c

https://oneuptime.com/blog/post/2026-03-20-remove-tc-qdisc-rules-ipv4/view
sudo tc qdisc show dev eth0
sudo tc class show dev eth0a
sudo tc filter show dev eth0

-----------------------------------------------------------------------------------------
2: enp5s0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP group default qlen 1000
=> RX OK
num_rx_queues: 8

3: enp0s25: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UP group default qlen 1000
=> RX KO
num_rx_queues: 1

-----------------------------------------------------------------------------------------
sudo tc qdisc del dev eth0 root
sudo tc qdisc show dev eth0

fq_codel or fifo_fasta
sudo tc qdisc replace dev eth0 root fq_codel
sudo tc qdisc replace dev eth0 root fq
sudo tc qdisc replace dev eth0 root pfifo_fast
sudo tc qdisc replace dev eth0 root pfifo
sudo tc qdisc replace dev eth0 root sfq
sudo tc qdisc replace dev eth0 root tbf rate 220kbit latency 50ms burst 1540

sudo tc qdisc replace dev eth0 root tbf rate 5000bit burst 32kbit latency 1ms
watch tc -s qdisc show dev eth0


sudo tc qdisc replace dev eth0 root handle 1: htb default 2
sudo tc class add dev eth0 parent 1: classid 1:1 htb rate 2.4mbit
watch -n 1 'tc -s class show dev eth0'

sudo tc qdisc replace dev eth0 root handle 1: htb default 1
sudo tc class replace dev eth0 parent 1: classid 1:1 htb rate 100kbit

sudo tc qdisc replace dev eth0 handle ffff: ingress
sudo tc filter add dev eht0 parent ffff: protocol ip prio 50 u32 match ip dport 80 0xffff police rate 50Mbit burst 10m drop flowid :1

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
-----------------------------------------------------------------------------------------
sudo apt install dwarves

apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio

sudo apt install dwarves

uname -r
=> 6.8.0-060800-generic
cd /usr/lib/modules/6.8.0-060800-generic
ln -s /sys/kernel/btf/vmlinux .
