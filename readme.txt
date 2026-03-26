-------------------------------------------------------------------------------
This project provides wireless communication between ground / board and board / board systems.

-------------------------------------------------------------------------------
For Desktop (ex: GCS) recommended Ubuntu 22.04 (6.8.0-106-generic)

Or

From Ubuntu 24.04 (set ubuntu mainline 6.8)
sudo add-apt-repository ppa:cappelikan/ppa
sudo apt update
sudo apt install mainline
sudo dkms status
(=> none)
mainline install 6.8

-------------------------------------------------------------------------------
git clone http://github.com/amtcvx/ringwfb.git
(git clone git@github.com:amtcvx/ringwfb.git)

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
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
sudo ip link add name br0 type bridge
(sudo ip link del name br0)

#sudo nmcli
#=> br0: non-géré

ip link show br0
ip address show br0
=> 6: br0: <BROADCAST,MULTICAST> 

sudo ip link set dev br0 up
(sudo ip link set dev br0 down)

ip link show br0
ip address show br0
=> 6: br0: <NO-CARRIER,BROADCAST,MULTICAST,UP>

sudo ip address add 10.2.3.4/8 dev br0
sudo ip route append default via 10.0.0.1 dev br0

ip address show br0
pprz@choubaca:~$ ip address show br0
=> 6: br0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 qdisc noqueue state DOWN group default qlen 1000
    link/ether 4e:37:56:dc:75:4f brd ff:ff:ff:ff:ff:ff
    inet 10.2.3.4/8 scope global br0

sudo ip link set dev eth0 master br0
ip link show master br0
=> 2: eno1: <BROADCAST,MULTICAST,UP,LOWER_UP> 

#sudo nmcli
#=> br0: connecté (en externe) à br0

sudo iw dev wlan0 set 4addr on
"4addr" mode aka "WDS" mode, which adds an extra MAC address field to Wi-Fi frames"

sudo ip link set dev wlan0 master br0
ip link show master br0
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel master br0 state UP mode DEFAULT group default qlen 1000
3: wlan0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue master br0 state UP mode DORMANT group default qlen 1000

#sudo nmcli connection show
#sudo nmcli connection del br0

-------------------------------------------------------------------------------
# nmcli connection add type bridge ifname br0 stp no
# nmcli connection add type bridge-slave ifname enp30s0 master br0
# nmcli connection down Connection
# nmcli connection up bridge-br0
# nmcli connection up bridge-slave-enp30s0
# nmcli connection modify Connection connection.autoconnect no

-------------------------------------------------------------------------------
socat udp-recv:4000 -

echo hello | socat - UDP-DATAGRAM:192.168.1.100:4000
echo hello | socat - UDP-DATAGRAM:192.168.1.200:4000
echo hello | socat - UDP-DATAGRAM:192.168.1.255:4000,broadcast
