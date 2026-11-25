#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*
g++ -std=c++20 -O2 devourer_rtph265_bridge.cpp \
    -I../devourer/src \
    -I../devourer/hal \
    ../devourer/build/libdevourer.a \
    $(pkg-config --cflags --libs libusb-1.0) \
    -lpthread \
    -o devourer_rtph265_bridge

./devourer_rtph265_bridge duplex 149 5601 5600

gst-launch-1.0 videotestsrc is-live=true ! \
  x265enc tune=zerolatency speed-preset=ultrafast ! \
  rtph265pay config-interval=-1 mtu=1500 ! \
  udpsink host=127.0.0.1 port=5601 sync=false async=false

gst-launch-1.0 udpsrc address=127.0.0.1 port=5600 mtu=1500 buffer-size=0 \
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
#include <string_view>
#include <thread>

// En-têtes exacts du dépôt OpenIPC/devourer
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

// En-tête 802.11 Data (24 octets)
constexpr std::array<uint8_t, 24> IEEE80211_HDR = {
    0x08, 0x01, 0x00, 0x00,             // Frame Control (Data), Duration
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // Addr1 (Broadcast)
    0x57, 0x42, 0x75, 0x05, 0xd6, 0x00, // Addr2 (SA)
    0x57, 0x42, 0x75, 0x05, 0xd6, 0x00, // Addr3 (BSSID)
    0x00, 0x00                          // Sequence Control
};

constexpr size_t TX_HDR_LEN    = RADIOTAP_HDR.size() + IEEE80211_HDR.size(); // 37 octets
constexpr size_t BUFFER_STRIDE = 1600; // Multiple de 64 octets (> 37 + 1500 = 1537)

std::atomic<bool> g_running{true};

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
    // 1. Masquage déterministe de SIGINT/SIGTERM avant toute création de thread ou libusb
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigset, nullptr);

    const std::string_view mode = (argc > 1) ? argv[1] : "duplex";
    const int channel           = (argc > 2) ? std::atoi(argv[2]) : 149;
    const uint16_t tx_port      = (argc > 3) ? static_cast<uint16_t>(std::atoi(argv[3])) : 5601;
    const uint16_t rx_port      = (argc > 4) ? static_cast<uint16_t>(std::atoi(argv[4])) : 5600;

    if (mode != "rx" && mode != "tx" && mode != "duplex") {
        std::cerr << "Usage: " << argv << " [rx|tx|duplex] [channel=149] [tx_port=5601] [rx_port=5600]\n";
        return 1;
    }

    // 2. Initialisation libusb et ouverture de l'adaptateur ASUS USB-AC56 (0b05:17d2)
    libusb_context* usb_ctx = nullptr;
    if (libusb_init(&usb_ctx) < 0) {
        std::cerr << "[Fatal] libusb_init échoué.\n";
        return 1;
    }

    libusb_device_handle* handle = libusb_open_device_with_vid_pid(usb_ctx, USB_VID, USB_PID);
    if (!handle) {
        std::cerr << "[Fatal] Adaptateur 0b05:17d2 introuvable (vérifiez lsusb et udev).\n";
        libusb_exit(usb_ctx);
        return 1;
    }

    auto logger = std::make_shared<Logger>();
    std::shared_ptr<devourer::UsbDeviceLock> usb_lock;

    if (devourer::claim_interface_then_reset(handle, 0, logger, true, usb_lock, "0b05:17d2") != 0) {
        std::cerr << "[Fatal] Impossible de revendiquer l'interface USB 0 (déchargez rtw88_8812au).\n";
        libusb_close(handle);
        libusb_exit(usb_ctx);
        return 1;
    }

    // 3. Instanciation radio via WiFiDriver::CreateRadio
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

    const bool enable_rx = (mode == "rx" || mode == "duplex");
    const bool enable_tx = (mode == "tx" || mode == "duplex");

    // 4. Socket RX connecté vers GStreamer udpsrc (127.0.0.1:5600)
    int rx_sock = -1;
    if (enable_rx) {
        rx_sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port   = htons(rx_port);
        inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);
        connect(rx_sock, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
    }

    auto rx_callback = [rx_sock](const Packet& pkt) noexcept {
        if (pkt.Data.size() > IEEE80211_HDR.size()) [[likely]] {
            const uint8_t* rtp_ptr = pkt.Data.data() + IEEE80211_HDR.size();
            const size_t rtp_len   = pkt.Data.size() - IEEE80211_HDR.size();
            if (rtp_len <= MAX_RTP_PAYLOAD) [[likely]] {
                send(rx_sock, rtp_ptr, rtp_len, MSG_DONTWAIT);
            }
        }
    };

    // 5. Démarrage matériel selon le mode (RX, TX ou Duplex)
    std::thread rx_thread;
    if (enable_tx && enable_rx) {
        radio->InitWrite(sel_chan);
        rx_thread = std::thread([&radio, &rx_callback]() {
            radio->StartRxLoop(rx_callback);
        });
    } else if (enable_tx) {
        radio->InitWrite(sel_chan);
    } else {
        rx_thread = std::thread([&radio, &rx_callback, sel_chan]() {
            radio->Init(rx_callback, sel_chan);
        });
    }

    // 6. Boucle TX Zero-Copy par lots (GStreamer udpsink -> recvmmsg -> Radio)
    int tx_sock = -1;
    std::thread tx_thread;
    if (enable_tx) {
        tx_sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);

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
            g_running.store(false, std::memory_order_release);
        } else {
            tx_thread = std::thread([&radio, tx_sock]() noexcept {
                setRealtimeCpu(1);

                alignas(64) std::array<std::array<uint8_t, BUFFER_STRIDE>, BATCH_SIZE> pool{};
                std::array<iovec, BATCH_SIZE> iov{};
                std::array<mmsghdr, BATCH_SIZE> msgs{};

                for (size_t i = 0; i < BATCH_SIZE; ++i) {
                    std::memcpy(pool[i].data(), RADIOTAP_HDR.data(), RADIOTAP_HDR.size());
                    std::memcpy(pool[i].data() + RADIOTAP_HDR.size(), IEEE80211_HDR.data(), IEEE80211_HDR.size());

                    iov[i].iov_base            = pool[i].data() + TX_HDR_LEN;
                    iov[i].iov_len             = MAX_RTP_PAYLOAD;
                    msgs[i].msg_hdr.msg_iov    = &iov[i];
                    msgs[i].msg_hdr.msg_iovlen = 1;
                }

                constexpr timespec wait_ts{.tv_sec = 0, .tv_nsec = 50'000'000}; // Timeout 50 ms

                while (g_running.load(std::memory_order_relaxed)) [[likely]] {
                    timespec ts = wait_ts;
                    const int n = recvmmsg(tx_sock, msgs.data(), BATCH_SIZE, MSG_WAITFORONE, &ts);
                    if (n < 0) {
                        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
                        break;
                    }
                    for (int i = 0; i < n; ++i) {
                        const unsigned int flags = msgs[i].msg_hdr.msg_flags;
                        const size_t rtp_len     = msgs[i].msg_len;
                        if (!(flags & MSG_TRUNC) && rtp_len > 0) [[likely]] {
                            radio->send_packet(pool[i].data(), TX_HDR_LEN + rtp_len);
                        }
                    }
                }
            });
        }
    }

    std::cout << "[OK] Pont actif (" << mode << ") sur 0b05:17d2 | Canal " << channel
              << " | TX UDP :" << tx_port << " | RX UDP :" << rx_port
              << " (Ctrl+C pour quitter)\n";

    // 7. Attente synchrone de Ctrl+C (SIGINT) ou SIGTERM
    int caught_sig = 0;
    sigwait(&sigset, &caught_sig);

    std::cout << "\n[Arrêt] Signal " << caught_sig << " reçu, fermeture propre...\n";
    g_running.store(false, std::memory_order_release);

    // 1. Arrêt matériel immédiat (exécute Jaguar1 clean de-init + PowerOff)
    radio->Stop();

    // 2. Chien de garde : si libusb_close/join bloque sur un URB noyau, rend la main après 250 ms
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        std::cout << "[Arrêt] Matériel éteint, sortie immédiate.\n" << std::flush;
        _exit(0);
    }).detach();

    // 3. Déblocage des sockets et réveil libusb
    if (tx_sock >= 0) {
        shutdown(tx_sock, SHUT_RDWR);
        close(tx_sock);
        tx_sock = -1;
    }
    libusb_interrupt_event_handler(usb_ctx);

    if (tx_thread.joinable()) tx_thread.join();
    if (rx_thread.joinable()) rx_thread.join();

    radio.reset();
    usb_lock.reset();

    if (rx_sock >= 0) {
        close(rx_sock);
        rx_sock = -1;
    }

    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(usb_ctx);

    std::cout << "[Arrêt] Terminé.\n";
    return 0;
}
