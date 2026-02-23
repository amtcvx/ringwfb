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
#sudo sysctl -w net.ipv4.ip_forward=1
#sudo iptables -t mangle -A POSTROUTING -p udp -d 192.168.4.100 --dport 5010 -j TEE --gateway  192.168.1.100

sudo iptables -t nat -A OUTPUT -p udp -d 192.168.1.100 --dport 5010 -j DNAT --to-destination 192.168.4.100:5010
