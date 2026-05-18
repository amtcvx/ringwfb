https://dev.to/zqiu/netlink-communication-between-kernel-and-user-space-2mg1

apt install dwarves

make
make client
sudo insmod netlink_kernel.ko
sudo dmesg -Hw
./netlink_client
sudo rmmod netlink_kernel.ko
