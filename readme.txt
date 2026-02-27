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

sudo iptables -A OUTPUT -p udp -d 127.0.0.1 -j TEE --dport 4000 --gateway 192.168.1.100
(sudo iptables -t mangle -A POSTROUTING -p udp -d 127.0.0.1 --dport 4000 -j TEE --gateway 192.168.1.100)

sudo iptables -t nat -A POSTROUTING -p udp -o lo -j SNAT --to-source 192.168.1.100:4000
(sudo sysctl -w net.ipv4.conf.all.route_localnet=1)

etc ...

-------------------------------------------------------------------------------
Octave1 : 192.168.1.1
sudo iptables -A OUTPUT -p udp -d 192.168.1.100 -j TEE --gateway 192.168.4.1

-------------------------------------------------------------------------------
Octave1 : 192.168.1.1

sudo sysctl -w net.ipv4.ip_forward=1

sudo iptables -t nat -A OUTPUT -p udp -d 192.168.1.100 --dport 4000 -j DNAT --to-destination 192.168.4.100:4000

#sudo iptables -t mangle -A POSTROUTING -p udp -d 192.168.4.100 --dport 4000 -j TEE --gateway 192.168.1.100

gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=NV12  ! mpph265enc bps=1000000 gop=60 qp-max=48 qp-min=8 rc-mode=cbr ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=4000 host=192.168.1.100
sudo nc -u -vv -l 192.168.1.100 4000
sudo nc -u -vv -l 192.168.4.100 4000

-------------------------------------------------------------------------------
sudo iptables -t nat -A OUTPUT -o lo -d 127.0.0.1 -p udp --dport 4000 -j DNAT  --to-destination 192.168.1.100:4000

gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=NV12  ! mpph265enc bps=1000000 gop=60 qp-max=48 qp-min=8 rc-mode=cbr ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=4000 host=127.0.0.1
WARNING: from element /GstPipeline:pipeline0/GstUDPSink:udpsink0: Error sending UDP packets
Additional debug info:
../gst/udp/gstmultiudpsink.c(688): gst_multiudpsink_send_messages (): /GstPipeline:pipeline0/GstUDPSink:udpsink0:
client 127.0.0.1:4000, reason: Error sending message: Invalid argument

sudo sysctl -w net.ipv4.conf.all.route_localnet=1

gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=NV12  ! mpph265enc bps=1000000 gop=60 qp-max=48 qp-min=8 rc-mode=cbr ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=4000 host=127.0.0.1
OK

sudo iptables -L -v -n -t nat
Chain OUTPUT (policy ACCEPT 0 packets, 0 bytes)
 pkts bytes target     prot opt in     out     source               destination         
   62 70797 DNAT       udp  --  *      lo      0.0.0.0/0            127.0.0.1            udp dpt:4000 to:192.168.1.100:4000

sudo iptables -t nat -A POSTROUTING -d 192.168.1.100 -j MASQUERADE

sudo iptables -L -v -n -t nat
Chain OUTPUT (policy ACCEPT 0 packets, 0 bytes)
 pkts bytes target     prot opt in     out     source               destination         
   63 70860 DNAT       udp  --  *      lo      0.0.0.0/0            127.0.0.1            udp dpt:4000 to:192.168.1.100:4000

Chain POSTROUTING (policy ACCEPT 0 packets, 0 bytes)
 pkts bytes target     prot opt in     out     source               destination         
    1    63 MASQUERADE  all  --  *      *       0.0.0.0/0            192.168.1.100

nc -u -vv -l 192.168.1.100 4000
