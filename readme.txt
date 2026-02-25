This project provides wireless communication between ground / board and board / board systems.

-------------------------------------------------------------------------------
git clone --recurse-submodules  http://github.com/amtcvx/ringwfb.git
(git clone --recurse-submodules  git@github.com:amtcvx/ringwfb.git)

Installation need internet connection and sudo 
./install.sh
(./uninstall.sh)

-------------------------------------------------------------------------------
Before first run, set Makefile according to the target role

For unique ground control station set
  ROLEFLAG += -DDRONEID=0
For each boards set
  ROLEFLAG += -DDRONEID=1
  ...

make clean;make all

-------------------------------------------------------------------------------
bin/wfb

nc -u -vv -l 5000

-------------------------------------------------------------------------------
sudo iptables -t mangle -F
sudo iptables -F
sudo iptables -X

sudo iptables -L
sudo iptables -L -v -n -t mangle

-------------------------------------------------------------------------------
sudo iptables -t mangle -I PREROUTING -j TEE --gateway 192.168.1.1
sudo iptables -t mangle -I POSTROUTING -j TEE --gateway 192.168.1.1
sudo iptables -t mangle -S

-------------------------------------------------------------------------------
Port 8001 to 8002
-----------------
sudo iptables -A PREROUTING -t mangle -p udp ! -s 127.0.0.1 --dport 8001 -j TEE --gateway 127.0.0.1
sudo iptables -A OUTPUT -t nat -p udp -s 127.0.0.1/32 --dport 8001 -j DNAT --to 127.0.0.1:8002

sudo iptables -L -v -n -t mangle
Chain PREROUTING (policy ACCEPT 0 packets, 0 bytes)
 pkts bytes target     prot opt in     out     source               destination         
    0     0 TEE        udp  --  *      *      !127.0.0.1            0.0.0.0/0            udp dpt:8001 TEE gw:127.0.0.1

gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=NV12  ! mpph265enc bps=1000000 gop=60 qp-max=48 qp-min=8 rc-mode=cbr ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=8001 host=127.0.0.1
sudo nc -u -vv -l 127.0.0.1 8002
gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=NV12  ! mpph265enc bps=1000000 gop=60 qp-max=48 qp-min=8 rc-mode=cbr ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=8002 host=127.0.0.1
sudo nc -u -vv -l 127.0.0.1 8002

-------------------------------------------------------------------------------
sudo iptables -t mangle -A PREROUTING -p udp --dport 1935 -d 192.168.1.1 -j TEE --gateway 192.168.4.100
sudo iptables -t nat -A PREROUTING -p udp --dport 1935 -d 192.168.1.1 -j DNAT --to-destination 192.168.4.1:1935  

sudo iptables -L -v -n -t mangle
Chain PREROUTING (policy ACCEPT 0 packets, 0 bytes)
 pkts bytes target     prot opt in     out     source               destination         
    0     0 TEE        udp  --  *      *       0.0.0.0/0            192.168.1.100        udp dpt:1935 TEE gw:192.168.4.100


sudo iptables -t mangle -A PREROUTING -p udp --dport 1935 -d 192.168.1.1 -j TEE --gateway 192.168.4.100

-------------------------------------------------------------------------------
sudo iptables -t mangle -A PREROUTING  -p udp --dport 60000 -j TEE --gateway 192.168.1.1
=> 
loop => FULL CPU !  (htop)

-------------------------------------------------------------------------------
sudo sysctl -w net.ipv4.ip_forward=1

sudo iptables -t nat -A OUTPUT -p udp -d 192.168.1.1 --dport 4000 -j DNAT --to-destination 192.168.4.1
sudo iptables -t mangle -A POSTROUTING -p udp -d 192.168.4.1 --dport 4000 -j TEE --gateway 192.168.1.1

sudo iptables -L -v -n -t nat
Chain OUTPUT (policy ACCEPT 0 packets, 0 bytes)
 pkts bytes target     prot opt in     out     source               destination         
    0     0 DNAT       udp  --  *      *       0.0.0.0/0            192.168.1.1          udp dpt:4000 to:192.168.4.1

sudo iptables -L -v -n -t mangle
Chain POSTROUTING (policy ACCEPT 0 packets, 0 bytes)
 pkts bytes target     prot opt in     out     source               destination         
    0     0 TEE        udp  --  *      *       0.0.0.0/0            192.168.4.1          udp dpt:4000 TEE gw:192.168.1.1


gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=NV12  ! mpph265enc bps=1000000 gop=60 qp-max=48 qp-min=8 rc-mode=cbr ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=4000 host=192.168.1.100
gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=NV12  ! mpph265enc bps=1000000 gop=60 qp-max=48 qp-min=8 rc-mode=cbr ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=4000 host=192.168.4.100
sudo nc -u -vv -l 192.168.4.100 4000
