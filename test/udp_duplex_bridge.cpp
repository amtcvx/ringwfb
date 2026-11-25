/*
g++ -std=c++20 -O2 udp_duplex_bridge.cpp \
    -I../devourer/src \
    -I../devourer/hal \
    ../devourer/build/libdevourer.a \
    $(pkg-config --cflags --libs libusb-1.0) \
    -lpthread \
    -o udp_duplex_bridge

sudo ./udp_duplex_bridge 1
gst-launch-1.0 videotestsrc is-live=true ! \
    video/x-raw,width=640,height=480,framerate=30/1 ! \
    x265enc tune=zerolatency speed-preset=ultrafast bitrate=1200 key-int-max=15 option-string="repeat-headers=1" ! \
    rtph265pay config-interval=-1 mtu=1400 ! \
    udpsink host=127.0.0.1 port=5600 sync=false

gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=YUY2,width=640,height=480,framerate=30/1 ! \
    videoconvert ! video/x-raw,format=I420 ! \
    x265enc tune=zerolatency speed-preset=ultrafast bitrate=1200 key-int-max=15 option-string="repeat-headers=1" ! \
    rtph265pay config-interval=-1 mtu=1400 ! \
    udpsink host=127.0.0.1 port=5600 sync=false


sudo ./udp_duplex_bridge 2
gst-launch-1.0 -v udpsrc port=5601 caps="application/x-rtp,media=video,clock-rate=90000,encoding-name=H265" ! \
    rtph265depay ! h265parse ! avdec_h265 ! \
    videoconvert ! autovideosink sync=false

*/
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <libusb-1.0/libusb.h>
#include <memory>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "RxPacket.h"
#include "WiFiDriver.h"
#include "logger.h"

static std::atomic<bool> g_running{true};
static std::shared_ptr<IRadio> g_radio;

void sig_handler(int) {
    g_running = false;
    if (g_radio) {
        g_radio->StopRxLoop();
    }
}

// En-tete Radiotap (13 octets) : NO_ACK + MCS1 + Short Guard Interval (SGI = true)
static constexpr uint8_t RADIOTAP_HDR[13] = {
    0x00, 0x00,             // version + pad
    0x0d, 0x00,             // longueur = 13 octets
    0x00, 0x80, 0x08, 0x00, // present flags: TX flags (0x00008000) + MCS (0x00080000)
    0x08, 0x00,             // TX flags: NO_ACK (0x0008)
    0x07,                   // MCS known: BW (0x01) | MCS (0x02) | GI (0x04)
    0x04,                   // MCS flags: SGI = 1 (0x04) | 20 MHz (0x00)
    0x01                    // MCS index: MCS1 (14.4 Mbps avec SGI)
};

// En-tete IEEE 802.11 Data (24 octets) avec signature "OP"
struct __attribute__((packed)) Ieee80211Hdr {
    uint16_t frame_control = 0x0008;
    uint16_t duration = 0x0000;
    uint8_t addr1[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t addr2[6] = {'O', 'P', 0x00, 0x00, 0x00, 0x01}; // addr2[5] = sender node_id
    uint8_t addr3[6] = {'O', 'P', 0x00, 0x00, 0x00, 0x00}; // BSSID filtre
    uint16_t seq_ctrl = 0x0000;
};

static constexpr size_t RTAP_LEN = sizeof(RADIOTAP_HDR);
static constexpr size_t DOT11_LEN = sizeof(Ieee80211Hdr);
static constexpr size_t TX_HDR_LEN = RTAP_LEN + DOT11_LEN; // 37 octets

static void pin_and_prioritize(int core_id, int priority) {
    unsigned int num_cores = std::thread::hardware_concurrency();
    if (num_cores > 1) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id % static_cast<int>(num_cores), &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }
    sched_param sp{};
    sp.sched_priority = priority;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: sudo " << argv[0] << " <node_id: 1 ou 2> [channel=149] [udp_in=5600] [udp_out=5601]\n";
        return 1;
    }

    uint8_t local_id  = static_cast<uint8_t>(std::stoi(argv[1]));
    uint8_t channel   = (argc > 2) ? static_cast<uint8_t>(std::stoi(argv[2])) : 149;
    uint16_t port_in  = (argc > 3) ? static_cast<uint16_t>(std::stoi(argv[3])) : 5600;
    uint16_t port_out = (argc > 4) ? static_cast<uint16_t>(std::stoi(argv[4])) : 5601;

    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

    // 1. Ouverture de la cle ASUS USB-AC56 (0b05:17d2)
    libusb_context* ctx = nullptr;
    if (libusb_init(&ctx) < 0) return 1;

    libusb_device_handle* handle = libusb_open_device_with_vid_pid(ctx, 0x0b05, 0x17d2);
    if (!handle) {
        std::cerr << "Erreur: cle USB 0b05:17d2 introuvable.\n";
        libusb_exit(ctx);
        return 1;
    }

    if (libusb_kernel_driver_active(handle, 0) == 1) {
        libusb_detach_kernel_driver(handle, 0);
    }
    if (libusb_claim_interface(handle, 0) < 0) {
        std::cerr << "Erreur: impossible de reclamer l'interface USB 0.\n";
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }

    // 2. Deux sockets UDP dedies (aucun verrou partage entre RX et TX)
    int tx_udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    int rx_udp_sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);

    int buf_sz = 4 * 1024 * 1024;
    setsockopt(tx_udp_sock, SOL_SOCKET, SO_RCVBUF, &buf_sz, sizeof(buf_sz));
    setsockopt(rx_udp_sock, SOL_SOCKET, SO_SNDBUF, &buf_sz, sizeof(buf_sz));

    sockaddr_in addr_in{};
    addr_in.sin_family = AF_INET;
    addr_in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr_in.sin_port = htons(port_in);
    if (bind(tx_udp_sock, reinterpret_cast<sockaddr*>(&addr_in), sizeof(addr_in)) < 0) {
        perror("bind UDP");
        return 1;
    }

    timeval tv{0, 100000};
    setsockopt(tx_udp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr_out{};
    addr_out.sin_family = AF_INET;
    addr_out.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr_out.sin_port = htons(port_out);
    connect(rx_udp_sock, reinterpret_cast<const sockaddr*>(&addr_out), sizeof(addr_out));

    // 3. Initialisation de la radio via libdevourer.a
    auto logger = std::make_shared<Logger>();
    WiFiDriver driver(logger);
    g_radio = driver.CreateRadio(handle);

    SelectedChannel ch{};
    ch.Channel = channel;
    ch.ChannelOffset = 0;
    ch.ChannelWidth = CHANNEL_WIDTH_20;

    g_radio->InitWrite(ch);

    // Callback RX Zero-Copy
    auto on_rx_packet = [rx_udp_sock, local_id](const Packet& pkt) noexcept {
        if (__builtin_expect(pkt.Data.size() <= DOT11_LEN, 0)) return;

        const auto* hdr = reinterpret_cast<const Ieee80211Hdr*>(pkt.Data.data());
        if (__builtin_expect(hdr->addr3[0] != 'O' || hdr->addr3[1] != 'P', 0)) return;
        if (__builtin_expect(hdr->addr2[5] == local_id, 0)) return;

        send(rx_udp_sock,
             pkt.Data.data() + DOT11_LEN,
             pkt.Data.size() - DOT11_LEN,
             MSG_DONTWAIT);
    };

    // Thread RX haute priorite (coeur 1, priorite 90)
    std::thread rx_thread([&]() {
        pin_and_prioritize(1, 90);
        g_radio->StartRxLoop(on_rx_packet);
    });

    // Thread TX priorite moderee (coeur 2, priorite 40)
    pin_and_prioritize(2, 40);

    std::cout << "[ZeroCopy Duplex Node " << int(local_id) << "] Canal " << int(channel)
              << " | :" << port_in << " <-> RF (MCS1 + SGI) <-> :" << port_out << "\n";

    // 4. Boucle TX Zero-Copy : recvfrom ecrit directement apres les 37 octets d'en-tete
    alignas(64) uint8_t tx_buf[2048];
    std::memcpy(tx_buf, RADIOTAP_HDR, RTAP_LEN);

    auto* dot11_hdr = reinterpret_cast<Ieee80211Hdr*>(tx_buf + RTAP_LEN);
    *dot11_hdr = Ieee80211Hdr{};
    dot11_hdr->addr2[5] = local_id;

    uint8_t* payload_ptr = tx_buf + TX_HDR_LEN;
    constexpr size_t max_payload = sizeof(tx_buf) - TX_HDR_LEN;
    uint16_t seq = 0;

    while (g_running) {
        ssize_t n = recvfrom(tx_udp_sock, payload_ptr, max_payload, 0, nullptr, nullptr);
        if (__builtin_expect(n <= 0, 0)) continue;

        dot11_hdr->seq_ctrl = (seq++ & 0x0FFF) << 4;
        g_radio->send_packet(tx_buf, TX_HDR_LEN + static_cast<size_t>(n));
        std::this_thread::sleep_for(std::chrono::microseconds(300));
    }

    // 5. Arret propre
    g_radio->StopRxLoop();
    if (rx_thread.joinable()) rx_thread.join();
    g_radio.reset();

    close(tx_udp_sock);
    close(rx_udp_sock);
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    return 0;
}

