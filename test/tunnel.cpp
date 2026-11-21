/*
sudo apt update && sudo apt install -y openssh-server

g++ -std=c++20 -O2 tunnel.cpp \
    -I../devourer/src \
    -I../devourer/hal \
    ../devourer/build/libdevourer.a \
    $(pkg-config --cflags --libs libusb-1.0) \
    -lpthread \
    -o tunnel

sudo ./tunnel

ping 10.250.12.7
ssh pprz@10.250.12.7

*/
#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <thread>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <libusb-1.0/libusb.h>

#include "WiFiDriver.h"
#include "RxPacket.h"
#include "logger.h"

// En-tête Radiotap (12 octets) avec débit fixe 6 Mbps OFDM + drapeau NO_ACK
const std::array<uint8_t, 12> RADIOTAP_6MBPS = {
    0x00, 0x00,             // Version + Pad
    0x0c, 0x00,             // Longueur totale : 12 octets
    0x04, 0x80, 0x00, 0x00, // Present flags : RATE + TX_FLAGS
    0x0c,                   // Débit fixe : 6 Mbps (0x0c = 12 * 0.5 Mbps)
    0x00,                   // Alignement
    0x08, 0x00              // TX_FLAGS : NO_ACK (0x0008)
};

const std::array<uint8_t, 6> LINK_MAC = { 0x02, 0x44, 0x45, 0x56, 0x00, 0x01 };

// Canaux 5 GHz (prioritaires) + canaux 2.4 GHz (1, 6, 11, 13)
const std::vector<uint8_t> SCAN_CHANNELS = {
    36, 40, 44, 48, 149, 153, 157, 161, 165,
    1, 6, 11, 13
};

struct __attribute__((packed)) WifiDataHdr {
    uint16_t fc = 0x0008;
    uint16_t dur = 0x0000;
    std::array<uint8_t, 6> addr1;
    std::array<uint8_t, 6> addr2;
    std::array<uint8_t, 6> addr3;
    uint16_t seq = 0x0000;
};

int open_tun(const char* dev_name) {
    struct ifreq ifr{};
    int fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) return -1;
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    strncpy(ifr.ifr_name, dev_name, IFNAMSIZ);
    if (ioctl(fd, TUNSETIFF, (void*)&ifr) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

std::string derive_unique_ipv4() {
    uint32_t hash = 2166136261u;
    auto feed = [&](const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        char c;
        while (f.get(c)) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 16777619u;
        }
    };
    feed("/etc/machine-id");
    feed("/proc/cpuinfo");

    uint8_t oct3 = (hash >> 8) & 0xFF;
    uint8_t oct4 = (hash & 0xFE) | 0x01;
    if (oct3 == 0 && oct4 == 0) oct4 = 1;

    std::array<char, 32> ip{};
    snprintf(ip.data(), ip.size(), "10.250.%u.%u", oct3, oct4);
    return std::string(ip.data());
}

// Ouvre en priorité 0b05:17d2 (ASUS USB-AC56 RTL8812AU), sinon tout adaptateur Devourer
libusb_device_handle* open_target_usb(libusb_context* ctx, WiFiDriver& driver) {
    // 1. Recherche directe et instantanée du 0b05:17d2
    libusb_device_handle* direct = libusb_open_device_with_vid_pid(ctx, 0x0b05, 0x17d2);
    if (direct) {
        if (libusb_kernel_driver_active(direct, 0) == 1) {
            libusb_detach_kernel_driver(direct, 0);
        }
        if (libusb_claim_interface(direct, 0) == 0) {
            std::cout << "[USB] Adaptateur 0b05:17d2 (ASUS USB-AC56) verrouillé\n";
            return direct;
        }
        libusb_close(direct);
    }

    // 2. Fallback : recherche générique si l'autre extrémité utilise une autre carte
    libusb_device** list = nullptr;
    ssize_t cnt = libusb_get_device_list(ctx, &list);
    libusb_device_handle* found_handle = nullptr;

    for (ssize_t i = 0; i < cnt; ++i) {
        libusb_device_handle* handle = nullptr;
        if (libusb_open(*(list + i), &handle) == 0) {
            if (libusb_kernel_driver_active(handle, 0) == 1) {
                libusb_detach_kernel_driver(handle, 0);
            }
            if (libusb_claim_interface(handle, 0) == 0) {
                try {
                    auto test_radio = driver.CreateRadio(handle);
                    if (test_radio) {
                        found_handle = handle;
                        break;
                    }
                } catch (...) {}
                libusb_release_interface(handle, 0);
            }
            libusb_close(handle);
        }
    }
    libusb_free_device_list(list, 1);
    return found_handle;
}

bool try_set_channel(IRadio* dev, uint8_t ch) {
    try {
        dev->SetMonitorChannel(SelectedChannel{
            .Channel = ch,
            .ChannelOffset = 0,
            .ChannelWidth = CHANNEL_WIDTH_20,
        });
        return true;
    } catch (...) {
        return false;
    }
}

int main(int argc, char** argv) {
    int tun_fd = open_tun("tun0");
    if (tun_fd < 0) {
        std::cerr << "Erreur /dev/net/tun (root requis)\n";
        return 1;
    }

    std::string local_ip = derive_unique_ipv4();
    std::string cmd = "ip addr flush dev tun0 && "
                      "ip addr add " + local_ip + "/16 broadcast 10.250.255.255 dev tun0 && "
                      "ip link set dev tun0 mtu 1400 up";
    (void)system(cmd.c_str());

    libusb_context* ctx = nullptr;
    libusb_init(&ctx);
    auto logger = std::make_shared<Logger>();
    WiFiDriver driver(logger);

    libusb_device_handle* handle = open_target_usb(ctx, driver);
    if (!handle) {
        std::cerr << "Aucun adaptateur Wi-Fi compatible Devourer trouvé\n";
        return 1;
    }

    auto dev = driver.CreateRadio(handle);

    std::atomic<bool> in_scan_mode{true};
    std::atomic<uint32_t> scan_noise_pkts{0};
    std::atomic<int32_t> scan_noise_energy{0};
    std::atomic<bool> scan_peer_found{false};
    std::string peer_ip_announced = "";

    auto main_rx_cb = [&](const Packet& pkt) {
        if (in_scan_mode) {
            if (pkt.Data.size() >= sizeof(WifiDataHdr)) {
                const auto* hdr = reinterpret_cast<const WifiDataHdr*>(pkt.Data.data());
                if (hdr->addr1 == LINK_MAC && hdr->addr2 == LINK_MAC) {
                    scan_peer_found = true;
                    return;
                }
            }
            scan_noise_pkts++;
            int rssi = std::max(static_cast<int>(*(pkt.RxAtrib.rssi)),
                                static_cast<int>(*(pkt.RxAtrib.rssi + 1)));
            scan_noise_energy += std::max(1, 100 + rssi);
            return;
        }

        if (pkt.Data.size() <= sizeof(WifiDataHdr)) return;
        const auto* hdr = reinterpret_cast<const WifiDataHdr*>(pkt.Data.data());
        if (hdr->addr1 != LINK_MAC || hdr->addr2 != LINK_MAC) return;

        const uint8_t* ip_payload = pkt.Data.data() + sizeof(WifiDataHdr);
        size_t ip_len = pkt.Data.size() - sizeof(WifiDataHdr);

        if (ip_len > 5 && memcmp(ip_payload, "PING:", 5) == 0) {
            std::string remote_ip(reinterpret_cast<const char*>(ip_payload + 5), ip_len - 5);
            if (remote_ip != local_ip && remote_ip != peer_ip_announced) {
                peer_ip_announced = remote_ip;
                std::cout << "\n[Liaison active] Pair détecté -> ssh root@" << remote_ip << "\n";
            }
            return;
        }

        (void)write(tun_fd, ip_payload, ip_len);
    };

    uint8_t init_ch = 0;
    for (uint8_t ch : SCAN_CHANNELS) {
        try {
            dev->InitWrite(SelectedChannel{
                .Channel = ch,
                .ChannelOffset = 0,
                .ChannelWidth = CHANNEL_WIDTH_20,
            });
            init_ch = ch;
            break;
        } catch (...) {}
    }
    if (init_ch == 0) {
        std::cerr << "Impossible d'initialiser un canal Wi-Fi sur cet adaptateur\n";
        return 1;
    }

    std::thread rx_thread([&]() {
        dev->StartRxLoop(main_rx_cb);
    });
    rx_thread.detach();

    uint8_t active_channel = init_ch;
    if (argc >= 2) {
        active_channel = static_cast<uint8_t>(std::stoi(*(argv + 1)));
        if (!try_set_channel(dev.get(), active_channel)) {
            std::cerr << "Canal " << (int)active_channel << " non supporté par la carte\n";
            return 1;
        }
    } else {
        uint64_t best_score = UINT64_MAX;
        for (uint8_t ch : SCAN_CHANNELS) {
            if (!try_set_channel(dev.get(), ch)) continue;

            scan_noise_pkts = 0;
            scan_noise_energy = 0;
            scan_peer_found = false;

            for (int t = 0; t < 18; ++t) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (scan_peer_found) {
                    active_channel = ch;
                    goto channel_selected;
                }
            }
            uint64_t score = static_cast<uint64_t>(scan_noise_pkts) * 50 + scan_noise_energy;
            if (score < best_score) {
                best_score = score;
                active_channel = ch;
            }
        }
    }

channel_selected:
    in_scan_mode = false;
    try_set_channel(dev.get(), active_channel);

    bool is_24ghz = (active_channel <= 14);
    std::cout << "[Prêt] IP : " << local_ip
              << " | Canal : " << (int)active_channel << (is_24ghz ? " (2.4 GHz)" : " (5 GHz)")
              << " | Débit fixe : 6 Mbps\n";

    std::thread beacon_thread([&]() {
        std::string msg = "PING:" + local_ip;
        WifiDataHdr bhdr{};
        bhdr.addr1 = LINK_MAC;
        bhdr.addr2 = LINK_MAC;
        bhdr.addr3 = LINK_MAC;

        std::vector<uint8_t> bframe;
        bframe.insert(bframe.end(), RADIOTAP_6MBPS.begin(), RADIOTAP_6MBPS.end());
        bframe.insert(bframe.end(), reinterpret_cast<uint8_t*>(&bhdr), reinterpret_cast<uint8_t*>(&bhdr) + sizeof(WifiDataHdr));
        bframe.insert(bframe.end(), msg.begin(), msg.end());

        while (true) {
            dev->send_packet(bframe.data(), bframe.size());
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });
    beacon_thread.detach();

    std::vector<uint8_t> ip_buf(1500);
    std::vector<uint8_t> tx_frame;
    WifiDataHdr hdr{};
    hdr.addr1 = LINK_MAC;
    hdr.addr2 = LINK_MAC;
    hdr.addr3 = LINK_MAC;
    uint16_t seq_num = 0;

    while (true) {
        ssize_t n = read(tun_fd, ip_buf.data(), ip_buf.size());
        if (n <= 0) continue;

        hdr.seq = (seq_num++ & 0x0FFF) << 4;

        tx_frame.clear();
        tx_frame.insert(tx_frame.end(), RADIOTAP_6MBPS.begin(), RADIOTAP_6MBPS.end());
        tx_frame.insert(tx_frame.end(), reinterpret_cast<uint8_t*>(&hdr),
                        reinterpret_cast<uint8_t*>(&hdr) + sizeof(WifiDataHdr));
        tx_frame.insert(tx_frame.end(), ip_buf.begin(), ip_buf.begin() + n);

        dev->send_packet(tx_frame.data(), tx_frame.size());
    }
    return 0;
}

