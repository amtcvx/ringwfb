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
    x265enc tune=zerolatency speed-preset=ultrafast bitrate=800 key-int-max=30 ! \
    rtph265pay config-interval=-1 mtu=1400 ! \
    udpsink host=127.0.0.1 port=5600 sync=false

sudo ./udp_duplex_bridge 2
gst-launch-1.0 -v udpsrc port=5601 caps="application/x-rtp,media=video,clock-rate=90000,encoding-name=H265" ! \
    rtph265depay ! h265parse ! avdec_h265 ! \
    videoconvert ! autovideosink sync=false

*/
#include <arpa/inet.h>
#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <libusb-1.0/libusb.h>
#include <memory>
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

// En-tete Radiotap (13 octets) : NO_ACK + MCS1 (13 Mbps en 20 MHz, robuste et rapide)
static constexpr uint8_t RADIOTAP_HDR[13] = {
    0x00, 0x00,             // version + pad
    0x0d, 0x00,             // longueur = 13 octets
    0x00, 0x80, 0x08, 0x00, // present flags: TX flags + MCS
    0x08, 0x00,             // TX flags: NO_ACK
    0x07,                   // MCS known: BW + MCS + GI
    0x00,                   // MCS flags: 20 MHz, Long GI
    0x01                    // MCS index: MCS1
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

    // Priorite temps reel (reduit la latence USB/UDP)
    sched_param sp{};
    sp.sched_priority = 50;
    sched_setscheduler(0, SCHED_FIFO, &sp);

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

    // 2. Sockets UDP avec tampons 2 Mo (evite les pertes sur rafales IDR)
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    int buf_sz = 2 * 1024 * 1024;
    setsockopt(udp_sock, SOL_SOCKET, SO_RCVBUF, &buf_sz, sizeof(buf_sz));
    setsockopt(udp_sock, SOL_SOCKET, SO_SNDBUF, &buf_sz, sizeof(buf_sz));

    sockaddr_in addr_in{};
    addr_in.sin_family = AF_INET;
    addr_in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr_in.sin_port = htons(port_in);
    if (bind(udp_sock, reinterpret_cast<sockaddr*>(&addr_in), sizeof(addr_in)) < 0) {
        perror("bind UDP");
        return 1;
    }

    timeval tv{0, 100000};
    setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr_out{};
    addr_out.sin_family = AF_INET;
    addr_out.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr_out.sin_port = htons(port_out);

    // 3. Initialisation de la radio via libdevourer.a
    auto logger = std::make_shared<Logger>();
    WiFiDriver driver(logger);
    g_radio = driver.CreateRadio(handle);

    SelectedChannel ch{};
    ch.Channel = channel;
    ch.ChannelOffset = 0;
    ch.ChannelWidth = CHANNEL_WIDTH_20;

    g_radio->InitWrite(ch);

    // Callback RX 100% Zero-Copy (offset 802.11 = 0 confirme)
    auto on_rx_packet = [&](const Packet& pkt) {
        if (__builtin_expect(pkt.Data.size() <= DOT11_LEN, 0)) return;

        const auto* hdr = reinterpret_cast<const Ieee80211Hdr*>(pkt.Data.data());
        // Verification rapide de la signature "OP" et filtrage de l'echo local
        if (hdr->addr3[0] != 'O' || hdr->addr3[1] != 'P') return;
        if (hdr->addr2[5] == local_id) return;

        sendto(udp_sock,
               pkt.Data.data() + DOT11_LEN,
               pkt.Data.size() - DOT11_LEN,
               MSG_DONTWAIT,
               reinterpret_cast<const sockaddr*>(&addr_out),
               sizeof(addr_out));
    };

    std::thread rx_thread([&]() {
        sched_setscheduler(0, SCHED_FIFO, &sp);
        g_radio->StartRxLoop(on_rx_packet);
    });

    std::cout << "[ZeroCopy Duplex Node " << int(local_id) << "] Canal " << int(channel)
              << " | :" << port_in << " <-> RF (MCS1) <-> :" << port_out << "\n";

    // 4. Boucle TX Zero-Copy : recvfrom ecrit directement apres les 37 octets d'en-tete
    alignas(64) uint8_t tx_buf[4096];
    std::memcpy(tx_buf, RADIOTAP_HDR, RTAP_LEN);

    auto* dot11_hdr = reinterpret_cast<Ieee80211Hdr*>(tx_buf + RTAP_LEN);
    *dot11_hdr = Ieee80211Hdr{};
    dot11_hdr->addr2[5] = local_id;

    uint8_t* payload_ptr = tx_buf + TX_HDR_LEN;
    constexpr size_t max_payload = sizeof(tx_buf) - TX_HDR_LEN;
    uint16_t seq = 0;

    while (g_running) {
        ssize_t n = recvfrom(udp_sock, payload_ptr, max_payload, 0, nullptr, nullptr);
        if (__builtin_expect(n <= 0, 0)) continue;

        dot11_hdr->seq_ctrl = (seq++ & 0x0FFF) << 4;
        g_radio->send_packet(tx_buf, TX_HDR_LEN + static_cast<size_t>(n));
    }

    // 5. Arret propre
    g_radio->StopRxLoop();
    if (rx_thread.joinable()) rx_thread.join();
    g_radio.reset();

    close(udp_sock);
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    return 0;
}
