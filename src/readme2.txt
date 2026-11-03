EMISSION

sudo ip tuntap add dev tap0 mode tap
sudo ip addr add 192.168.99.1/24 dev tap0
sudo ip link set tap0 up

# 2. Configurer le filtre Traffic Control (tc) pour intercepter le trafic de tap0
sudo tc qdisc add dev tap0 handle ffff: ingress

# 3. Rediriger instantanément TOUS les paquets sortant de tap0 vers wlan0mon
sudo tc filter add dev tap0 parent ffff: protocol all u32 match u32 0 0 action mirred egress redirect dev wlan0mon

sudo ip link set dev tap0 txqueuelen 1000
sudo ip link set dev wlan0mon txqueuelen 1000

sudo ip link set dev tap0 mtu 1400
sudo iw dev wlan0mon set bitrates legacy 5 24

gst-launch-1.0 -v \
    v4l2src device=/dev/video0 ! \
    video/x-raw,width=640,height=480,framerate=30/1 ! \
    videoconvert ! \
    x264enc tune=zerolatency bitrate=800 speed-preset=ultrafast ! \
    rtph264pay config-interval=1 ! \
    udpsink host=192.168.99.2 port=5000 sync=false


RECEPTION

sudo ip tuntap add dev tap0 mode tap
sudo ip addr add 192.168.99.2/24 dev tap0
sudo ip link set tap0 up

# Redirection du flux physique wlan0mon vers l'interface virtuelle tap0
sudo tc qdisc add dev wlan0mon handle ffff: ingress
sudo tc filter add dev wlan0mon parent ffff: protocol all u32 match u32 0 0 action mirred egress redirect dev tap0

gst-launch-1.0 -v \
    udpsrc port=5000 caps="application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264" ! \
    rtph264depay ! \
    h264parse ! \
    avdec_h264 ! \
    autovideosink sync=false
