/*
g++ -std=c++20 -O2 dual_sync.cpp \
    -I../devourer/src \
    -I../devourer/hal \
    ../devourer/build/libdevourer.a \
    $(pkg-config --cflags --libs libusb-1.0) \
    -lpthread \
    -o dual_sync

sudo apt update && sudo apt install -y openssh-server

Usage: ./dual_sync <ground|drone> [udp_rx_port=5600] [udp_tx_port=5601]


sudo ./dual_sync drone

gst-launch-1.0 videotestsrc is-live=true ! \
  video/x-raw,width=1280,height=720,framerate=30/1 ! \
  x265enc tune=zerolatency bitrate=2000 speed-preset=ultrafast key-int-max=30 ! \
  rtph265pay config-interval=1 mtu=1400 pt=96 ! \
  udpsink host=127.0.0.1 port=5600

#gst-launch-1.0 videotestsrc ! \
#  x265enc tune=zerolatency bitrate=4000 speed-preset=ultrafast key-int-max=30 ! \
#  rtph265pay config-interval=1 mtu=1400 pt=96 ! \
#  udpsink host=127.0.0.1 port=5600


sudo ./dual_sync ground

ssh root@10.5.0.1
#ssh -L 8080:127.0.0.1:80 -L 5760:127.0.0.1:5760 root@10.5.0.1


gst-launch-1.0 udpsrc port=5601 caps="application/x-rtp,media=video,clock-rate=90000,encoding-name=H265,payload=96" ! \
  rtph265depay ! h265parse ! avdec_h265 ! videoconvert ! autovideosink sync=false

#gst-launch-1.0 udpsrc port=5601 caps="application/x-rtp,media=video,encoding-name=H265,payload=96" ! \
#  rtph265depay ! avdec_h265 ! autovideosink sync=false

*/
#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <mutex>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <libusb-1.0/libusb.h>

#include "IRadio.h"
#include "IRtlRadio.h"
#include "RxPacket.h"
#include "WiFiDriver.h"
#include "logger.h"

using namespace std::chrono_literals;

struct [[gnu::packed]] SyncMsg {
    uint32_t magic = 0x44565253; // "DVRS"
    uint8_t  type;               // 1 = PROPOSAL, 2 = COMMIT
    uint8_t  chan;
    uint64_t tx_tsf;
    uint64_t go_tsf;
};

static int open_tun(bool is_drone) {
    int fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (fd < 0) return -1;

    ifreq ifr{};
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    std::strncpy(ifr.ifr_name, "devourer0", IFNAMSIZ);
    if (ioctl(fd, TUNSETIFF, &ifr) < 0) { close(fd); return -1; }

    std::string cmd = std::string("ip addr add ") + (is_drone ? "10.5.0.1/24" : "10.5.0.2/24") +
                      " dev devourer0 && ip link set devourer0 mtu 1400 up";
    std::system(cmd.c_str());
    return fd;
}

static libusb_device_handle* open_adapter(libusb_context* ctx, libusb_device* skip = nullptr) {
    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0) return nullptr;

    libusb_device_handle* found = nullptr;
    for (ssize_t i = 0; i < count; ++i) {
        if (list[i] == skip) continue;
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(list[i], &desc) == 0 &&
            desc.idVendor == 0x0b05 && desc.idProduct == 0x17d2 &&
            libusb_open(list[i], &found) == 0) {
            break;
        }
    }
    libusb_free_device_list(list, 1);
    return found;
}

// Radiotap minimal 8 octets + 802.11 24 octets = 32 octets d'en-tête
static void send_frame(IRadio& r, bool is_drone, uint8_t fc, const void* payload, size_t len) {
    if (len > 1500) return;
    const uint8_t src_id = is_drone ? 0x01 : 0x02;
    uint8_t buf[32 + 1500] = {
        0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,          // Radiotap minimal valide (8 octets)
        fc,   0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // 802.11 FC + DA (broadcast)
        0x57, 0x42, 0x75, 0x05, 0xd6, src_id,                    // SA (0x01 = drone, 0x02 = ground)
        0x57, 0x42, 0x75, 0x05, 0xd6, 0x00, 0x00, 0x00           // BSSID + Seq
    };
    std::memcpy(buf + 32, payload, len);
    if (!r.send_packet(buf, 32 + len)) {
        std::cerr << "[ERR] send_packet rejected frame (fc=0x" << std::hex << int(fc) << std::dec << ")\n";
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv << " <drone|ground> [rx_port] [tx_port]\n";
        return 1;
    }

    const bool is_drone    = (std::string(argv[1]) == "drone");
    const uint16_t rx_port = (argc > 2) ? std::stoi(argv[2]) : 5600;
    const uint16_t tx_port = (argc > 3) ? std::stoi(argv[3]) : 5601;

    int tun_fd = open_tun(is_drone);
    if (tun_fd < 0) { std::cerr << "TUN error (run with sudo)\n"; return 1; }

    int udp_sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    sockaddr_in local_addr{AF_INET, htons(rx_port), {htonl(INADDR_LOOPBACK)}, {}};
    sockaddr_in out_addr{AF_INET, htons(tx_port), {htonl(INADDR_LOOPBACK)}, {}};
    bind(udp_sock, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr));

    libusb_context* ctx = nullptr;
    libusb_init(&ctx);

    libusb_device_handle* h_txrx  = open_adapter(ctx);
    libusb_device_handle* h_scout = h_txrx ? open_adapter(ctx, libusb_get_device(h_txrx)) : nullptr;
    if (!h_txrx || !h_scout) { std::cerr << "Need two 0b05:17d2 adapters\n"; return 1; }

    auto logger = std::make_shared<Logger>();
    WiFiDriver drv(logger);
    auto txrx  = drv.CreateRadio(h_txrx);
    auto scout = drv.CreateRadio(h_scout);
    if (!txrx || !scout) return 1;

    SelectedChannel ch0{.Channel = 149, .ChannelOffset = 0, .ChannelWidth = CHANNEL_WIDTH_20};
    txrx->InitWrite(ch0);
    scout->InitWrite(ch0);

    // État partagé sans aucun blocage USB dans le callback RX
    std::atomic<uint8_t>  cur_chan{149}, best_chan{149}, target_chan{0};
    std::atomic<uint64_t> switch_tsf{0};
    std::mutex sync_mtx;
    std::optional<SyncMsg> pending_sync;

    // Thread RX : pur décodage mémoire (zéro transfert USB synchrone ici !)
    std::thread rx([&] {
        const uint8_t peer_sa[] = {0x57, 0x42, 0x75, 0x05, 0xd6, static_cast<uint8_t>(is_drone ? 0x02 : 0x01)};

        txrx->StartRxLoop([&](const Packet& p) {
            const uint8_t* raw = p.Data.data();
            if (p.Data.size() <= 24 || std::memcmp(raw + 10, peer_sa, 6) != 0) return;

            size_t len = p.Data.size() - 24;
            if (*raw == 0x08) {
                sendto(udp_sock, raw + 24, len, 0, reinterpret_cast<sockaddr*>(&out_addr), sizeof(out_addr));
            } else if (*raw == 0x88) {
                (void)write(tun_fd, raw + 24, len);
            } else if (*raw == 0x40 && len >= sizeof(SyncMsg)) {
                SyncMsg m{};
                std::memcpy(&m, raw + 24, sizeof(m));
                if (m.magic == 0x44565253) {
                    std::lock_guard<std::mutex> lk(sync_mtx);
                    pending_sync = m;
                }
            }
        });
    });

    // Thread Scanner (sur la 2ème clé uniquement)
    std::thread scan([&] {
        auto* rtl = dynamic_cast<IRtlRadio*>(scout.get());
        if (!rtl) return;
        constexpr uint8_t chans[] = {36, 40, 44, 48, 149, 153, 157, 161};
        uint32_t best_noise = UINT32_MAX;

        for (size_t i = 0;; ++i) {
            uint8_t c = chans[i % 8];
            scout->FastRetune(c);
            std::this_thread::sleep_for(15ms);
            (void)rtl->GetRxEnergy(false);
            std::this_thread::sleep_for(80ms);
            auto e = rtl->GetRxEnergy(false);
            uint32_t noise = e.fa_ofdm + e.cca_ofdm;
            if (noise < best_noise || c == best_chan) { best_noise = noise; best_chan = c; }
        }
    });

    // Boucle principale : propriétaire unique du plan de contrôle USB de txrx
    std::array<uint8_t, 1500> buf{};
    auto last_prop = std::chrono::steady_clock::now();

    while (true) {
        ssize_t n;
        while ((n = recv(udp_sock, buf.data(), buf.size(), 0)) > 0)
            send_frame(*txrx, is_drone, 0x08, buf.data(), n);
        while ((n = read(tun_fd, buf.data(), buf.size())) > 0)
            send_frame(*txrx, is_drone, 0x88, buf.data(), n);

        // Traitement d'un éventuel message Sync reçu par le thread RX
        std::optional<SyncMsg> rx_sync;
        {
            std::lock_guard<std::mutex> lk(sync_mtx);
            if (pending_sync) { rx_sync = *pending_sync; pending_sync.reset(); }
        }

        uint64_t now = txrx->ReadTsf();

        if (rx_sync) {
            if (is_drone && rx_sync->type == 1 && rx_sync->chan != cur_chan) {
                uint64_t go = now + 30000ULL;
                SyncMsg commit{0x44565253, 2, rx_sync->chan, now, go};
                send_frame(*txrx, is_drone, 0x40, &commit, sizeof(commit));
                target_chan = rx_sync->chan;
                switch_tsf  = go;
            } else if (!is_drone && rx_sync->type == 2) {
                uint64_t delta = (rx_sync->go_tsf > rx_sync->tx_tsf) ? (rx_sync->go_tsf - rx_sync->tx_tsf) : 20000ULL;
                target_chan = rx_sync->chan;
                switch_tsf  = now + delta;
            }
        }

        if (target_chan != 0 && static_cast<int64_t>(now - switch_tsf) >= 0) {
            uint8_t next = target_chan.exchange(0);
            txrx->FastRetune(next);
            cur_chan = next;
            std::cout << "[" << argv << "] Switched -> ch " << int(next) << "\n";
        }

        if (!is_drone && std::chrono::steady_clock::now() - last_prop > 5s) {
            last_prop = std::chrono::steady_clock::now();
            if (best_chan != cur_chan && target_chan == 0) {
                SyncMsg prop{0x44565253, 1, best_chan, now, 0};
                send_frame(*txrx, is_drone, 0x40, &prop, sizeof(prop));
            }
        }
        std::this_thread::sleep_for(500us);
    }
}

