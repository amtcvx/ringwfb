/*
DEVPATH=../devourer g++ -std=c++20 -O2 recepteur.cpp \
    -I$DEVPATH/src \
    -I$DEVPATH/src/hal \
    $DEVPATH/build/libdevourer.a \
    $(pkg-config --cflags --libs libusb-1.0) \
    -lpthread \
    -o recepteur

Récepteur : Accrochage auto + Pont Radio → UDP

Dès que le récepteur capte une trame portant tx_channel == candidate_channel, 
il verrouille la fréquence et transfère instantanément chaque paquet vidéo vers le port UDP 5600 local.

gst-launch-1.0 udpsrc uri=udp://127.0.0.1:5600 ! application/x-rtp, encoding-name=H265, payload=96 ! \
  rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false

*/
#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>

#include "WiFiDriver.h"
#include "IRadio.h"
#include "RxPacket.h"
#include "SelectedChannel.h"
#include "AdapterCaps.h"

#pragma pack(push, 1)
struct VideoPacketHeader {
    uint8_t wifi_hdr[24];   // 0..23 : En-tête 802.11 standard
    uint8_t tx_channel;     // 24    : Canal réel d'émission
    uint8_t seq[3];         // 25..27: Numéro de séquence 24 bits
};
#pragma pack(pop)

#include <cstdlib>
#include <string>

uint16_t get_env_hex(const char* name, uint16_t default_val) {
    const char* val = std::getenv(name);
    if (!val || *val == '\0') return default_val;
    return static_cast<uint16_t>(std::stoul(val, nullptr, 16));
}

// Parcourt tous les périphériques USB, filtre par VID:PID et ouvre le premier disponible
libusb_device_handle* open_first_available_adapter(libusb_context* ctx) {
    uint16_t target_vid = get_env_hex("DEVOURER_VID", 0x0b05);
    uint16_t target_pid = get_env_hex("DEVOURER_PID", 0x17d2);

    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0) {
        std::cerr << "[USB] Erreur lors du listage des peripheriques.\n";
        return nullptr;
    }

    libusb_device_handle* claimed_handle = nullptr;
    int matching_found = 0;

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device* dev = list[i];
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(dev, &desc) != 0) continue;

        if (desc.idVendor != target_vid || desc.idProduct != target_pid) continue;

        ++matching_found;
        uint8_t bus  = libusb_get_bus_number(dev);
        uint8_t port = libusb_get_port_number(dev);

        libusb_device_handle* handle = nullptr;
        if (libusb_open(dev, &handle) != 0) {
            std::cout << "[USB] Cle #" << matching_found << " (Bus " << (int)bus
                      << " Port " << (int)port << ") inaccessible, passage a la suivante...\n";
            continue;
        }

        // Détache le pilote noyau (rtw88 / 88XXau) si actif
        if (libusb_kernel_driver_active(handle, 0) == 1) {
            libusb_detach_kernel_driver(handle, 0);
        }

        // Tente de verrouiller l'interface 0 : échoue si un autre processus l'utilise déjà
        int rc = libusb_claim_interface(handle, 0);
        if (rc == 0) {
            std::cout << "[USB] Adaptateur disponible verrouille : "
                      << std::hex << target_vid << ":" << target_pid << std::dec
                      << " (Bus " << (int)bus << ", Port " << (int)port << ")\n";
            claimed_handle = handle;
            break;
        } else {
            std::cout << "[USB] Cle #" << matching_found << " (Bus " << (int)bus
                      << " Port " << (int)port << ") deja occupee ("
                      << libusb_strerror(static_cast<libusb_error>(rc)) << "), ignorée.\n";
            libusb_close(handle);
        }
    }

    libusb_free_device_list(list, 1);

    if (!claimed_handle) {
        std::cerr << "[USB] Aucun adaptateur libre trouve pour "
                  << std::hex << target_vid << ":" << target_pid << std::dec
                  << " (" << matching_found << " detecte(s) au total).\n";
    }

    return claimed_handle;
}

int main() {
    libusb_context* ctx = nullptr;
    libusb_init(&ctx);

    libusb_device_handle* handle = open_first_available_adapter(ctx);
    if (!handle) {
        libusb_exit(ctx);
        return 1;
    }
    libusb_detach_kernel_driver(handle, 0);
    libusb_claim_interface(handle, 0);

    // Socket UDP de sortie vers le lecteur vidéo (127.0.0.1:5601)
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in dst_addr{};
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_port = htons(5600);
    dst_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    auto logger = std::make_shared<Logger>();
    WiFiDriver driver(logger);
    auto radio = driver.CreateRadio(handle);
    if (!radio) {
        std::cerr << "Erreur : Impossible de creer l'instance radio.\n";
        return 1;
    }

    const std::vector<uint8_t> channels = {36, 40, 44, 48, 149, 153, 157, 161, 165};
    std::atomic<bool> is_locked{false};
    std::atomic<uint8_t> current_ch{channels[0]};
    std::atomic<uint64_t> last_rx_ms{0};

    auto now_ms = []() -> uint64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    };

    // 1. Initialisation matérielle en 20 MHz via InitWrite (non-bloquant)
    SelectedChannel init_ch{};
    init_ch.Channel = channels[0];
    init_ch.ChannelOffset = 0;
    init_ch.ChannelWidth = CHANNEL_WIDTH_20;
    init_ch.Band = 0;
    radio->InitWrite(init_ch);

    // 2. Lancement de StartRxLoop dans un thread dédié
    std::thread rx_thread([&]() {
        radio->StartRxLoop([&](const Packet& pkt) {
            if (pkt.RxAtrib.pkt_rpt_type != RX_PACKET_TYPE::NORMAL_RX) return;
            if (pkt.RxAtrib.crc_err) return;

            const uint8_t* data = pkt.Data.data();
            size_t len = pkt.Data.size();

            // Retirer les 4 octets de FCS matériel ajoutés par la RTL8812AU
            if (pkt.RxAtrib.fcs_present) {
                if (len < 4) return;
                len -= 4;
            }

            if (len <= sizeof(VideoPacketHeader)) return;

            const auto* hdr = reinterpret_cast<const VideoPacketHeader*>(data);

            // Vérifie la signature MAC (octets 10..12) ET le canal annoncé
            if (hdr->wifi_hdr[10] == 0x12 &&
                hdr->wifi_hdr[11] == 0x34 &&
                hdr->wifi_hdr[12] == 0x56 &&
                hdr->tx_channel == current_ch.load(std::memory_order_relaxed)) {

                last_rx_ms.store(now_ms(), std::memory_order_relaxed);

                if (!is_locked.exchange(true)) {
                    std::cout << "[RX] Video verrouillee sur le canal "
                              << (int)hdr->tx_channel
                              << " | RSSI Ant A: " << (int)pkt.RxAtrib.rssi[0]
                              << " | Ant B: " << (int)pkt.RxAtrib.rssi[1] << "\n";
                }

                // Réinjection immédiate du payload RTP/UDP pur
                const uint8_t* payload = data + sizeof(VideoPacketHeader);
                size_t payload_len = len - sizeof(VideoPacketHeader);
                sendto(udp_sock, payload, payload_len, 0,
                       reinterpret_cast<sockaddr*>(&dst_addr), sizeof(dst_addr));
            }
        });
    });

    std::cout << "[RX] Recherche du signal video en 20 MHz...\n";
    size_t idx = 0;

    // 3. Boucle de contrôle : FastRetune jusqu'à verrouillage + Watchdog
    while (true) {
        if (!is_locked.load()) {
            uint8_t ch = channels[idx];
            current_ch.store(ch);
            radio->FastRetune(ch);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));

            if (!is_locked.load()) {
                idx = (idx + 1) % channels.size();
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            if (now_ms() - last_rx_ms.load() > 1500) {
                std::cout << "[RX] Signal perdu, reprise du scan...\n";
                is_locked.store(false);
            }
        }
    }

    radio->StopRxLoop();
    if (rx_thread.joinable()) rx_thread.join();
    close(udp_sock);
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    return 0;
}
