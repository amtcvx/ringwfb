/*
g++ -std=c++20 -O2 emetteur.cpp \
    -I../devourer/src \
    -I../devourer/hal \
    ../devourer/build/libdevourer.a \
    $(pkg-config --cflags --libs libusb-1.0) \
    -lpthread \
    -o emetteur

Émetteur : Scan auto + Pont UDP → Radio (libdevourer.a)

Ce code reprend le scan par antenne, sélectionne le meilleur canal en 20 MHz, 
puis relaye en continu tout flux vidéo arrivant sur le port UDP 5700.

gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420  ! \
  x265enc bitrate=2048 ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5700 host=127.0.0.1

*/
#include <iostream>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>

#include "WiFiDriver.h"
#include "IRadio.h"
#include "IRtlRadio.h"
#include "RxPacket.h"
#include "RxSense.h"
#include "SelectedChannel.h"
#include "AdapterCaps.h"

#include "TxMode.h"

#pragma pack(push, 1)
// En-tête Radiotap minimal de 8 octets requis devant le MPDU par send_packet
struct MinimalRadiotapHeader {
    uint8_t it_version = 0;
    uint8_t it_pad = 0;
    uint16_t it_len = 8;
    uint32_t it_present = 0;
};

struct VideoPacketHeader {
    uint8_t wifi_hdr[24];   // 0..23 : En-tête 802.11 standard (doit correspondre à recepteur.cpp)
    uint8_t tx_channel;     // 24    : Canal réel d'émission
    uint8_t seq[3];         // 25..27: Numéro de séquence 24 bits
};

struct FullTxFrame {
    MinimalRadiotapHeader rtap;
    VideoPacketHeader hdr;
    uint8_t payload[1450];
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
    uint16_t target_vid1 = get_env_hex("DEVOURER_VID", 0x0bda);
    uint16_t target_pid1 = get_env_hex("DEVOURER_PID", 0x8812);
    uint16_t target_vid2 = get_env_hex("DEVOURER_VID", 0x0b05);
    uint16_t target_pid2 = get_env_hex("DEVOURER_PID", 0x17d2);
    uint16_t target_vid, target_pid;

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

        if (!(desc.idVendor == target_vid1 && desc.idProduct == target_pid1) &&
           !(desc.idVendor == target_vid2 && desc.idProduct == target_pid2)) continue;
        target_vid = desc.idVendor; target_pid = desc.idProduct;

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

    auto logger = std::make_shared<Logger>();
    WiFiDriver driver(logger);
    auto radio = driver.CreateRadio(handle);
    if (!radio) {
        std::cerr << "Erreur : Impossible de creer l'instance radio.\n";
        return 1;
    }

    const devourer::AdapterCaps caps = radio->GetAdapterCaps();
    auto* rtl_radio = caps.rx_energy_ok ? dynamic_cast<IRtlRadio*>(radio.get()) : nullptr;

    // 1. Initialisation matérielle en 20 MHz sur le premier canal 5 GHz
    const std::vector<uint8_t> channels = {36, 40, 44, 48, 149, 153, 157, 161, 165};
    SelectedChannel sel_ch{};
    sel_ch.Channel = channels[0];
    sel_ch.ChannelOffset = 0;
    sel_ch.ChannelWidth = CHANNEL_WIDTH_20;
    sel_ch.Band = 0;
    radio->InitWrite(sel_ch);

    // Boucle RX passive en arrière-plan pour les transferts USB pendant le scan
    std::thread rx_thread([&]() {
        radio->StartRxLoop([](const Packet&) {});
    });

    // 2. Balayage des canaux 5 GHz (20 MHz) et classement automatique
    uint8_t best_channel = channels[0];
    float best_score = 1e9f;
    const uint32_t dwell_us = 25000; // 25 ms par canal

    std::cout << "[TX] Analyse spectrale des canaux 5 GHz (20 MHz)...\n";
    for (uint8_t ch : channels) {
        radio->FastRetune(ch);
        std::this_thread::sleep_for(std::chrono::microseconds(1500));

        if (rtl_radio) {
            rtl_radio->GetRxEnergy(false); // Reset des compteurs delta FA/CCA
        }
        radio->ArmChannelBusy(dwell_us);
        std::this_thread::sleep_for(std::chrono::microseconds(dwell_us + 2000));

        auto busy_info = radio->GetChannelBusy();
        float busy = busy_info.valid ? static_cast<float>(busy_info.busy_pct) : 0.0f;

        float nhm_env = 0.0f;
        uint32_t fa = 0;
        if (rtl_radio) {
            RxEnergy e = rtl_radio->GetRxEnergy(true);
            if (e.valid_nhm) nhm_env = static_cast<float>(e.nhm_env_ratio_pct);
            if (e.valid_fa) fa = e.fa_ofdm;
        }

        float score = busy + (nhm_env * 0.8f) + (fa * 0.1f);
        std::cout << "  - Canal " << (int)ch << " : Occ=" << busy
                  << "% | Bruit_Env=" << nhm_env << "% | FA=" << fa
                  << " => Score=" << score << "\n";

        if (score < best_score) {
            best_score = score;
            best_channel = ch;
        }
    }

    // 3. Reconfiguration sur le canal gagnant en 20 MHz pour l'émission
    sel_ch.Channel = best_channel;
    sel_ch.ChannelOffset = 0;
    sel_ch.ChannelWidth = CHANNEL_WIDTH_20;
    sel_ch.Band = 0;
    radio->SetMonitorChannel(sel_ch);

    devourer::TxMode tx_mode{};
    tx_mode.mode   = devourer::TxMode::Mode::HT; // Mode 802.11n (HT)
    tx_mode.ht_mcs = 3;                          // MCS 3 (~26 Mbps bruts en 20 MHz)
    tx_mode.bw_mhz = 20;                         // Largeur stricte 20 MHz
    tx_mode.sgi    = false;                      // Long Guard Interval (meilleure tenue aux échos)
    tx_mode.ldpc   = true;                       // Codage LDPC actif
    tx_mode.stbc   = true;                       // STBC actif (exploite les 2 antennes TX)

    radio->SetTxMode(tx_mode);

    radio->SetTxPower(20);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    std::cout << "[TX] Canal elu : " << (int)best_channel
              << " (20 MHz) | Score : " << best_score << "\n";

    // 4. Ouverture du socket UDP d'entrée vidéo (127.0.0.1:5700)
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5700);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(udp_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Erreur bind UDP 5700\n";
        return 1;
    }

    // Préparation de la trame d'émission (Radiotap 8o + 802.11 24o + Meta 4o)
    FullTxFrame tx_frame{};
    std::memset(&tx_frame.hdr, 0, sizeof(VideoPacketHeader));
    tx_frame.hdr.wifi_hdr[0] = 0x08; // Type: Data
    std::memset(&tx_frame.hdr.wifi_hdr[4], 0xFF, 6); // Destination: Broadcast
    // Signature MAC source attendue par recepteur.cpp (octets 10, 11, 12)
    tx_frame.hdr.wifi_hdr[10] = 0x12;
    tx_frame.hdr.wifi_hdr[11] = 0x34;
    tx_frame.hdr.wifi_hdr[12] = 0x56;
    tx_frame.hdr.tx_channel = best_channel;

    uint32_t seq = 0;
    const size_t header_overhead = sizeof(MinimalRadiotapHeader) + sizeof(VideoPacketHeader);
    std::cout << "[TX] En attente du flux video UDP sur 127.0.0.1:5700...\n";

    while (true) {
        ssize_t n = recv(udp_sock, tx_frame.payload, sizeof(tx_frame.payload), 0);
        if (n > 0) {
            tx_frame.hdr.seq[0] = seq & 0xFF;
            tx_frame.hdr.seq[1] = (seq >> 8) & 0xFF;
            tx_frame.hdr.seq[2] = (seq >> 16) & 0xFF;
            ++seq;

            radio->send_packet(reinterpret_cast<const uint8_t*>(&tx_frame), header_overhead + n);
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

