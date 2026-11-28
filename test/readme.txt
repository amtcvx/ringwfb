gst-launch-1.0 v4l2src device=/dev/video0 ! \
  video/x-raw,format=YUY2,width=640,height=480,framerate=30/1 ! \
  videoconvert ! video/x-raw,format=I420 ! \
  x265enc tune=zerolatency bitrate=2500 speed-preset=ultrafast \
    option-string=\"keyint=30:repeat-headers=1:bframes=0:scenecut=0\" ! \
  rtph265pay config-interval=1 mtu=1350 aggregate-mode=zero-latency ! \
  udpsink host=192.168.1.29 port=5600 sync=false
