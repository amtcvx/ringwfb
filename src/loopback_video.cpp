/*
sudo apt install gstreamer1.0-plugins-bad gstreamer1.0-libav gstreamer1.0-plugins-good
sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev

g++ -std=c++20 loopback_video.cpp /home/pprz/Projects/devourer/build/libdevourer.a \
    -I/home/pprz/Projects/devourer/src \
    $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 libusb-1.0) \
    -lpthread -o loopback_video

sudo ./loopback_video 1 4 1 5

*/

#include "WiFiDriver.h"
#include "IRadio.h"
#include "DeviceConfig.h"
#include "UsbOpen.h"
#include "RxPacket.h"
//#include "Logger.h"
#include "logger.h"

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

#include <libusb-1.0/libusb.h>

#include <iostream>
#include <vector>
#include <memory>
#include <thread>
#include <cstring>
#include <cstdlib>

//using namespace devourer;

// --- EN-TÊTES RADIO (13 octets Radiotap + 24 octets IEEE 802.11 = 37 octets) ---
static const uint8_t RADIOTAP_HEADER[] = {
    0x00, 0x00, 0x0d, 0x00, 0x00, 0x80, 0x08, 0x00,
    0x08, 0x00, 0x07, 0x00, 0x03 // MCS 3 (plus rapide que MCS 0 pour la vidéo)
};

static const uint8_t IEEE80211_HEADER[] = {
    0x08, 0x01, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // Destination (Broadcast)
    0x13, 0x22, 0x33, 0x44, 0x55, 0x66, // Source MAC (utilisée pour le filtre RX)
    0x13, 0x22, 0x33, 0x44, 0x55, 0x66, // BSSID
    0x00, 0x00
};

static const uint8_t EXPECTED_SRC_MAC[6] = {0x13, 0x22, 0x33, 0x44, 0x55, 0x66};
static const size_t IEEE80211_LEN = 24;

// Structure regroupant un adaptateur et son contexte libusb dédié
struct RadioAdapter {
    libusb_context *usb_ctx = nullptr;
    libusb_device_handle *usb_handle = nullptr;
    Logger_t logger{};
    std::shared_ptr<devourer::UsbDeviceLock> usb_lock;
    std::unique_ptr<IRadio> radio;
};

bool init_adapter(RadioAdapter &ad, uint8_t target_bus, uint8_t target_addr) {
    if (libusb_init(&ad.usb_ctx) < 0) return false;

    libusb_device **list = nullptr;
    ssize_t cnt = libusb_get_device_list(ad.usb_ctx, &list);
    for (ssize_t i = 0; i < cnt; i++) {
        if (libusb_get_bus_number(list[i]) == target_bus &&
            libusb_get_device_address(list[i]) == target_addr) {
            libusb_device_descriptor desc;
if (libusb_get_device_descriptor(list[i], &desc) == 0 && (desc.idVendor == 0x0bda || desc.idVendor == 0x0b05)) {
                libusb_open(list[i], &ad.usb_handle);
                break;
            }
        }
    }
    libusb_free_device_list(list, 1);

    if (!ad.usb_handle) return false;

    ad.logger = std::make_shared<Logger>();

    std::string dev_id = std::to_string(target_bus) + ":" + std::to_string(target_addr);
    if (claim_interface_then_reset(ad.usb_handle, 0, ad.logger, true, ad.usb_lock, dev_id) != 0) {
        return false;
    }

    WiFiDriver driver(ad.logger);
    devourer::DeviceConfig cfg;
    ad.radio = driver.CreateRadio(ad.usb_handle, ad.usb_ctx, nullptr, cfg);

    return (ad.radio != nullptr);
}

void close_adapter(RadioAdapter &ad) {
    if (ad.radio) {
        ad.radio->Stop();
        ad.radio.reset();
    }
    if (ad.usb_handle) {
        libusb_release_interface(ad.usb_handle, 0);
        libusb_close(ad.usb_handle);
    }
    if (ad.usb_ctx) {
        libusb_exit(ad.usb_ctx);
    }
}

// --- CALLBACK TX (GStreamer appsink -> Carte Wi-Fi TX) ---
static GstFlowReturn on_tx_sample(GstElement *sink, gpointer user_data) {
    IRadio *tx_radio = static_cast<IRadio*>(user_data);
    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) return GST_FLOW_ERROR;

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;

    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        // Prépare la trame complète : [Radiotap] + [802.11] + [Paquet RTP]
        std::vector<uint8_t> frame;
        frame.reserve(sizeof(RADIOTAP_HEADER) + sizeof(IEEE80211_HEADER) + map.size);
        frame.insert(frame.end(), std::begin(RADIOTAP_HEADER), std::end(RADIOTAP_HEADER));
        frame.insert(frame.end(), std::begin(IEEE80211_HEADER), std::end(IEEE80211_HEADER));
        frame.insert(frame.end(), map.data, map.data + map.size);

        tx_radio->send_packet(frame.data(), frame.size());
        gst_buffer_unmap(buffer, &map);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

// --- CALLBACK DE LIBÉRATION ZERO-COPY ---
static void on_rx_buffer_free(gpointer data) {
    std::free(data);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <tx_bus> <tx_dev> <rx_bus> <rx_dev>\n";
        std::cerr << "Exemple: sudo " << argv[0] << " 1 4 1 5\n";
        return 1;
    }

    gst_init(&argc, &argv);

    uint8_t tx_bus = std::stoi(argv[1]), tx_dev = std::stoi(argv[2]);
    uint8_t rx_bus = std::stoi(argv[3]), rx_dev = std::stoi(argv[4]);

    RadioAdapter tx_adapter, rx_adapter;
    if (!init_adapter(tx_adapter, tx_bus, tx_dev)) {
        std::cerr << "Erreur d'ouverture de l'adaptateur TX\n";
        return 1;
    }
    if (!init_adapter(rx_adapter, rx_bus, rx_dev)) {
        std::cerr << "Erreur d'ouverture de l'adaptateur RX\n";
        close_adapter(tx_adapter);
        return 1;
    }

    SelectedChannel chan;
    chan.Channel = 149;
    chan.ChannelOffset = 0;
    chan.ChannelWidth = CHANNEL_WIDTH_20;

    // 1. Démarrage de la radio TX
    tx_adapter.radio->InitWrite(chan);

    // 2. Création du pipeline GStreamer RX (appsrc -> décodage -> fenêtre vidéo)
    GstElement *rx_pipeline = gst_parse_launch(
        "appsrc name=rx_src is-live=true format=time "
        "caps=\"application/x-rtp,media=video,clock-rate=90000,encoding-name=H265,payload=96\" ! "
        "rtph265depay ! avdec_h265 ! videoconvert ! autovideosink sync=false", nullptr);
    GstElement *appsrc = gst_bin_get_by_name(GST_BIN(rx_pipeline), "rx_src");
    gst_element_set_state(rx_pipeline, GST_STATE_PLAYING);

    // 3. Démarrage du thread de réception RX (Filtrage MAC + Zero-Copy vers appsrc)
    std::thread rx_thread([&]() {
        rx_adapter.radio->Init([appsrc](const Packet &pkt) {
            // Vérification taille minimale et type Data (0x08)
            if (pkt.Data.size() <= IEEE80211_LEN || pkt.Data[0] != 0x08) return;

            // Filtrage sur notre adresse MAC source (offset 10)
            if (std::memcmp(&pkt.Data[10], EXPECTED_SRC_MAC, 6) != 0) return;

            // Copie unique vers un bloc C géré par GStreamer (Zero-Copy dans le pipeline)
            size_t total_len = pkt.Data.size();
            uint8_t *raw_mem = static_cast<uint8_t*>(std::malloc(total_len));
            if (!raw_mem) return;
            std::memcpy(raw_mem, pkt.Data.data(), total_len);

            // Offset = 24 pour sauter l'en-tête 802.11 et lire directement le RTP
            GstBuffer *buf = gst_buffer_new_wrapped_full(
                GST_MEMORY_FLAG_READONLY, raw_mem, total_len,
                IEEE80211_LEN, total_len - IEEE80211_LEN,
                raw_mem, on_rx_buffer_free
            );

            gst_app_src_push_buffer(GST_APP_SRC(appsrc), buf);
        }, chan);
    });

    // 4. Création du pipeline GStreamer TX (mire vidéo -> H.264 -> RTP -> appsink)
    // Note : mtu=1400 est crucial pour ne pas dépasser la taille max d'une trame Wi-Fi
    GstElement *tx_pipeline = gst_parse_launch(
        "videotestsrc is-live=true pattern=ball ! "
        "video/x-raw,width=640,height=480,framerate=30/1 ! "
        "x265enc tune=zerolatency bitrate=1500 speed-preset=ultrafast key-int-max=30 ! "
        "rtph265pay config-interval=1 mtu=1400 pt=96 ! "
        "appsink name=tx_sink emit-signals=true sync=false max-buffers=1 drop=true", nullptr);
    GstElement *appsink = gst_bin_get_by_name(GST_BIN(tx_pipeline), "tx_sink");
    g_signal_connect(appsink, "new-sample", G_CALLBACK(on_tx_sample), tx_adapter.radio.get());
    gst_element_set_state(tx_pipeline, GST_STATE_PLAYING);

    std::cout << "Boucle vidéo active sur le canal 149 ! Fermez la fenêtre vidéo ou faites Ctrl+C pour arrêter.\n";

    // Boucle principale GStreamer
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);

    // 5. Nettoyage propre
    gst_element_set_state(tx_pipeline, GST_STATE_NULL);
    gst_element_set_state(rx_pipeline, GST_STATE_NULL);
    gst_object_unref(tx_pipeline);
    gst_object_unref(rx_pipeline);

    rx_adapter.radio->Stop();
    if (rx_thread.joinable()) rx_thread.join();

    close_adapter(tx_adapter);
    close_adapter(rx_adapter);

    return 0;
}
