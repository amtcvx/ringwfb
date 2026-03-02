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
sudo iptables -t nat -F
sudo iptables -F
sudo iptables -X

sudo iptables -t mangle -F;sudo iptables -t nat -F;sudo iptables -F;sudo iptables -X


sudo iptables -L -v
sudo iptables -L -v -n -t mangle
sudo iptables -L -v -n -t nat

-------------------------------------------------------------------------------
# (APPLICATION)->-[OUTPUT]->--[POSTROUTING]->-(PACKET OUT)
#                  -mangle        -mangle
#                  -nat (src)     -nat (dst)
#                  -filter
#
-------------------------------------------------------------------------------
Octave1 : 192.168.1.1
GCS : 192.168.1.200

gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=NV12  ! mpph265enc bps=1000000 gop=60 qp-max=48 qp-min=8 rc-mode=cbr ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=4000 host=127.0.0.1
(SRC 127.0.0.1, DST 127.0.0.1)

sudo iptables -t nat -A OUTPUT -d 127.0.0.1 -p udp --dport 4000 -j DNAT --to 192.168.1.200:4000
(SRC 127.0.0.1, DST 192.168.1.200)
sudo iptables -t nat -A POSTROUTING -d 192.168.1.200 -p udp --dport 4000 -j SNAT --to-source 192.168.1.2
(SRC 192.168.1.2, DST 192.168.1.200)

sudo sysctl -w net.ipv4.conf.all.route_localnet=1
(enable to route packets with src of 127.0.0.1)

sudo iptables -L -v -t nat
Chain OUTPUT (policy ACCEPT 0 packets, 0 bytes)
 pkts bytes target     prot opt in     out     source               destination         
    1    63 DNAT       udp  --  any    any     anywhere             localhost            udp dpt:4000 to:192.168.1.200:4000

Chain POSTROUTING (policy ACCEPT 0 packets, 0 bytes)
 pkts bytes target     prot opt in     out     source               destination         
    1    63 SNAT       udp  --  any    any     anywhere             192.168.1.200        udp dpt:4000 to:192.168.1.2

nc -u -vv -l 192.168.1.200 4000

-------------------------------------------------------------------------------
Octave1 : 192.168.1.1, 192.168.1.2
GCS : 192.168.1.100, 192.168.1.200

sudo iptables -t nat -A OUTPUT -d 127.0.0.1 -p udp --dport 4000 -j TEE --gateway 192.168.1.100
TBC ...

-------------------------------------------------------------------------------
Octave1 : 192.168.1.1

sudo iptables -A OUTPUT -p udp -d 127.0.0.1 -j TEE --dport 4000 --gateway 192.168.1.100
(sudo iptables -t mangle -A POSTROUTING -p udp -d 127.0.0.1 --dport 4000 -j TEE --gateway 192.168.1.100)

sudo iptables -t nat -A POSTROUTING -p udp -o lo -j SNAT --to-source 192.168.1.100:4000
(sudo sysctl -w net.ipv4.conf.all.route_localnet=1)

etc ...
