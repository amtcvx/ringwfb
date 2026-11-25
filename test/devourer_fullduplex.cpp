/*
g++ -std=c++20 -O2 devourer_fullduplex.cpp \
    -I../devourer/src \
    -I../devourer/hal \
    ../devourer/build/libdevourer.a \
    $(pkg-config --cflags --libs libusb-1.0) \
    -lpthread \
    -o devourer_fullduplex

./devourer_fullduplex 1 149 5601 5600

gst-launch-1.0 videotestsrc is-live=true ! \
  x265enc tune=zerolatency speed-preset=ultrafast ! \
  rtph265pay config-interval=-1 mtu=1500 ! \
  udpsink host=127.0.0.1 port=5601 sync=false async=false

#./devourer_fullduplex 2 149 5600 5601

#gst-launch-1.0 udpsrc address=127.0.0.1 port=5600 mtu=1500 buffer-size=0 \
  caps="application/x-rtp,media=video,clock-rate=90000,encoding-name=H265" ! \
  rtph265depay ! avdec_h265 ! autovideosink sync=false

*/
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <arpa/inet.h>
#include <libusb-1.0/libusb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>

#include "DeviceConfig.h"
#include "IRadio.h"
#include "RxPacket.h"
#include "SelectedChannel.h"
#include "UsbOpen.h"
#include "WiFiDriver.h"
#include "logger.h"

namespace {
constexpr uint16_t USB_VID       = 0x0b05; // ASUS USB-AC56
constexpr uint16_t USB_PID       = 0x17d2; // RTL8812AU (Jaguar1)
constexpr size_t BATCH_SIZE      = 16;
constexpr size_t MAX_RTP_PAYLOAD = 1500;   // rtph265pay config-interval=-1 mtu=1500

// En-tête Radiotap minimal (13 octets : MCS 2, 20 MHz, Long GI)
constexpr std::array<uint8_t, 13> RADIOTAP_HDR = {
    0x00, 0x00,             // Version 0, pad 0
    0x0d, 0x00,             // Longueur = 13 octets
    0x00, 0x00, 0x08, 0x00, // Present flags : IEEE80211_RADIOTAP_MCS (bit 19)
    0x07,                   // Known : BW + MCS + GI
    0x00,                   // Flags : 20 MHz, Long GI
    0x02,                   // MCS index = 2 (QPSK 3/4)
    0x00, 0x00              // Alignement
};

// Préfixe MAC 5 octets partagé (le 6e octet est le node_id)
constexpr std::array<uint8_t, 5> MAC_PREFIX = {0x57, 0x42, 0x75, 0x05, 0xd6};

// En-tête 802.11 Data (24 octets)
constexpr std::array<uint8_t, 24> IEEE80211_HDR_TEMPLATE = {
    0x08, 0x01, 0x00, 0x00,             // Frame Control (Data), Duration
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // Addr1 (Broadcast)
    0x57, 0x42, 0x75, 0x05, 0xd6, 0x01, // Addr2 (SA : préfixe + node_id)
    0x57, 0x42, 0x75, 0x05, 0xd6, 0x00, // Addr3 (BSSID)
    0x00, 0x00                          // Sequence Control
};

constexpr size_t TX_HDR_LEN    = RADIOTAP_HDR.size() + IEEE80211_HDR_TEMPLATE.size(); // 37 octets
constexpr size_t BUFFER_STRIDE = 1600; // Multiple de 64 octets (> 37 + 1500)

std::atomic<bool> g_running{true};
std::atomic<uint64_t> g_tx_pkts{0};
std::atomic<uint64_t> g_rx_pkts{0};

void setRealtimeCpu(int core) noexcept {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    sched_param param{.sched_priority = 50};
    pthread_setschedparam(pthread_self(), SCHED_RR, &param);
}
} // namespace

int main(int argc, char* argv[]) {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigset, nullptr);

    // Usage : ./devourer_fullduplex [node_id=1] [channel=149] [tx_port=5601] [rx_port=5600]
    const uint8_t local_node_id = (argc > 1) ? static_cast<uint8_t>(std::atoi(argv[1])) : 1;
    const int channel           = (argc > 2) ? std::atoi(argv[2]) : 149;
    const uint16_t tx_port      = (argc > 3) ? static_cast<uint16_t>(std::atoi(argv[3])) : 5601;
    const uint16_t rx_port      = (argc > 4) ? static_cast<uint16_t>(std::atoi(argv[4])) : 5600;

    libusb_context* usb_ctx = nullptr;
    if (libusb_init(&usb_ctx) < 0) {
        std::cerr << "[Fatal] libusb_init échoué.\n";
        return 1;
    }

    libusb_device_handle* handle = libusb_open_device_with_vid_pid(usb_ctx, USB_VID, USB_PID);
    if (!handle) {
        std::cerr << "[Fatal] Adaptateur 0b05:17d2 introuvable.\n";
        libusb_exit(usb_ctx);
        return 1;
    }

    auto logger = std::make_shared<Logger>();
    std::shared_ptr<devourer::UsbDeviceLock> usb_lock;

    if (devourer::claim_interface_then_reset(handle, 0, logger, true, usb_lock, "0b05:17d2") != 0) {
        std::cerr << "[Fatal] Échec claim_interface sur 0b05:17d2.\n";
        libusb_close(handle);
        libusb_exit(usb_ctx);
        return 1;
    }

    WiFiDriver driver(logger);
    devourer::DeviceConfig cfg;

    auto radio = driver.CreateRadio(handle, usb_ctx, nullptr, cfg);
    if (!radio) {
        std::cerr << "[Fatal] WiFiDriver::CreateRadio a échoué.\n";
        usb_lock.reset();
        libusb_release_interface(handle, 0);
        libusb_close(handle);
        libusb_exit(usb_ctx);
        return 1;
    }

    SelectedChannel sel_chan{
        .Channel       = static_cast<uint8_t>(channel),
        .ChannelOffset = 0,
        .ChannelWidth  = CHANNEL_WIDTH_20,
    };

    // Initialisation Full Duplex (TX + RX simultanés)
    radio->InitWrite(sel_chan);

    // Voie RX (Air -> UDP 127.0.0.1:rx_port vers GStreamer udpsrc)
    int rx_sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(rx_port);
    inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);
    connect(rx_sock, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));

    auto rx_callback = [rx_sock, local_node_id](const Packet& pkt) noexcept {
        const size_t sz = pkt.Data.size();
        if (sz < 36) [[unlikely]] return;

        const uint8_t* frame = pkt.Data.data();

        // Filtre le préfixe MAC OpenIPC (57:42:75:05:d6)
        if (std::memcmp(frame + 10, MAC_PREFIX.data(), MAC_PREFIX.size()) != 0) [[unlikely]] {
            return;
        }

        // Ignore les paquets émis par ce même nœud
        if (frame[15] == local_node_id) [[unlikely]] {
            return;
        }

        const size_t hdr_len = ((frame[0] & 0x80) != 0) ? 26 : 24;
        if (sz <= hdr_len + 12) [[unlikely]] return;

        const uint8_t* rtp_ptr = frame + hdr_len;
        const size_t rtp_len   = sz - hdr_len;

        // Vérifie RTP v2
        if ((rtp_ptr[0] & 0xC0) != 0x80) [[unlikely]] return;

        if (rtp_len <= MAX_RTP_PAYLOAD) [[likely]] {
            if (send(rx_sock, rtp_ptr, rtp_len, MSG_DONTWAIT) > 0) {
                g_rx_pkts.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::thread rx_thread([&radio, &rx_callback]() {
        radio->StartRxLoop(rx_callback);
    });

    // Voie TX Zero-Copy par lots (GStreamer udpsink :tx_port -> recvmmsg -> send_packets)
    int tx_sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);

    int busy_poll = 50;
    setsockopt(tx_sock, SOL_SOCKET, SO_BUSY_POLL, &busy_poll, sizeof(busy_poll));
    int rcvbuf = 2 * 1024 * 1024;
    setsockopt(tx_sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    sockaddr_in bind_addr{};
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = htons(tx_port);
    bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(tx_sock, reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        std::cerr << "[Fatal] Bind UDP 127.0.0.1:" << tx_port << " impossible.\n";
        return 1;
    }

    std::thread tx_thread([&radio, tx_sock, local_node_id]() noexcept {
        setRealtimeCpu(1);

        auto hdr80211 = IEEE80211_HDR_TEMPLATE;
        hdr80211[15]  = local_node_id;

        alignas(64) std::array<std::array<uint8_t, BUFFER_STRIDE>, BATCH_SIZE> pool{};
        std::array<iovec, BATCH_SIZE> iov{};
        std::array<mmsghdr, BATCH_SIZE> msgs{};
        std::array<TxPacketView, BATCH_SIZE> tx_views{};

        for (size_t i = 0; i < BATCH_SIZE; ++i) {
            std::memcpy(pool[i].data(), RADIOTAP_HDR.data(), RADIOTAP_HDR.size());
            std::memcpy(pool[i].data() + RADIOTAP_HDR.size(), hdr80211.data(), hdr80211.size());

            iov[i].iov_base            = pool[i].data() + TX_HDR_LEN;
            iov[i].iov_len             = MAX_RTP_PAYLOAD;
            msgs[i].msg_hdr.msg_iov    = &iov[i];
            msgs[i].msg_hdr.msg_iovlen = 1;
        }

        constexpr timespec wait_ts{.tv_sec = 0, .tv_nsec = 50'000'000};

        while (g_running.load(std::memory_order_relaxed)) [[likely]] {
            timespec ts = wait_ts;
            const int n = recvmmsg(tx_sock, msgs.data(), BATCH_SIZE, MSG_WAITFORONE, &ts);
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
                break;
            }

            size_t valid_count = 0;
            for (int i = 0; i < n; ++i) {
                const unsigned int flags = msgs[i].msg_hdr.msg_flags;
                const size_t rtp_len     = msgs[i].msg_len;
                if (!(flags & MSG_TRUNC) && rtp_len > 0) [[likely]] {
                    tx_views[valid_count].data = pool[i].data();
                    tx_views[valid_count].len  = TX_HDR_LEN + rtp_len;
                    ++valid_count;
                }
            }

            if (valid_count > 0) [[likely]] {
                const size_t sent = radio->send_packets(tx_views.data(), valid_count);
                g_tx_pkts.fetch_add(sent, std::memory_order_relaxed);
            }
        }
    });

    // Thread de télémétrie légère (affiche les compteurs TX/RX toutes les 2 secondes)
    std::thread stats_thread([]() {
        while (g_running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!g_running.load(std::memory_order_relaxed)) break;
            std::cout << "[Stats] TX radio: " << g_tx_pkts.load(std::memory_order_relaxed)
                      << " pkts | RX radio -> UDP: " << g_rx_pkts.load(std::memory_order_relaxed)
                      << " pkts\n" << std::flush;
        }
    });

    std::cout << "[OK] Full Duplex actif sur 0b05:17d2 | Node ID: " << static_cast<int>(local_node_id)
              << " | Canal: " << channel << " | TX UDP :" << tx_port << " | RX UDP :" << rx_port
              << " (Ctrl+C pour quitter)\n";

    int caught_sig = 0;
    sigwait(&sigset, &caught_sig);

    std::cout << "\n[Arrêt] Signal " << caught_sig << " reçu, fermeture propre...\n";
    g_running.store(false, std::memory_order_release);

    radio->Stop();

    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
std::cout << "[Arrêt] Matériel éteint, sortie immédiate.\n" << std::flush;
_exit(0);
}).detach();
shutdown(tx_sock, SHUT_RDWR);
close(tx_sock);
libusb_interrupt_event_handler(usb_ctx);
if (stats_thread.joinable()) stats_thread.join();
if (tx_thread.joinable()) tx_thread.join();
if (rx_thread.joinable()) rx_thread.join();
radio.reset();
usb_lock.reset();
close(rx_sock);
libusb_release_interface(handle, 0);
libusb_close(handle);
libusb_exit(usb_ctx);
std::cout << "[Arrêt] Terminé.\n";
return 0;
}
