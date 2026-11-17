clear;make clean;make
sudo cp *.ko /lib/modules/7.0.0-38-generic/
sudo depmod -A

sudo modprobe mymodule 
sudo modprobe r mymodule 


gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420  ! x265enc bitrate=2048 ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5600 host=127.0.0.1

gst-launch-1.0 udpsrc uri=udp://127.0.0.1:5700 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false


RATES
-----
gst-launch-1.0 udpsrc uri=udp://127.0.0.1:5600  ! identity drop-allocation=true ! fdsink fd=1 | pv > /dev/null

gst-launch-1.0 videotestsrc ...  x265enc bitrate=1
[3,83KiB/s]

gst-launch-1.0 videotestsrc ...  x265enc bitrate=102400 
[2,30MiB/s]

REMOTE
------
gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420  ! x265enc bitrate=2048 ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5600 host=192.168.1.214

gst-launch-1.0 udpsrc uri=udp://192.168.1.113:5600 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false

gst-launch-1.0 udpsrc uri=udp://0.0.0.0:5600 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false

LOCAL
-----
gst-launch-1.0 videotestsrc ... host=127.0.0.1

gst-launch-1.0 udpsrc uri=udp://127.0.0.1:5600  ...

CAMERA
------
v4l2-ctl --device=/dev/video0 --list-formats-ext
...
[1]: 'YUYV' (YUYV 4:2:2)
  Size: Discrete 640x480
  Interval: Discrete 0.033s (30.000 fps)

gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=YUY2 ! videoconvert ! autovideosink

gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=YUY2 ! videoconvert ! x265enc ... 


CHANNELS / FREQ
----------------
0	1	2412
1	2	2417
2	3	2422
3	4	2427
4	5	2432
5	6	2437
6	7	2442
7	8	2447
8	9	2452
9	10	2457
10	11	2462
11	36	5180
12	40	5200
13	44	5220
14	48	5240
15	52	5260
16	56	5280
17	60	5300
18	64	5320
19	100	5500
20	104	5520
21	108	5540
22	112	5560
23	116	5580
24	120	5600
25	124	5620
26	128	5640
27	132	5660
28	136	5680
29	140	5700
30	144	5720
31	149	5745
32	153	5765
33	157	5785
34	161	5805
35	165	5825

