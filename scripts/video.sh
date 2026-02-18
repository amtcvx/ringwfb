#!/bin/bash

# ON BOARD
# RADXA
gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=NV12  ! mpph265enc bps=1000000 gop=60 qp-max=48 qp-min=8 rc-mode=cbr ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5600 host=127.0.0.1

# ON GROUND
#gst-launch-1.0 udpsrc port=5601 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false
