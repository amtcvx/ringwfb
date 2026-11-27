/*
g++ -std=c++20 -O2 main_h265.cpp \
    -I../devourer/src \
    -I../devourer/hal \
    ../devourer/build/libdevourer.a \
    $(pkg-config --cflags --libs libusb-1.0) \
    -lpthread \
    -o main_h265

sudo ./main_h265 5600 5601 127.0.0.1

gst-launch-1.0 -v videotestsrc is-live=true pattern=ball ! \
  video/x-raw,width=1280,height=720,framerate=30/1 ! \
  x265enc tune=zerolatency bitrate=2500 speed-preset=ultrafast key-int-max=15 option-string="bframes=0:intra-refresh=1:vbv-maxrate=2500:vbv-bufsize=300" ! \
  rtph265pay config-interval=-1 mtu=1400 ! \
  udpsink host=127.0.0.1 port=5600 sync=false

sudo ./main_h265 5600 5601 127.0.0.1

gst-launch-1.0 -v udpsrc port=5601 caps="application/x-rtp,media=video,encoding-name=H265,clock-rate=90000,payload=96" ! \
  rtpjitterbuffer latency=15 drop-on-latency=true ! \
  rtph265depay ! h265parse config-interval=-1 ! avdec_h265 ! videoconvert ! autovideosink sync=false


RTP H.265, Time-Slicing inter-frames, scan bi-bande 2.4/5 GHz, synchro sans maître

*/
#include "DeviceConfig.h"
#include "IRadio.h"
#include "RxPacket.h"
#include "UsbDeviceLock.h"
#include "WiFiDriver.h"
#include <libusb-1.0/libusb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using namespace std::chrono;

enum Msg : uint8_t { DATA = 1, HB = 2, PROP = 3, ACC = 4 };

// Radiotap TX 13 octets : HT MCS 3 (39 Mbps en 20 MHz) 
constexpr std::array<uint8_t, 13> RTAP_TX = {
    0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x08, 0x00,
    0x07, 0x00, 0x03, 0x00, 0x00
};

// En-tête IEEE 802.11 Data (24 octets)
constexpr std::array<uint8_t, 24> IEEE80211_HDR = {
    0x08, 0x01, 0x00, 0x00,
    0x57, 0x42, 0x00, 0x00, 0x00, 0x01,
    0x57, 0x42, 0x00, 0x00, 0x00, 0x02,
    0x57, 0x42, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00
};

constexpr std::array<uint8_t, 4> MAGIC_DV = {'D', 'V', '6', '5'};

struct __attribute__((packed)) Hdr {
    std::array<uint8_t, 13> rtap;
    std::array<uint8_t, 24> dot11;
    std::array<uint8_t, 4> magic;
    uint8_t type, ch;
    uint32_t id, delay_us;
    uint16_t seq;
};

// Canaux 5 GHz rapides (< 1 ms avec FastRetune intra-bande) + 2.4 GHz en dernier recours
const std::vector<uint8_t> CHANS_5G = {36, 40, 44, 48, 149, 153, 157, 161, 165};
const std::vector<uint8_t> CHANS_24G = {1, 6, 11, 13};
constexpr uint8_t RDV_5G = 149, RDV_24 = 6;

inline bool IsEndOfH265PFrame(const uint8_t* p, size_t n) {
    if (n < 13 || (*p >> 6) != 2 || !(*(p + 1) & 0x80)) return false;
    uint8_t nal = (*(p + 12) >> 1) & 0x3F;
    return nal < 16 || nal > 21;
}

class Node {
    IRadio* r_;
    std::mutex tx_mtx_;
    uint32_t id_{std::random_device{}()};
    int s_in_{-1}, s_out_{-1};
    sockaddr_in dst_{};

    std::atomic<bool> run_{true}, scan_{false}, sw_pend_{false}, send_ack_{false};
    std::atomic<uint8_t> state_{0}, p_ch_{0};
    std::atomic<uint16_t> ack_seq_{0};
    std::atomic<uint32_t> ack_delay_{0}, noise_{0}, s_noise_{0};
    std::atomic<int64_t> deadline_ns_{0}, last_rx_ns_{0};

    uint8_t ch_{RDV_5G};
    uint16_t seq_{0}, a_seq_{0};
    uint32_t frame_cnt_{0};
    size_t s_idx_{0};
    std::array<uint32_t, 13> scores_{};
    steady_clock::time_point last_sw_{steady_clock::now()};

    static int64_t NowNs() {
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }

    void Tune(uint8_t c) { r_->FastRetune(c, true); ch_ = c; }

    void TxCtrl(uint8_t t, uint8_t c, uint16_t s, uint32_t d = 0) {
        if (scan_) return;
        Hdr h{RTAP_TX, IEEE80211_HDR, MAGIC_DV, t, c, id_, d, s};
        std::lock_guard lk(tx_mtx_);
        r_->send_packet(reinterpret_cast<uint8_t*>(&h), sizeof(h));
    }

    void MicroScan() {
      // Scanner uniquement si le canal actif montre du bruit (> 8 trames parasites)
      // et au maximum 1 fois toutes les 6 frames
      if (noise_.load() < 8 || (++frame_cnt_ % 6 != 0)) return;
  
      size_t i = s_idx_;
      uint8_t c = CHANS_5G.at(i);
      s_idx_ = (s_idx_ + 1) % CHANS_5G.size();
      if (c == ch_ || ch_ <= 14) return; // FastRetune rapide uniquement intra-bande 5 GHz
  
      scan_ = true; s_noise_ = 0;
      { std::lock_guard lk(tx_mtx_); r_->FastRetune(c, true); }
      std::this_thread::sleep_for(milliseconds(2));
      { std::lock_guard lk(tx_mtx_); r_->FastRetune(ch_, true); }
      scores_.at(i) = (scores_.at(i) * 3 + s_noise_) / 4;
      scan_ = false;
    }


    void OnRx(const Packet& rx) {
        if (rx.RxAtrib.crc_err) return;
        size_t fcs_len = rx.RxAtrib.fcs_present ? 4 : 0;
        if (rx.Data.size() <= fcs_len) return;

        const uint8_t* raw = rx.Data.data();
        size_t len = rx.Data.size() - fcs_len;

        const uint8_t* ptr = nullptr;
        size_t rem = 0;
        constexpr size_t OFFSET_MAGIC = offsetof(Hdr, magic);
        for (size_t off = 24; off + (sizeof(Hdr) - OFFSET_MAGIC) <= len && off <= 32; ++off) {
            if (std::memcmp(raw + off, "DV65", 4) == 0) {
                ptr = raw + off - OFFSET_MAGIC;
                rem = len - off + OFFSET_MAGIC;
                break;
            }
        }
        if (!ptr) { (scan_ ? s_noise_ : noise_)++; return; }

        auto* h = reinterpret_cast<const Hdr*>(ptr);
        if (h->id == id_) return;
        last_rx_ns_ = NowNs();
        if (state_ == 2) state_ = 0;

        if (h->type == DATA && rem > sizeof(Hdr)) {
            sendto(s_out_, ptr + sizeof(Hdr), rem - sizeof(Hdr), 0, (sockaddr*)&dst_, sizeof(dst_));
        } else if (h->type == PROP && (state_ != 1 || h->id > id_)) {
            state_ = 1;
            p_ch_ = h->ch;
            ack_seq_ = h->seq;
            ack_delay_ = h->delay_us;
            deadline_ns_ = NowNs() + static_cast<int64_t>(h->delay_us) * 1000;
            sw_pend_ = true;
            send_ack_ = true;
        } else if (h->type == ACC && state_ == 1 && h->seq == a_seq_) {
            sw_pend_ = true;
        }
    }

public:
    Node(IRadio* r, uint16_t pin, const char* ip, uint16_t pout) : r_(r) {
        last_rx_ns_ = NowNs();
        s_in_ = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in a{AF_INET, htons(pin), {INADDR_ANY}};
        bind(s_in_, (sockaddr*)&a, sizeof(a));
        s_out_ = socket(AF_INET, SOCK_DGRAM, 0);
        dst_.sin_family = AF_INET; dst_.sin_port = htons(pout);
        inet_pton(AF_INET, ip, &dst_.sin_addr);
    }

    ~Node() { run_ = false; r_->StopRxLoop(); close(s_in_); close(s_out_); }

    void Run() {
        SelectedChannel sc{};
        sc.Channel = RDV_5G;
        sc.ChannelOffset = 0;
        sc.ChannelWidth = CHANNEL_WIDTH_20;
        r_->InitWrite(sc);
        r_->SetTxPower(35);

        std::thread t_rx([this] { r_->StartRxLoop([this](const Packet& f) { OnRx(f); }); });
        std::thread t_tx([this] {
            std::array<uint8_t, 2048> buf{};
            while (run_) {
                ssize_t n = recvfrom(s_in_, buf.data(), buf.size(), 0, nullptr, nullptr);
                if (n <= 0 || n > 1420 || scan_) continue;
                std::vector<uint8_t> pkt(sizeof(Hdr) + n);
                *reinterpret_cast<Hdr*>(pkt.data()) = Hdr{RTAP_TX, IEEE80211_HDR, MAGIC_DV, DATA, ch_, id_, 0, ++seq_};
                std::memcpy(pkt.data() + sizeof(Hdr), buf.data(), n);
                { std::lock_guard lk(tx_mtx_); r_->send_packet(pkt.data(), pkt.size()); }
                if (state_ == 0 && IsEndOfH265PFrame(buf.data(), n)) MicroScan();
            }
        });

        bool alt = false;
        while (run_) {
            int64_t now_ns = NowNs();
            auto now_tp = steady_clock::now();

            if (send_ack_.exchange(false)) {
                for (int k = 0; k < 4; ++k) TxCtrl(ACC, p_ch_, ack_seq_, ack_delay_);
            }

            if (sw_pend_ && now_ns >= deadline_ns_) {
                { std::lock_guard lk(tx_mtx_); Tune(p_ch_); }
                sw_pend_ = false; state_ = 0; last_rx_ns_ = NowNs(); last_sw_ = now_tp;
                std::cout << "[CH] " << (int)ch_ << "\n";
            }
            // Watchdog élargi à 1.5 s pour éviter tout décrochage intempestif
            if ((now_ns - last_rx_ns_) > 1'500'000'000LL) {
                uint8_t rdv = (alt = !alt) ? RDV_24 : RDV_5G;
                if (ch_ != rdv) { std::lock_guard lk(tx_mtx_); Tune(rdv); }
                state_ = 2;
            }

	    // Si le canal actif dépasse le seuil d'encombrement (noise_ > 40)
	    if (state_ == 0 && duration_cast<seconds>(now_tp - last_sw_).count() >= 2 && noise_ > 40) {
              uint8_t best = ch_; uint32_t min_s = UINT32_MAX;
              for (size_t i = 0; i < CHANS_5G.size(); ++i) {
                if (scores_.at(i) < min_s) { min_s = scores_.at(i); best = CHANS_5G.at(i); }
              }
              // Si même le meilleur canal 5 GHz est très bruité (> 25), basculer sur 2.4 GHz (canal 6)
              if (min_s > 25) best = RDV_24;

              if (best != ch_) {
                state_ = 1; a_seq_ = ++seq_; p_ch_ = best; deadline_ns_ = now_ns + 35'000'000LL;
                for (int k = 0; k < 5; ++k) { TxCtrl(PROP, best, a_seq_, 35000); std::this_thread::sleep_for(milliseconds(1)); }
              }
              noise_ = 0;
            }


            TxCtrl(HB, ch_, ++seq_);
            std::this_thread::sleep_for(milliseconds(10));
        }
        t_tx.join(); t_rx.join();
    }
};

int main(int argc, char** argv) {
    uint16_t pin = argc > 1 ? std::stoi(*(argv + 1)) : 5600;
    uint16_t pout = argc > 2 ? std::stoi(*(argv + 2)) : 5601;
    const char* ip = argc > 3 ? *(argv + 3) : "127.0.0.1";

    libusb_context* ctx = nullptr;
    libusb_init(&ctx);
    auto* dev = libusb_open_device_with_vid_pid(ctx, 0x0b05, 0x17d2);
    if (!dev) return 1;
    libusb_claim_interface(dev, 0);

    auto logger = std::make_shared<Logger>();
    devourer::DeviceConfig cfg;
    cfg.rx.enable_with_tx = true;

    auto usb_lock = std::make_shared<devourer::UsbDeviceLock>();
    WiFiDriver drv(logger);
    auto radio = drv.CreateRadio(dev, ctx, usb_lock, cfg);

    if (!radio) return 1;

    Node(radio.get(), pin, ip, pout).Run();
    libusb_release_interface(dev, 0);
    libusb_close(dev);
    libusb_exit(ctx);
    return 0;
}

