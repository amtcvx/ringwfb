
sudo ip tuntap add dev tun0 mode tun
sudo ip addr add 10.0.0.1/24 dev tun0
sudo ip link set tun0 up

sudo ip link del tun0



EMISSION

sudo ip tuntap add dev tap0 mode tap
sudo ip addr add 10.0.0.1/24 dev tap0
sudo ip link set tap0 up

sudo tc qdisc add dev tap0 ingress
sudo tc filter add dev tap0 parent ffff: protocol all matchall action mirred egress redirect dev wlx3c7c3fa9bfb6


#sudo ip link set dev tap0 txqueuelen 1000
#sudo ip link set dev wlan0mon txqueuelen 1000

sudo ip link set dev tap0 mtu 1400
sudo iw dev wlan0mon set bitrates legacy 5 24

gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420  ! x265enc bitrate=2048 ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5600 host=10.0.0.1



RECEPTION

sudo ip tuntap add dev tap0 mode tap
sudo ip addr add 10.0.0.2/24 dev tap0
sudo ip link set tap0 up

sudo tc qdisc add dev tap0 clsact
sudo tc filter add dev tap0 imgres_egress prototype all matchall action mirred egress redirect dev wlan@mon


gst-launch-1.0 udpsrc uri=udp://10.0.0.2:5600 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false

sudo ip link del tap0
sudo tc qdisc del dev tap0 ingress
sudo tc qdisc del dev tap0 clsact
