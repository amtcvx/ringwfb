/*
sudo apt update && sudo apt install -y openssh-server

g++ -std=c++20 -O2 tunnel_ping_udp.cpp \
    -I../devourer/src \
    -I../devourer/hal \
    ../devourer/build/libdevourer.a \
    $(pkg-config --cflags --libs libusb-1.0) \
    -lpthread \
    -o tunnel_ping_udp

sudo ./tunnel_ping_udp

ping 10.250.12.7

ssh pprz@10.250.12.7

gst-launch-1.0 udpsrc uri=udp://127.0.0.1:5601 ! application/x-rtp, encoding-name=H265, payload=96 ! \
  rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false

gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420  ! \
  x265enc bitrate=2048 ! rtph265pay name=pay0 pt=96 config-interval=1 mtu=1400 ! udpsink port=5600 host=127.0.0.1

*/
#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <libusb-1.0/libusb.h>

#include "WiFiDriver.h"
#include "RxPacket.h"
#include "logger.h"

// Ports locaux pour interfaçage GStreamer (udpsink -> 5600 / 5601 -> udpsrc)
constexpr uint16_t LOCAL_UDP_SRC_PORT = 5600;  // Écoute locale (reçoit depuis GStreamer udpsink)
constexpr uint16_t LOCAL_UDP_SINK_PORT = 5601; // Sortie locale (envoie vers GStreamer udpsrc)

// Index MCS par défaut (20 MHz, Short GI, STBC) :
// 0 = 7.2 Mbps | 1 = 14.4 Mbps | 2 = 21.7 Mbps (recommandé HD) | 3 = 28.9 Mbps
constexpr uint8_t DEFAULT_MCS_INDEX = 2;

// Construit l'en-tête Radiotap MCS (13 octets) : TX_FLAGS (0x8000) + MCS (0x80000)
std::array<uint8_t, 13> make_radiotap_mcs(uint8_t mcs_index) {
    // Active STBC (1 flux sur 2 antennes) uniquement pour MCS 0..7 (1x1)
    uint8_t mcs_known = (mcs_index <= 7) ? 0x17 : 0x07; // BW | MCS | GI | (STBC)
    uint8_t mcs_flags = (mcs_index <= 7) ? 0x24 : 0x04; // 20 MHz | Short GI | (1 flux STBC)

    return {
        0x00, 0x00,             // Version (0) + Pad (0)
        0x0d, 0x00,             // Longueur totale : 13 octets (0x0d)
        0x00, 0x80, 0x08, 0x00, // Present flags : TX_FLAGS (bit 15) + MCS (bit 19)
        0x08, 0x00,             // TX_FLAGS : NO_ACK (0x0008)
        mcs_known,              // MCS known
        mcs_flags,              // MCS flags
        mcs_index               // Index MCS
    };
}

// Distinction des flux via addr1
const std::array<uint8_t, 6> LINK_MAC_TUN = { 0x02, 0x44, 0x45, 0x56, 0x00, 0x01 };
const std::array<uint8_t, 6> LINK_MAC_UDP = { 0x02, 0x44, 0x45, 0x56, 0x00, 0x02 };

// Canaux 5 GHz (prioritaires) + canaux 2.4 GHz
const std::vector<uint8_t> SCAN_CHANNELS = {
    36, 40, 44, 48, 149, 153, 157, 161, 165,
    1, 6, 11, 13
};

// En-tête 802.11 QoS Data (26 octets) requis par RTL8812AU pour les débits MCS
struct __attribute__((packed)) WifiDataHdr {
    uint16_t fc = 0x0088;          // 0x0088 = Data + QoS (indispensable pour MCS sur RTL8812AU)
    uint16_t dur = 0x0000;
    std::array<uint8_t, 6> addr1;  // Destination
    std::array<uint8_t, 6> addr2;  // Source
    std::array<uint8_t, 6> addr3;  // BSSID
    uint16_t seq = 0x0000;
    uint16_t qos = 0x0000;         // QoS Control (2 octets)
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

int open_udp_socket(uint16_t bind_port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Tampon de réception élargi (1 Mo) pour absorber les pics vidéo HD (I-frames)
    int rcvbuf = 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    addr.sin_port = htons(bind_port);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
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
    // Arguments optionnels : ./tunnel [canal] [mcs_index]
    uint8_t mcs_index = DEFAULT_MCS_INDEX;
    if (argc >= 3) {
        mcs_index = static_cast<uint8_t>(std::stoi(*(argv + 2)));
    }
    const auto radiotap_hdr = make_radiotap_mcs(mcs_index);

    int tun_fd = open_tun("tun0");
    if (tun_fd < 0) {
        std::cerr << "Erreur /dev/net/tun (root requis)\n";
        return 1;
    }

    int udp_sock = open_udp_socket(LOCAL_UDP_SRC_PORT);
    if (udp_sock < 0) {
        std::cerr << "Erreur ouverture socket UDP sur le port " << LOCAL_UDP_SRC_PORT << "\n";
        return 1;
    }

    sockaddr_in udp_sink_dest{};
    udp_sink_dest.sin_family = AF_INET;
    udp_sink_dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    udp_sink_dest.sin_port = htons(LOCAL_UDP_SINK_PORT);

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
    std::mutex tx_mutex;
    std::atomic<uint16_t> global_seq{0};

    // Fonction d'envoi thread-safe (Radiotap MCS + Trame QoS Data 0x0088)
    auto send_wifi_payload = [&](const std::array<uint8_t, 6>& dst_mac, const uint8_t* data, size_t len) {
        WifiDataHdr hdr{};
        hdr.fc = 0x0088; // QoS Data
        hdr.addr1 = dst_mac;
        hdr.addr2 = LINK_MAC_TUN;
        hdr.addr3 = LINK_MAC_TUN;
        hdr.seq = (global_seq.fetch_add(1) & 0x0FFF) << 4;
        // File matérielle vidéo haute priorité (TID 5) pour UDP, Best Effort (TID 0) pour tun0
        hdr.qos = (dst_mac == LINK_MAC_UDP) ? 0x0005 : 0x0000;

        std::vector<uint8_t> frame;
        frame.reserve(radiotap_hdr.size() + sizeof(WifiDataHdr) + len);
        frame.insert(frame.end(), radiotap_hdr.begin(), radiotap_hdr.end());
        frame.insert(frame.end(), reinterpret_cast<uint8_t*>(&hdr),
                     reinterpret_cast<uint8_t*>(&hdr) + sizeof(WifiDataHdr));
        frame.insert(frame.end(), data, data + len);

        std::lock_guard<std::mutex> lock(tx_mutex);
        dev->send_packet(frame.data(), frame.size());
    };

    std::atomic<bool> in_scan_mode{true};
    std::atomic<uint32_t> scan_noise_pkts{0};
    std::atomic<int32_t> scan_noise_energy{0};
    std::atomic<bool> scan_peer_found{false};
    std::string peer_ip_announced = "";

    auto main_rx_cb = [&](const Packet& pkt) {
        if (in_scan_mode) {
            if (pkt.Data.size() >= sizeof(WifiDataHdr)) {
                const auto* hdr = reinterpret_cast<const WifiDataHdr*>(pkt.Data.data());
                if ((hdr->addr1 == LINK_MAC_TUN || hdr->addr1 == LINK_MAC_UDP) && hdr->addr2 == LINK_MAC_TUN) {
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
        if (hdr->addr2 != LINK_MAC_TUN) return;

        const uint8_t* payload = pkt.Data.data() + sizeof(WifiDataHdr);
        size_t payload_len = pkt.Data.size() - sizeof(WifiDataHdr);

        // 1. Flux UDP reçu par radio -> transfert vers udpsrc local (port 5601)
        if (hdr->addr1 == LINK_MAC_UDP) {
          (void)sendto(udp_sock, payload, payload_len, 0,
            reinterpret_cast<const sockaddr*>(&udp_sink_dest), sizeof(udp_sink_dest));
          return;
        }

        // 2. Flux IP / Balise reçu par radio -> transfert vers tun0
        if (hdr->addr1 == LINK_MAC_TUN) {
          if (payload_len > 5 && memcmp(payload, "PING:", 5) == 0) {
            std::string remote_ip(reinterpret_cast<const char*>(payload + 5), payload_len - 5);
            if (remote_ip != local_ip && remote_ip != peer_ip_announced) {
              peer_ip_announced = remote_ip;
              std::cout << "\n[Liaison active] Pair détecté -> ssh root@" << remote_ip << "\n";
            }
            return;
          }
          (void)write(tun_fd, payload, payload_len);
        }
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

  std::thread rx_thread([&] {
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
    << " | Modulation : MCS " << (int)mcs_index << (mcs_index <= 7 ? " (STBC 20 MHz)" : " (MIMO 2x2 20 MHz)")
    << "\n[UDP] Écoute (udpsink -> radio) : 127.0.0.1:" << LOCAL_UDP_SRC_PORT
    << " | Sortie (radio -> udpsrc) : 127.0.0.1:" << LOCAL_UDP_SINK_PORT << "\n";

  // Thread 1 : Balise de découverte
  std::thread beacon_thread([&] {
    std::string msg = "PING:" + local_ip;
    while (true) {
      send_wifi_payload(LINK_MAC_TUN, reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  });
  beacon_thread.detach();

  // Thread 2 : Réception UDP locale (udpsink) -> Émission Wi-Fi
  std::thread udp_tx_thread([&] {
    std::vector<uint8_t> udp_buf(1500);
    while (true) {
      ssize_t n = recv(udp_sock, udp_buf.data(), udp_buf.size(), 0);
      if (n > 0) {
        send_wifi_payload(LINK_MAC_UDP, udp_buf.data(), static_cast<size_t>(n));
      }
    }
  });
  udp_tx_thread.detach();

  // Boucle principale : Lecture tun0 -> Émission Wi-Fi
  std::vector<uint8_t> ip_buf(1500);
  while (true) {
    ssize_t n = read(tun_fd, ip_buf.data(), ip_buf.size());
    if (n > 0) {
      send_wifi_payload(LINK_MAC_TUN, ip_buf.data(), static_cast<size_t>(n));
    }
  }
  return 0;
}
