/*
g++ -std=c++20 -O2 dual_sync.cpp \
    -I../devourer/src \
    -I../devourer/hal \
    ../devourer/build/libdevourer.a \
    $(pkg-config --cflags --libs libusb-1.0) \
    -lpthread \
    -o dual_sync

Usage: ./dual_sync <ground|drone> [udp_rx_port=5600] [udp_tx_port=5601]


sudo ./dual_sync drone

gst-launch-1.0 videotestsrc ! \
  x265enc tune=zerolatency bitrate=4000 speed-preset=ultrafast key-int-max=30 ! \
  rtph265pay config-interval=1 mtu=1400 pt=96 ! \
  udpsink host=127.0.0.1 port=5600


sudo ./dual_sync ground

ssh root@10.5.0.1
#ssh -L 8080:127.0.0.1:80 -L 5760:127.0.0.1:5760 root@10.5.0.1

gst-launch-1.0 udpsrc port=5601 caps="application/x-rtp,media=video,encoding-name=H265,payload=96" ! \
  rtph265depay ! avdec_h265 ! autovideosink sync=false

*/
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <mutex>
#include <libusb-1.0/libusb.h>

#include "WiFiDriver.h"
#include "IRadio.h"
#include "IRtlRadio.h"
#include "RxPacket.h"
#include "UsbOpen.h"
#include "logger.h"

using namespace std::chrono_literals;

std::mutex txrx_usb_mtx;

static constexpr uint16_t vid_pid[2][2] = {
    {0x0b05, 0x17d2}, // ASUS USB-AC56
    {0x0b05, 0x17d2}
};

// Creates & configures a Linux TUN interface (Drone = 10.5.0.1/24, Ground = 10.5.0.2/24)
static int open_tun(bool is_drone) {
    int fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (fd < 0) return -1;
    ifreq ifr{};
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    std::strncpy(ifr.ifr_name, "devourer0", IFNAMSIZ);
    if (ioctl(fd, TUNSETIFF, &ifr) < 0) { close(fd); return -1; }

    const char* ip = is_drone ? "10.5.0.1/24" : "10.5.0.2/24";
    std::string cmd = std::string("ip addr add ") + ip + " dev devourer0 && ip link set devourer0 mtu 1400 up";
    std::system(cmd.c_str());
    return fd;
}

libusb_device_handle* get_available_adapter(libusb_context* ctx) {
    libusb_device** list = nullptr;
    ssize_t count = 0;
    if ((count = libusb_get_device_list(ctx, &list)) < 0) return nullptr;
    libusb_device_handle* claimed_handle = nullptr;
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device* dev = list[i];
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(dev, &desc) != 0) continue;
        if (!(desc.idVendor == vid_pid[0][0] && desc.idProduct == vid_pid[0][1]) &&
            !(desc.idVendor == vid_pid[1][0] && desc.idProduct == vid_pid[1][1])) continue;
        libusb_device_handle* handle = nullptr;
        if (libusb_open(dev, &handle) != 0) continue;
        if (libusb_kernel_driver_active(handle, 0) == 1) libusb_detach_kernel_driver(handle, 0);
        if (libusb_claim_interface(handle, 0) == 0) {
            claimed_handle = handle;
            break;
        } else libusb_close(handle);
    }
    libusb_free_device_list(list, 1);
    return claimed_handle;
}

#pragma pack(push, 1)
struct SyncMsg {
    uint32_t magic = 0x44565253; // "DVRS"
    uint8_t  type;               // 1 = PROPOSAL (Ground->Drone), 2 = COMMIT (Drone->Ground)
    uint8_t  chan;
    uint64_t tx_tsf;
    uint64_t go_tsf;
};
#pragma pack(pop)

// Frame types via 802.11 Frame Control byte 0:
//   0x08 = H.265 UDP Video | 0x88 = IP/SSH TUN Packet | 0x40 = TSF Channel Sync
static void send_frame(IRadio& r, uint8_t fc, const void* payload, size_t len) {
    uint8_t hdr[] = {
        0x00,0x00,0x0d,0x00, 0x00,0x80,0x08,0x00, 0x08,0x00,0x07,0x00,0x03, // Radiotap: MCS3
        fc,  0x00,0x00,0x00, 0xff,0xff,0xff,0xff,0xff,0xff,                // 802.11 FC + DA
        0x57,0x42,0x75,0x05,0xd6,0x00, 0x57,0x42,0x75,0x05,0xd6,0x00, 0x00,0x00
    };
    uint8_t buf[sizeof(hdr) + 1500];
    if (len > 1500) return;
    std::memcpy(buf, hdr, sizeof(hdr));
    std::memcpy(buf + sizeof(hdr), payload, len);
    std::lock_guard<std::mutex> lk(txrx_usb_mtx);
    r.send_packet(buf, sizeof(hdr) + len);
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    const bool is_drone    = (std::string(*(argv + 1)) == "drone");
    const uint16_t rx_port = (argc > 2) ? static_cast<uint16_t>(std::stoi(*(argv + 2))) : 5600;
    const uint16_t tx_port = (argc > 3) ? static_cast<uint16_t>(std::stoi(*(argv + 3))) : 5601;

    // 1. Setup TUN IP tunnel (for SSH) + non-blocking UDP sockets (for H.265 video)
    int tun_fd = open_tun(is_drone);
    if (tun_fd < 0) { std::cerr << "Failed to open /dev/net/tun (run with sudo)\n"; return 1; }

    int udp_sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    sockaddr_in local_addr{}, out_addr{};
    local_addr.sin_family      = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local_addr.sin_port        = htons(rx_port);
    bind(udp_sock, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr));

    out_addr.sin_family      = AF_INET;
    out_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    out_addr.sin_port        = htons(tx_port);

    // 2. Claim two 0b05:17d2 adapters via get_available_adapter()
    libusb_context* ctx = nullptr;
    libusb_init(&ctx);
    libusb_device_handle* h_txrx  = get_available_adapter(ctx);
    libusb_device_handle* h_scout = get_available_adapter(ctx);
    if (!h_txrx || !h_scout) { std::cerr << "Need two 0b05:17d2 adapters\n"; return 1; }

    auto logger = std::make_shared<Logger>();
    std::shared_ptr<devourer::UsbDeviceLock> lock_txrx, lock_scout;
    devourer::claim_interface_then_reset(h_txrx, 0, logger, true, lock_txrx, "txrx");
    devourer::claim_interface_then_reset(h_scout, 0, logger, true, lock_scout, "scout");

    WiFiDriver drv(logger);
    auto txrx_ptr  = drv.CreateRadio(h_txrx);  // Adapter 0: Duplex TX/RX (H.265 + SSH TUN + Sync)
    auto scout_ptr = drv.CreateRadio(h_scout); // Adapter 1: Background scanner
    if (!txrx_ptr || !scout_ptr) return 1;
    auto& txrx  = *txrx_ptr;
    auto& scout = *scout_ptr;

    std::atomic<uint8_t>  cur_chan{149}, best_chan{149}, target_chan{0};
    std::atomic<uint64_t> switch_tsf{0};

    SelectedChannel ch0{.Channel = 149, .ChannelOffset = 0, .ChannelWidth = CHANNEL_WIDTH_20};
    txrx.InitWrite(ch0);
    scout.InitWrite(ch0);

    // 3. Adapter 0 RX thread: demuxes H.265 UDP (0x08), SSH/IP TUN (0x88), and TSF Sync (0x40)
    std::thread rx([&] {
        static constexpr uint8_t SA[] = {0x57, 0x42, 0x75, 0x05, 0xd6, 0x00};
        txrx.StartRxLoop([&](const Packet& p) {
            const uint8_t* raw = p.Data.data();
            size_t raw_len = p.Data.size();
            if (raw_len <= 28 || std::memcmp(raw + 10, SA, 6) != 0) return;
            size_t payload_len = raw_len - 24 - 4;

            if (*raw == 0x08) { // H.265 UDP Video
                sendto(udp_sock, raw + 24, payload_len, 0,
                       reinterpret_cast<sockaddr*>(&out_addr), sizeof(out_addr));
                return;
            }
            if (*raw == 0x88) { // IP / SSH Tunnel packet -> inject into Linux kernel TUN
                (void)write(tun_fd, raw + 24, payload_len);
                return;
            }
            if (*raw == 0x40 && payload_len >= sizeof(SyncMsg)) { // TSF Channel Sync
                SyncMsg m{};
                std::memcpy(&m, raw + 24, sizeof(m));
                if (m.magic != 0x44565253) return;

                uint64_t now = 0;
                {
                  std::lock_guard<std::mutex> lk(txrx_usb_mtx);
                  now = txrx.ReadTsf();
                }
                if (is_drone && m.type == 1 && m.chan != cur_chan) {
                    uint64_t go = now + 30000ULL;
                    SyncMsg commit{0x44565253, 2, m.chan, now, go};
                    send_frame(txrx, 0x40, &commit, sizeof(commit));
                    target_chan = m.chan;
                    switch_tsf  = go;
                } else if (!is_drone && m.type == 2) {
                    uint64_t delta = (m.go_tsf > m.tx_tsf) ? (m.go_tsf - m.tx_tsf) : 20000ULL;
                    target_chan = m.chan;
                    switch_tsf  = now + delta;
                }
            }
        });
    });

    // 4. Adapter 1 Scanner thread: sweeps channels via FastRetune + GetRxEnergy
    std::thread scan([&] {
        auto* rtl_scout = dynamic_cast<IRtlRadio*>(&scout);
        if (!rtl_scout) return;
        const uint8_t chans[] = {36, 40, 44, 48, 149, 153, 157, 161};
        uint32_t best_noise = UINT32_MAX;
        for (size_t i = 0;; ++i) {
            uint8_t c = chans[i % 8];
            scout.FastRetune(c);
            std::this_thread::sleep_for(15ms);
            (void)rtl_scout->GetRxEnergy(false);
            std::this_thread::sleep_for(80ms);
            auto e = rtl_scout->GetRxEnergy(false);
            uint32_t noise = e.fa_ofdm + e.cca_ofdm;
            if (noise < best_noise || c == best_chan) { best_noise = noise; best_chan = c; }
        }
    });

    // 5. Main loop: forwards H.265 UDP + SSH TUN packets -> RF TX + executes TSF retunes
    std::array<uint8_t, 1500> io_buf{};
    auto last_prop = std::chrono::steady_clock::now();
    while (true) {
        ssize_t n;
        while ((n = recv(udp_sock, io_buf.data(), io_buf.size(), 0)) > 0) {
          send_frame(txrx, 0x08, io_buf.data(), static_cast<size_t>(n));
        }
        while ((n = read(tun_fd, io_buf.data(), io_buf.size())) > 0) {
          send_frame(txrx, 0x88, io_buf.data(), static_cast<size_t>(n));
        }

        uint64_t now = 0;
        {
          std::lock_guard<std::mutex> lk(txrx_usb_mtx);
          now = txrx.ReadTsf();
        }
        if (target_chan != 0 && now >= switch_tsf) {
            uint8_t next = target_chan.exchange(0);
	    {
              std::lock_guard<std::mutex> lk(txrx_usb_mtx);
              txrx.FastRetune(next);
	    }
            cur_chan = next;
            std::cout << "[" << *(argv + 1) << "] Switched -> ch " << int(next) << "\n";
        }

        if (!is_drone && std::chrono::steady_clock::now() - last_prop > 5s) {
            last_prop = std::chrono::steady_clock::now();
            if (best_chan != cur_chan && target_chan == 0) {
                SyncMsg prop{0x44565253, 1, best_chan, now, 0};
                send_frame(txrx, 0x40, &prop, sizeof(prop));
            }
        }
        std::this_thread::sleep_for(200us);
    }
}

