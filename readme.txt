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
sudo iptables -t mangle -A PREROUTING -p udp --dport 5010 -j TEE --gateway 192.168.1.100

sudo iptables -L -v -n -t mangle

Chain PREROUTING (policy ACCEPT 0 packets, 0 bytes)
 pkts bytes target     prot opt in     out     source               destination         
    0     0 TEE        udp  --  *      *       0.0.0.0/0            0.0.0.0/0            udp dpt:5010 TEE gw:192.168.1.100

Chain INPUT (policy ACCEPT 0 packets, 0 bytes)
 pkts bytes target     prot opt in     out     source               destination         

Chain FORWARD (policy ACCEPT 0 packets, 0 bytes)
 pkts bytes target     prot opt in     out     source               destination         

Chain OUTPUT (policy ACCEPT 0 packets, 0 bytes)
 pkts bytes target     prot opt in     out     source               destination         

Chain POSTROUTING (policy ACCEPT 0 packets, 0 bytes)
 pkts bytes target     prot opt in     out     source               destination    


sudo iptables -t mangle -F
sudo iptables -F
sudo iptables -X

-------------------------------------------------------------------------------
nc -u -vv -l 192.168.4.100 5010
nc -u -vv -l 192.168.4.100 5010
nc -u -vv -l 192.168.1.100 5010

sudo iptables -A INPUT -p udp --sport 5010 -d 192.168.1.1 --dport 5010 -j TEE --gateway 192.168.4.1

sudo iptables -L
Chain INPUT (policy ACCEPT)
target     prot opt source               destination         
TEE        udp  --  anywhere             192.168.1.1          udp spt:5010 dpt:5010 TEE gw:192.168.4.1

sudo iptables -F

-------------------------------------------------------------------------------
#sudo sysctl -w net.ipv4.ip_forward=1
#sudo iptables -t mangle -A POSTROUTING -p udp -d 192.168.4.100 --dport 5010 -j TEE --gateway  192.168.1.100

sudo iptables -t nat -A OUTPUT -p udp -d 192.168.1.100 --dport 5010 -j DNAT --to-destination 192.168.4.100:5010
