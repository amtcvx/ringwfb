obj-m += netlink_kernel.o

#generate the path
CURRENT_PATH:=$(shell pwd)

#the current kernel version number
LINUX_KERNEL:=$(shell uname -r)

#the absolute path
LINUX_KERNEL_PATH:=/usr/src/linux-headers-$(LINUX_KERNEL)

all:
	@echo $(LINUX_KERNEL_PATH)
	make -C $(LINUX_KERNEL_PATH) M=$(CURRENT_PATH) modules

client:
	gcc netlink_client.c -o netlink_client -g

#clean
clean:
	make -C $(LINUX_KERNEL_PATH) M=$(CURRENT_PATH) clean
	rm netlink_client
