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
#include <arpa/inet.h>
#include <libusb-1.0/libusb.h>

#include <exception>
#include <csignal>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <cstdio>

#include "WiFiDriver.h"
#include "IRtlRadio.h"

/*****************************************************************************/
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

/*****************************************************************************/
static uint16_t target[2][2] = {{0x0bda,0x8812},{0x0b05,0x17d2}};
const std::vector<uint8_t> channels = {36, 40, 44, 48, 149, 153, 157, 161, 165};

static std::atomic<bool> g_running{true};
void handle_signal(int) {
  g_running.store(false);
}

constexpr uint8_t get_band_from_channel(uint8_t channel) {
  return (channel <= 14) ? 0 : 1;
}

/*****************************************************************************/
libusb_device_handle* get_adapt(libusb_context* ctx) {

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
    if (libusb_claim_interface(handle, 0)==0) {
      claimed_handle = handle;
      break;
    } else libusb_close(handle);
  }
  libusb_free_device_list(list, 1);
  return claimed_handle;
}

/*****************************************************************************/
uint8_t scan_chan(std::unique_ptr<IRadio>& radio) {

  const devourer::AdapterCaps caps = radio->GetAdapterCaps();
  auto* rtl_radio = caps.rx_energy_ok ? dynamic_cast<IRtlRadio*>(radio.get()) : nullptr;
  SelectedChannel sel_ch{};
  sel_ch.Channel = channels[0];
  sel_ch.ChannelOffset = 0;
  sel_ch.ChannelWidth = CHANNEL_WIDTH_20;
  sel_ch.Band = get_band_from_channel(channels[0]);
  radio->InitWrite(sel_ch);

  uint8_t best_channel = channels[0];
  float best_score = 1e9f;
  const uint32_t dwell_us = 25000; // 25 ms par canal
  for (uint8_t ch : channels) {
    radio->FastRetune(ch);
    std::this_thread::sleep_for(std::chrono::microseconds(1500));
    if (rtl_radio) rtl_radio->GetRxEnergy(false); // Reset des compteurs delta FA/CCA
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
  return(best_channel);
}

/*****************************************************************************/
void set_chan(uint8_t best_channel, std::unique_ptr<IRadio>& radio) {

  radio->FastRetune(best_channel);

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
};

/*****************************************************************************/
void set_frame(uint8_t best_channel, FullTxFrame& tx_frame ) {

  std::memset(&tx_frame.hdr, 0, sizeof(VideoPacketHeader));
  tx_frame.hdr.wifi_hdr[0] = 0x08; // Type: Data
  std::memset(&tx_frame.hdr.wifi_hdr[4], 0xFF, 6); // Destination: Broadcast
  tx_frame.hdr.wifi_hdr[10] = 0x12;
  tx_frame.hdr.wifi_hdr[11] = 0x34;
  tx_frame.hdr.wifi_hdr[12] = 0x56;
  tx_frame.hdr.tx_channel = best_channel;
}

/*****************************************************************************/
int set_io() {

  int ret = socket(AF_INET, SOCK_DGRAM, 0);
  if (ret >= 0) {
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    if (setsockopt(ret, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) { close(ret); ret = -1; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5700);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(ret, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { close(ret); ret = -1; }
  }
  return(ret);
}	

/*****************************************************************************/
int main() {
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  libusb_context* ctx = nullptr;
  if (libusb_init(&ctx)  < 0) return (-1);
  libusb_device_handle* handle = get_adapt(ctx);
  if (!(handle)) { libusb_exit(ctx); return (-1); }

  int reset_rc = libusb_reset_device(handle);
  if (reset_rc != 0 && reset_rc != LIBUSB_ERROR_NOT_FOUND) {
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    return -1;
  }

  try {
    auto logger = std::make_shared<Logger>();
    WiFiDriver driver(logger);
    std::unique_ptr<IRadio> radio = driver.CreateRadio(handle, ctx);
    if (radio) { 
      uint8_t best_channel = scan_chan(radio);
      printf("1 (%d)\n",best_channel);
      set_chan(best_channel,radio);
      FullTxFrame tx_frame{};
      set_frame(best_channel,tx_frame);
      int udp_sock = set_io();
      if (udp_sock >= 0) {

        uint32_t seq = 0;
        const size_t header_overhead = sizeof(MinimalRadiotapHeader) + sizeof(VideoPacketHeader);

        while (g_running.load()) {
          ssize_t n = recv(udp_sock, tx_frame.payload, sizeof(tx_frame.payload), 0);
          if (n > 0) {
            tx_frame.hdr.seq[0] = seq & 0xFF;
            tx_frame.hdr.seq[1] = (seq >> 8) & 0xFF;
            tx_frame.hdr.seq[2] = (seq >> 16) & 0xFF;
            ++seq;
            radio->send_packet(reinterpret_cast<const uint8_t*>(&tx_frame), header_overhead + n);
          }
        }
        close(udp_sock);
      }
      radio.reset();
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Exception radio : %s\n", e.what());
  }

  libusb_release_interface(handle, 0);
  libusb_close(handle);
  libusb_exit(ctx);

  return(0);
}
