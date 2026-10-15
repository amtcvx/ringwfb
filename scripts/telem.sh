#!/bin/bash

SERDEV=/dev/ttyS4
HOSTIP=127.0.0.1
#HOSTIP=192.168.3.1

sudo socat $SERDEV,b115200 udp4-datagram:$HOSTIP:4244
sudo socat udp4-datagram:$HOSTIP:4245 $SERDEV,b115200
