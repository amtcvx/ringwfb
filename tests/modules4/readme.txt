ISSUE : CAN ONLY WORK ONE SIDE ?

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

3: enp0s25: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UP group default qlen 1000
=> RX KO

-----------------------------------------------------------------------------------------
sudo apt install dwarves

apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio

sudo apt install dwarves

uname -r
=> 6.8.0-060800-generic
cd /usr/lib/modules/6.8.0-060800-generic
ln -s /sys/kernel/btf/vmlinux .
