ip address
=>
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN group default qlen 1000

sudo wireshark -i lo

lkm.c
"
enp0s3 = dev_get_by_name(&init_net,"lo");
"

make
sudo insmod lkm.ko
sudo rmmod lkm.ko
make clean


https://github.com/dmytroshytyi/KERNEL-sk_buff-helloWorld/
