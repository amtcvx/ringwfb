/*

sudo ip addr add 10.5.0.1/24 dev tun0
sudo ip link set dev tun0 mtu 1350 up

sudo ip addr add 10.5.0.2/24 dev tun0
sudo ip link set dev tun0 mtu 1350 up

ssh root@10.5.0.2

*/
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <memory>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <libusb-1.0/libusb.h>
#include "WiFiDriver.h"
#include "IRadio.h"
#include "DeviceConfig.h"
#include "UsbOpen.h"

// Magic bytes pour filtrer uniquement nos paquets tunnel sur les ondes
static constexpr uint8_t MAGIC[4] = {0x53, 0x53, 0x48, 0x54}; // "SSHT"

int open_tun(const char* dev_name) {
    int fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) return -1;

    struct ifreq ifr{};
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI; // Paquets IP purs sans en-tête TUN
    std::strncpy(ifr.ifr_name, dev_name, IFNAMSIZ - 1);

    if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char** argv) {
    // 1. Créer l'interface virtuelle tun0 (nécessite root ou CAP_NET_ADMIN)
    int tun_fd = open_tun("tun0");
    if (tun_fd < 0) {
        std::cerr << "Erreur ouverture /dev/net/tun (lancer en sudo ?)\n";
        return 1;
    }

    // 2. Initialiser libusb et devourer
    libusb_context* ctx = nullptr;
    libusb_init(&ctx);
    libusb_device_handle* handle = libusb_open_device_with_vid_pid(ctx, 0x0bda, 0x8812);
    if (!handle || !devourer::claim_interface_then_reset(handle, 0)) {
        std::cerr << "Adaptateur Realtek introuvable\n";
        return 1;
    }

    devourer::DeviceConfig cfg;
    cfg.tuning.tx_with_rx_thread = true;
    auto logger = std::make_shared<Logger>();
    WiFiDriver driver(logger);
    auto radio = driver.CreateRadio(handle, ctx, nullptr, cfg);

    SelectedChannel ch{.Channel = 149, .ChannelOffset = 0, .ChannelWidth = CHANNEL_WIDTH_20};
    if (!radio || !radio->InitWrite(ch)) return 1;

    // En-tête fixe : Radiotap (8 octets) + 802.11 Data (24 octets) + Magic (4 octets) = 36 octets
    const std::vector<uint8_t> radio_hdr = {
        0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, // Radiotap minimal
        0x08, 0x00, 0x00, 0x00,                         // Frame Control (Data) + Duration
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,             // Addr1 (Broadcast)
        0x57, 0x42, 0x75, 0x05, 0xd6, 0x00,             // Addr2 (TA)
        0x57, 0x42, 0x75, 0x05, 0xd6, 0x00,             // Addr3 (BSSID)
        0x00, 0x00,                                     // Seq Ctrl
        MAGIC[0], MAGIC[1], MAGIC[2], MAGIC[3]          // Signature du tunnel
    };

    // 3. Thread RX : Radio -> Noyau Linux (tun0)
    // Note : selon le backend, pkt.Data contient uniquement la trame 802.11 (sans Radiotap)
    std::thread rx_thread([&]() {
        radio->StartRxLoop([tun_fd](const Packet& pkt) {
            // 24 octets d'en-tête 802.11 + 4 octets MAGIC = 28 octets
            if (pkt.Data.size() <= 28) return;
            if (std::memcmp(pkt.Data.data() + 24, MAGIC, 4) != 0) return;

            const uint8_t* ip_pkt = pkt.Data.data() + 28;
            size_t ip_len = pkt.Data.size() - 28;
            ssize_t written = write(tun_fd, ip_pkt, ip_len);
            (void)written;
        });
    });

    // 4. Boucle TX : Noyau Linux (tun0) -> Radio
    std::vector<uint8_t> tx_buf(2048);
    std::memcpy(tx_buf.data(), radio_hdr.data(), radio_hdr.size());

    while (true) {
        ssize_t n = read(tun_fd, tx_buf.data() + radio_hdr.size(), 1500);
        if (n <= 0) break;
        radio->send_packet(tx_buf.data(), radio_hdr.size() + static_cast<size_t>(n));
    }

    radio->Stop();
    if (rx_thread.joinable()) rx_thread.join();
    radio.reset();
    close(tun_fd);
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    return 0;
}

