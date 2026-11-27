/*
g++ -std=c++20 -O2 wfb.cpp \
    -I../devourer/src \
    -I../devourer/hal \
    ../devourer/build/libdevourer.a \
    $(pkg-config --cflags --libs libusb-1.0) \
    -lpthread \
    -o wfb

./wfb master
gst-launch-1.0 videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420  ! \
  x265enc bitrate=2048 key-int-max=15 option-string="repeat-headers=1" ! rtph265pay name=pay0 pt=96 config-interval=-1 mtu=1400 ! udpsink port=5700 host=127.0.0.1

./wfb slave
gst-launch-1.0 udpsrc uri=udp://127.0.0.1:5701 ! application/x-rtp, encoding-name=H265, payload=96 ! \
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
#include <string>
#include <cinttypes>

#include "WiFiDriver.h"
#include "IRtlRadio.h"
#include "RxPacket.h"

/*****************************************************************************/
// Paramètres de synchronisation et d'anti-rebond
constexpr uint8_t PKT_TYPE_DATA          = 0x01;
constexpr uint8_t PKT_TYPE_HEARTBEAT     = 0x02;
constexpr int64_t LINK_LOST_TIMEOUT_MS   = 1000; // Perte de lien après 1 s sans réception
constexpr int     DEBOUNCE_REQUIRED_PKTS = 3;    // Nombre de trames consécutives pour verrouiller
constexpr int64_t DEBOUNCE_WINDOW_MS     = 150;  // Fenêtre max entre deux trames de confirmation

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

static std::atomic<bool>    g_running{true};
static std::atomic<uint8_t> g_scanning_channel{0};
static std::atomic<uint8_t> g_locked_channel{0};
static std::atomic<bool>    g_channel_found{false};
static std::atomic<int64_t> g_last_rx_ms{0};

void handle_signal(int) {
  g_running.store(false);
}

int64_t now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

constexpr uint8_t get_band_from_channel(uint8_t channel) {
  return (channel <= 14) ? 0 : 1;
}

/*****************************************************************************/
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

libusb_device_handle* open_and_reset_adapt(libusb_context* ctx) {
  libusb_device_handle* handle = claim_adapt(ctx);
  if (!handle) return nullptr;

  int rc = libusb_reset_device(handle);
  if (rc == 0) {
    libusb_claim_interface(handle, 0);
    return handle;
  } else if (rc == LIBUSB_ERROR_NOT_FOUND) {
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

// Balayage passif avec temps d'arrêt suffisant pour accumuler DEBOUNCE_REQUIRED_PKTS
uint8_t discover_channel(std::unique_ptr<IRadio>& radio) {
  size_t idx = 0;
  printf("[SUIVEUR] Balayage des canaux en cours...\n");

  while (g_running.load() && !g_channel_found.load()) {
    uint8_t ch = channels[idx];

    // On indique au callback RX quel canal est actuellement écouté (anti-fuite)
    g_scanning_channel.store(ch);
    radio->FastRetune(ch);

    // Temps d'écoute de 75 ms par canal (permet de recevoir au moins 3 heartbeats de 20 ms)
    for (int i = 0; i < 15 && g_running.load() && !g_channel_found.load(); ++i) {
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

void init_tx_frame(uint8_t channel, uint8_t local_mac_0, FullTxFrame& tx_frame) {
  std::memset(&tx_frame.hdr, 0, sizeof(VideoPacketHeader));
  tx_frame.hdr.wifi_hdr[0] = 0x08; // Type: Data
  std::memset(&tx_frame.hdr.wifi_hdr[4], 0xFF, 6); // Destination: Broadcast
  tx_frame.hdr.wifi_hdr[10] = local_mac_0;
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
int main(int argc, char* argv[]) {
  bool is_master = false;
  if (argc > 1 && std::string(argv[1]) == "master") {
    is_master = true;
  }

  const uint8_t local_mac_0  = is_master ? 0x12 : 0x99;
  const uint8_t remote_mac_0 = is_master ? 0x99 : 0x12;

  printf("Démarrage en mode : %s\n", is_master ? "MAÎTRE" : "SUIVEUR");

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
      SelectedChannel init_ch{};
      init_ch.Channel = channels[0];
      init_ch.ChannelOffset = 0;
      init_ch.ChannelWidth = CHANNEL_WIDTH_20;
      init_ch.Band = get_band_from_channel(channels[0]);
      radio->InitWrite(init_ch);

      int udp_tx_in  = open_udp_rx_socket(5700, 20);
      int udp_rx_out = socket(AF_INET, SOCK_DGRAM, 0);

      sockaddr_in rx_dest{};
      rx_dest.sin_family = AF_INET;
      rx_dest.sin_port = htons(5701);
      rx_dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

      // Variables d'état locales au thread RX pour l'anti-rebond
      uint8_t debounce_cand_ch  = 0;
      int     debounce_hits     = 0;
      int64_t debounce_last_ms  = 0;

      std::thread rx_thread([&]() {
        radio->StartRxLoop([&](const Packet& pkt) {

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

          // Filtrage MAC de l'interlocuteur
	  if (hdr->wifi_hdr[0]  != 0x08 ||
            hdr->wifi_hdr[10] != remote_mac_0 ||
            hdr->wifi_hdr[11] != 0x34 ||
            hdr->wifi_hdr[12] != 0x56) {
            return;
          }

          int64_t t_now = now_ms();

          // Logique de découverte avec anti-rebond (uniquement si non verrouillé)
          if (!g_channel_found.load()) {
            // 1. Rejeter toute trame résiduelle ne correspondant pas au canal actuellement scanné
            if (hdr->tx_channel != g_scanning_channel.load()) {
              debounce_hits = 0;
              return;
            }
            // 2. Vérifier la continuité sur le même canal dans la fenêtre temporelle
            if (hdr->tx_channel == debounce_cand_ch &&
              (t_now - debounce_last_ms) <= DEBOUNCE_WINDOW_MS) {
              ++debounce_hits;
            } else {
              debounce_cand_ch = hdr->tx_channel;
              debounce_hits = 1;
            }

            debounce_last_ms = t_now;
            // 3. Verrouillage uniquement après DEBOUNCE_REQUIRED_PKTS confirmations
            if (debounce_hits >= DEBOUNCE_REQUIRED_PKTS) {
              g_locked_channel.store(debounce_cand_ch);
              g_last_rx_ms.store(t_now);
              g_channel_found.store(true);
              debounce_hits = 0;
            }
	    return; // On ne transfère pas de données tant que le verrouillage n'est pas confirmé
          }

          // Mode verrouillé : rejeter toute fuite provenant d'un autre canal
          if (hdr->tx_channel != g_locked_channel.load()) {
            return;
          }

          // Mise à jour du watchdog de présence du maître
          g_last_rx_ms.store(t_now);
          // Transfert des données applicatives vers UDP 5701
          if (hdr->pkt_type == PKT_TYPE_DATA && frame_len > sizeof(VideoPacketHeader)) {
            const uint8_t* payload = pkt.Data.data() + sizeof(VideoPacketHeader);
            size_t payload_len = frame_len - sizeof(VideoPacketHeader);
            sendto(udp_rx_out, payload, payload_len, 0,
            reinterpret_cast<const sockaddr*>(&rx_dest), sizeof(rx_dest));
          }

        });
      });


      // Boucle principale (permet la re-découverte infinie en cas de perte du maître)
      try {
        while (g_running.load() && udp_tx_in >= 0) {
          uint8_t active_channel = 0;
          if (is_master) {
            active_channel = scan_best_channel(radio);
            g_locked_channel.store(active_channel);
            g_channel_found.store(true);
            printf("[MAÎTRE] Canal sélectionné : %d\n", active_channel);
          } else {
            g_locked_channel.store(0);
            g_channel_found.store(false);
            active_channel = discover_channel(radio);
            if (!g_running.load() || active_channel == 0) break;
            printf("[SUIVEUR] Verrouillé (confirmé %d/%d) sur le canal : %d\n",
              DEBOUNCE_REQUIRED_PKTS, DEBOUNCE_REQUIRED_PKTS, active_channel);
          }

          apply_radio_params(active_channel, radio);
          FullTxFrame tx_frame{};
          init_tx_frame(active_channel, local_mac_0, tx_frame);
          uint32_t seq = 0;
          const size_t header_overhead = sizeof(MinimalRadiotapHeader) + sizeof(VideoPacketHeader);
          // Boucle d'exploitation active
          while (g_running.load()) {
            // Surveillance Watchdog côté Suiveur
            if (!is_master) {
              if ((now_ms() - g_last_rx_ms.load()) > LINK_LOST_TIMEOUT_MS) {
                printf("[SUIVEUR] Signal du maître perdu (> %" PRId64 " ms). Re-scan...\n",
                  LINK_LOST_TIMEOUT_MS);
                break; // Retour au balayage discover_channel()
              }
            }

            ssize_t n = recv(udp_tx_in, tx_frame.payload, sizeof(tx_frame.payload), 0);
            if (n > 0) {
              tx_frame.hdr.pkt_type = PKT_TYPE_DATA;
              tx_frame.hdr.seq[0] = seq & 0xFF;
              tx_frame.hdr.seq[1] = (seq >> 8) & 0xFF;
              tx_frame.hdr.seq[2] = (seq >> 16) & 0xFF;
              ++seq;
              radio->send_packet(reinterpret_cast<const uint8_t*>(&tx_frame), header_overhead + n);
            } else if (is_master && g_running.load()) {
              tx_frame.hdr.pkt_type = PKT_TYPE_HEARTBEAT;
              tx_frame.hdr.seq[0] = seq & 0xFF;
              tx_frame.hdr.seq[1] = (seq >> 8) & 0xFF;
              tx_frame.hdr.seq[2] = (seq >> 16) & 0xFF;
              ++seq;
              radio->send_packet(reinterpret_cast<const uint8_t*>(&tx_frame), header_overhead);
            }
          }

          // Le Maître ne re-scanne pas sauf s'il quitte
          if (is_master) break;
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
