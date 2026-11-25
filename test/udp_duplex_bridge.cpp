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
    video/x-raw,width=1280,height=720,framerate=30/1 ! \
    x265enc tune=zerolatency speed-preset=ultrafast bitrate=2000 key-int-max=30 ! \
    rtph265pay config-interval=-1 mtu=1400 ! \
    udpsink host=127.0.0.1 port=5600 sync=false

sudo ./udp_duplex_bridge 2
gst-launch-1.0 udpsrc port=5601 caps="application/x-rtp,media=video,clock-rate=90000,encoding-name=H265,payload=96" ! \
    rtpjitterbuffer latency=20 drop-on-latency=true ! \
    rtph265depay ! h265parse ! avdec_h265 ! \
    autovideosink sync=false


*/
#include <arpa/inet.h>
#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <libusb-1.0/libusb.h>
#include <memory>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

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

// En-tête Radiotap minimal (13 octets) : NO_ACK + MCS3 (20 MHz)
static const uint8_t RADIOTAP_HDR[] = {
    0x00, 0x00,             // version + pad
    0x0d, 0x00,             // longueur = 13 octets
    0x00, 0x80, 0x08, 0x00, // present flags: TX flags + MCS
    0x08, 0x00,             // TX flags: NO_ACK
    0x07,                   // MCS known: BW + MCS + GI
    0x00,                   // MCS flags: 20 MHz, Long GI
    0x03                    // MCS index: MCS3
};

// En-tête IEEE 802.11 Data minimal (24 octets) avec signature "OP"
struct __attribute__((packed)) Ieee80211Hdr {
    uint16_t frame_control = 0x0008;
    uint16_t duration = 0x0000;
    uint8_t addr1[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t addr2[6] = {'O', 'P', 0x00, 0x00, 0x00, 0x01}; // addr2[5] = sender node_id
    uint8_t addr3[6] = {'O', 'P', 0x00, 0x00, 0x00, 0x00}; // BSSID filtre
    uint16_t seq_ctrl = 0x0000;
};

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

    // 1. Ouverture de la clé ASUS USB-AC56 (0b05:17d2)
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

    // 2. Sockets UDP locaux (udpsink -> port_in | port_out -> udpsrc)
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
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

    // Bring-up TX + RX sur une seule poignee USB
    g_radio->InitWrite(ch);

    auto on_rx_packet = [&](const Packet& pkt) {
        constexpr size_t hdr_len = sizeof(Ieee80211Hdr);
        if (pkt.Data.size() <= hdr_len) return;

        const auto* hdr = reinterpret_cast<const Ieee80211Hdr*>(pkt.Data.data());
        if (hdr->addr3[0] != 'O' || hdr->addr3[1] != 'P') return;
        if (hdr->addr2[5] == local_id) return; // Ignore notre propre echo

        const uint8_t* payload = pkt.Data.data() + hdr_len;
        size_t payload_len = pkt.Data.size() - hdr_len;

        sendto(udp_sock, payload, payload_len, 0,
               reinterpret_cast<const sockaddr*>(&addr_out), sizeof(addr_out));
    };

    // StartRxLoop est bloquant : on l'execute dans un thread dedie
    std::thread rx_thread([&]() {
        g_radio->StartRxLoop(on_rx_packet);
    });

    std::cout << "[Duplex Node " << int(local_id) << "] Canal " << int(channel)
              << " | UDP IN :" << port_in << " -> RF -> UDP OUT :" << port_out << "\n";

    // 4. Boucle TX : lecture UDP -> injection RF
    std::vector<uint8_t> udp_buf(2048);
    std::vector<uint8_t> tx_frame;
    Ieee80211Hdr dot11_hdr{};
    dot11_hdr.addr2[5] = local_id;
    uint16_t seq = 0;

    while (g_running) {
        ssize_t n = recvfrom(udp_sock, udp_buf.data(), udp_buf.size(), 0, nullptr, nullptr);
        if (n <= 0) continue;

        dot11_hdr.seq_ctrl = (seq++ & 0x0FFF) << 4;

        tx_frame.clear();
        tx_frame.insert(tx_frame.end(), RADIOTAP_HDR, RADIOTAP_HDR + sizeof(RADIOTAP_HDR));
        const auto* h_ptr = reinterpret_cast<const uint8_t*>(&dot11_hdr);
        tx_frame.insert(tx_frame.end(), h_ptr, h_ptr + sizeof(Ieee80211Hdr));
        tx_frame.insert(tx_frame.end(), udp_buf.data(), udp_buf.data() + n);

        g_radio->send_packet(tx_frame.data(), tx_frame.size());
    }

    // 5. Ordre d'arret obligatoire : StopRxLoop -> join -> detruire g_radio -> libusb
    g_radio->StopRxLoop();
    if (rx_thread.joinable()) rx_thread.join();
    g_radio.reset();

    close(udp_sock);
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    return 0;
}

