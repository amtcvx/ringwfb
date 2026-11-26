/*
g++ -std=c++20 -O2 wfb.cpp \
    -I../devourer/src \
    -I../devourer/hal \
    ../devourer/build/libdevourer.a \
    $(pkg-config --cflags --libs libusb-1.0) \
    -lpthread \
    -o wfb

gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420  ! \
  x265enc bitrate=2048 key-int-max=15 option-string="repeat-headers=1" ! rtph265pay name=pay0 pt=96 config-interval=-1 mtu=1400 ! udpsink port=5700 host=127.0.0.1

gst-launch-1.0 udpsrc uri=udp://127.0.0.1:5600 ! application/x-rtp, encoding-name=H265, payload=96 ! \
  rtph265depay ! h265parse ! avdec_h265 !  videoconvert ! autovideosink sync=false

*/
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdio>
#include <exception>

#include "WiFiDriver.h"
#include "IRtlRadio.h"
#include "RxPacket.h"

/*****************************************************************************/
// CONFIGURATION DU RÔLE DU NŒUD
// true  = Maître (ex: Drone) -> scanne le meilleur canal et émet en premier
// false = Suiveur (ex: Station Sol) -> découvre le canal du Maître et se verrouille
constexpr bool IS_MASTER = true;

// Signatures MAC (octet 10 de l'en-tête 802.11) inversées automatiquement selon le rôle
constexpr uint8_t LOCAL_MAC_0  = IS_MASTER ? 0x12 : 0x99;
constexpr uint8_t REMOTE_MAC_0 = IS_MASTER ? 0x99 : 0x12;

// Types de trames applicatives
constexpr uint8_t PKT_TYPE_DATA      = 0x01;
constexpr uint8_t PKT_TYPE_HEARTBEAT = 0x02;

/*****************************************************************************/
#pragma pack(push, 1)
struct MinimalRadiotapHeader {
    uint8_t it_version = 0;
    uint8_t it_pad = 0;
    uint16_t it_len = 8;
    uint32_t it_present = 0;
};

struct VideoPacketHeader {
    uint8_t wifi_hdr[24];   // 0..23 : En-tête 802.11 standard
    uint8_t tx_channel;     // 24    : Canal réel d'émission
    uint8_t pkt_type;       // 25    : PKT_TYPE_DATA ou PKT_TYPE_HEARTBEAT
    uint8_t seq[3];         // 26..28: Numéro de séquence 24 bits
};

struct FullTxFrame {
    MinimalRadiotapHeader rtap;
    VideoPacketHeader hdr;
    uint8_t payload[1450];
};
#pragma pack(pop)

/*****************************************************************************/
static uint16_t target[2][2] = {{0x0bda, 0x8812}, {0x0b05, 0x17d2}};
const std::vector<uint8_t> channels = {36, 40, 44, 48, 149, 153, 157, 161, 165};

static std::atomic<bool> g_running{true};
static std::atomic<uint8_t> g_locked_channel{0};
static std::atomic<bool> g_channel_found{false};

void handle_signal(int) {
  g_running.store(false);
}

constexpr uint8_t get_band_from_channel(uint8_t channel) {
  return (channel <= 14) ? 0 : 1;
}

/*****************************************************************************/
// Recherche et revendication simple de l'adaptateur USB
libusb_device_handle* claim_adapt(libusb_context* ctx) {
  libusb_device** list = nullptr;
  ssize_t count = libusb_get_device_list(ctx, &list);
  if (count < 0) return nullptr;

  libusb_device_handle* claimed_handle = nullptr;
  for (ssize_t i = 0; i < count; ++i) {
    libusb_device* dev = list[i];
    libusb_device_descriptor desc{};
    if (libusb_get_device_descriptor(dev, &desc) != 0) continue;

    bool is_target = (desc.idVendor == target[0][0] && desc.idProduct == target[0][1]) ||
                     (desc.idVendor == target[1][0] && desc.idProduct == target[1][1]);
    if (!is_target) continue;

    libusb_device_handle* handle = nullptr;
    if (libusb_open(dev, &handle) != 0) continue;

    libusb_set_auto_detach_kernel_driver(handle, 1);
    if (libusb_claim_interface(handle, 0) == 0) {
      claimed_handle = handle;
      break;
    } else {
      libusb_close(handle);
    }
  }
  libusb_free_device_list(list, 1);
  return claimed_handle;
}

// Ouverture avec réinitialisation matérielle USB (équivalent claim_interface_then_reset)
libusb_device_handle* open_and_reset_adapt(libusb_context* ctx) {
  libusb_device_handle* handle = claim_adapt(ctx);
  if (!handle) return nullptr;

  int rc = libusb_reset_device(handle);
  if (rc == 0) {
    libusb_claim_interface(handle, 0);
    return handle;
  } else if (rc == LIBUSB_ERROR_NOT_FOUND) {
    // Ré-énumération USB après reset : on ferme et on rouvre proprement
    libusb_close(handle);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    return claim_adapt(ctx);
  } else {
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    return nullptr;
  }
}

/*****************************************************************************/
// Scan des canaux pour le nœud Maître (suppose InitWrite déjà appelé)
uint8_t scan_best_channel(std::unique_ptr<IRadio>& radio) {
  const devourer::AdapterCaps caps = radio->GetAdapterCaps();
  auto* rtl_radio = caps.rx_energy_ok ? dynamic_cast<IRtlRadio*>(radio.get()) : nullptr;

  uint8_t best_channel = channels[0];
  float best_score = 1e9f;
  const uint32_t dwell_us = 25000;

  for (uint8_t ch : channels) {
    radio->FastRetune(ch);
    std::this_thread::sleep_for(std::chrono::microseconds(1500));
    if (rtl_radio) rtl_radio->GetRxEnergy(false);

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
    if (score < best_score) {
      best_score = score;
      best_channel = ch;
    }
  }
  return best_channel;
}

// Balayage passif pour le nœud Suiveur (suppose InitWrite et StartRxLoop actifs)
uint8_t discover_channel(std::unique_ptr<IRadio>& radio) {
  size_t idx = 0;
  printf("Recherche du signal maître sur les canaux 5 GHz...\n");

  while (g_running.load() && !g_channel_found.load()) {
    radio->FastRetune(channels[idx]);
    for (int i = 0; i < 10 && g_running.load() && !g_channel_found.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    idx = (idx + 1) % channels.size();
  }
  return g_locked_channel.load();
}

/*****************************************************************************/
void apply_radio_params(uint8_t channel, std::unique_ptr<IRadio>& radio) {
  radio->FastRetune(channel);

  devourer::TxMode tx_mode{};
  tx_mode.mode   = devourer::TxMode::Mode::HT;
  tx_mode.ht_mcs = 3;
  tx_mode.bw_mhz = 20;
  tx_mode.sgi    = false;
  tx_mode.ldpc   = true;
  tx_mode.stbc   = true;
  radio->SetTxMode(tx_mode);

  radio->SetTxPower(20);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void init_tx_frame(uint8_t channel, FullTxFrame& tx_frame) {
  std::memset(&tx_frame.hdr, 0, sizeof(VideoPacketHeader));
  tx_frame.hdr.wifi_hdr[0] = 0x08; // Type: Data
  std::memset(&tx_frame.hdr.wifi_hdr[4], 0xFF, 6); // Destination: Broadcast
  tx_frame.hdr.wifi_hdr[10] = LOCAL_MAC_0;
  tx_frame.hdr.wifi_hdr[11] = 0x34;
  tx_frame.hdr.wifi_hdr[12] = 0x56;
  tx_frame.hdr.tx_channel   = channel;
}

int open_udp_rx_socket(uint16_t local_port, int timeout_ms) {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock >= 0) {
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
      close(sock);
      return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(local_port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      close(sock);
      return -1;
    }
  }
  return sock;
}

/*****************************************************************************/
int main() {
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  libusb_context* ctx = nullptr;
  if (libusb_init(&ctx) < 0) return -1;

  libusb_device_handle* handle = open_and_reset_adapt(ctx);
  if (!handle) {
    std::fprintf(stderr, "Adaptateur Wi-Fi introuvable ou échec du reset USB.\n");
    libusb_exit(ctx);
    return -1;
  }

  try {
    auto logger = std::make_shared<Logger>();
    WiFiDriver driver(logger);
    std::unique_ptr<IRadio> radio = driver.CreateRadio(handle, ctx);

    if (radio) {
      // 1. Bring-up matériel UNIQUE dans le thread principal AVANT tout thread RX
      SelectedChannel init_ch{};
      init_ch.Channel = channels[0];
      init_ch.ChannelOffset = 0;
      init_ch.ChannelWidth = CHANNEL_WIDTH_20;
      init_ch.Band = get_band_from_channel(channels[0]);
      radio->InitWrite(init_ch);

      // 2. Préparation des sockets UDP (5700 = entrée TX, 5701 = sortie RX)
      // Timeout court de 20 ms pour permettre l'envoi de balises heartbeat si besoin
      int udp_tx_in  = open_udp_rx_socket(5700, 20);
      int udp_rx_out = socket(AF_INET, SOCK_DGRAM, 0);

      sockaddr_in rx_dest{};
      rx_dest.sin_family = AF_INET;
      rx_dest.sin_port = htons(5701);
      rx_dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

      // 3. Démarrage du thread de réception radio (après InitWrite)
      std::thread rx_thread([&radio, udp_rx_out, &rx_dest]() {
        radio->StartRxLoop([udp_rx_out, &rx_dest](const Packet& pkt) {
          if (pkt.RxAtrib.pkt_rpt_type != RX_PACKET_TYPE::NORMAL_RX || pkt.RxAtrib.crc_err) {
            return;
          }

          size_t frame_len = pkt.Data.size();
          if (pkt.RxAtrib.fcs_present) {
            if (frame_len < 4) return;
            frame_len -= 4;
          }

          if (frame_len < sizeof(VideoPacketHeader)) return;

          const auto* hdr = reinterpret_cast<const VideoPacketHeader*>(pkt.Data.data());

          // Filtrage strict sur la signature MAC de l'autre extrémité
          if (hdr->wifi_hdr[0] == 0x08 &&
              hdr->wifi_hdr[10] == REMOTE_MAC_0 &&
              hdr->wifi_hdr[11] == 0x34 &&
              hdr->wifi_hdr[12] == 0x56) {

            // Verrouillage du canal si pas encore fait (utile pour le Suiveur)
            if (!g_channel_found.load()) {
              g_locked_channel.store(hdr->tx_channel);
              g_channel_found.store(true);
            }

            // Si c'est un paquet de données (pas une balise), transfert vers UDP 5701
            if (hdr->pkt_type == PKT_TYPE_DATA && frame_len > sizeof(VideoPacketHeader)) {
              const uint8_t* payload = pkt.Data.data() + sizeof(VideoPacketHeader);
              size_t payload_len = frame_len - sizeof(VideoPacketHeader);
              sendto(udp_rx_out, payload, payload_len, 0,
                reinterpret_cast<const sockaddr*>(&rx_dest), sizeof(rx_dest));
            }
          }
        });
      });

      // Bloc try interne pour garantir le join() du rx_thread en cas d'exception
      try {
        uint8_t active_channel = 0;
        if constexpr (IS_MASTER) {
          active_channel = scan_best_channel(radio);
          g_locked_channel.store(active_channel);
          g_channel_found.store(true);
          printf("[MAÎTRE] Canal sélectionné : %d\n", active_channel);
        } else {
          active_channel = discover_channel(radio);
          if (g_running.load() && active_channel != 0) {
            printf("[SUIVEUR] Verrouillé sur le canal : %d\n", active_channel);
          }
        }

        if (g_running.load() && active_channel != 0 && udp_tx_in >= 0) {
          apply_radio_params(active_channel, radio);
          FullTxFrame tx_frame{};
          init_tx_frame(active_channel, tx_frame);
          uint32_t seq = 0;
          const size_t header_overhead = sizeof(MinimalRadiotapHeader) + sizeof(VideoPacketHeader);

          while (g_running.load()) {
            ssize_t n = recv(udp_tx_in, tx_frame.payload, sizeof(tx_frame.payload), 0);
            if (n > 0) {
              // Données UDP reçues -> émission immédiate
              tx_frame.hdr.pkt_type = PKT_TYPE_DATA;
              tx_frame.hdr.seq[0] = seq & 0xFF;
              tx_frame.hdr.seq[1] = (seq >> 8) & 0xFF;
              tx_frame.hdr.seq[2] = (seq >> 16) & 0xFF;
              ++seq;
              radio->send_packet(reinterpret_cast<const uint8_t*>(&tx_frame), header_overhead + n);
            } else if (IS_MASTER && g_running.load()) {
              // Timeout 20 ms atteint sans trafic vidéo : le Maître émet une balise légère
              // pour permettre au Suiveur d'accrocher le canal rapidement
              tx_frame.hdr.pkt_type = PKT_TYPE_HEARTBEAT;
              tx_frame.hdr.seq[0] = seq & 0xFF;
              tx_frame.hdr.seq[1] = (seq >> 8) & 0xFF;
              tx_frame.hdr.seq[2] = (seq >> 16) & 0xFF;
              ++seq;
              radio->send_packet(reinterpret_cast<const uint8_t*>(&tx_frame), header_overhead);
            }
          }
        }
      } catch (...) {
        if (udp_tx_in >= 0) close(udp_tx_in);
          radio->StopRxLoop();
          if (rx_thread.joinable()) rx_thread.join();
          if (udp_rx_out >= 0) close(udp_rx_out);
          throw;
        }

        if (udp_tx_in >= 0) close(udp_tx_in);
        radio->StopRxLoop();
        if (rx_thread.joinable()) rx_thread.join();
        if (udp_rx_out >= 0) close(udp_rx_out);
        radio.reset();
      }
    } catch (const std::exception& e) {
      std::fprintf(stderr, "Exception radio : %s\n", e.what());
    }
    printf("Arrêt propre terminé.\n");
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    return 0;
}
