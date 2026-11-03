#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 2048

// Entête Radiotap minimaliste (8 octets) pour indiquer une transmission basique
unsigned char radiotap_hdr[] = {
    0x00, 0x00, // Version & Flags
    0x08, 0x00, // Longueur de l'entête (8)
    0x00, 0x00, 0x00, 0x00 // Présence de champs (aucun)
};

// Entête IEEE 802.11 Data Frame minimale (24 octets)
unsigned char dot11_hdr[] = {
    0x08, 0x00, // Frame Control: Data frame
    0x00, 0x00, // Duration
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // Adresse 1: Dest (Broadcast pour simplifier)
    0x00, 0x11, 0x22, 0x33, 0x44, 0xff, // Adresse 2: Source (Virtuelle)
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // Adresse 3: BSSID (Broadcast)
    0x00, 0x00  // Sequence Control
};

int open_tap(char *dev) {
    struct ifreq ifr;
    int fd, err;
    if ((fd = open("/dev/net/tun", O_RDWR)) < 0) return fd;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI; // Mode TAP, pas d'entête packet info extra
    strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0) { close(fd); return err; }
    return fd;
}

int open_raw_socket(char *dev) {
    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock < 0) return sock;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) { close(sock); return -1; }
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = ifr.ifr_ifindex;
    sll.sll_protocol = htons(ETH_P_ALL);
    if (bind(sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) { close(sock); return -1; }
    return sock;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <interface_moniteur> <interface_tap>\n", argv[0]);
        return 1;
    }
    char *mon_iface = argv[1];
    char *tap_iface = argv[2];

    int tap_fd = open_tap(tap_iface);
    int raw_sock = open_raw_socket(mon_iface);
    if (tap_fd < 0 || raw_sock < 0) {
        perror("Erreur d'initialisation des interfaces (Êtes-vous root ?)");
        return 1;
    }

    printf("Tunnel bidirectionnel natif actif : %s <-> %s\n", mon_iface, tap_iface);
    
    unsigned char buffer[BUFFER_SIZE];
    fd_set fds;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(tap_fd, &fds);
        FD_SET(raw_sock, &fds);
        int max_fd = (tap_fd > raw_sock) ? tap_fd : raw_sock;

        if (select(max_fd + 1, &fds, NULL, NULL, NULL) < 0) break;

        // 1. ÉMISSION : Données reçues du Système (TAP) -> Injectées dans l'air (Monitor)
        if (FD_ISSET(tap_fd, &fds)) {
            int payload_len = read(tap_fd, buffer + sizeof(radiotap_hdr) + sizeof(dot11_hdr), BUFFER_SIZE - 40);
            if (payload_len > 0) {
                memcpy(buffer, radiotap_hdr, sizeof(radiotap_hdr));
                memcpy(buffer + sizeof(radiotap_hdr), dot11_hdr, sizeof(dot11_hdr));
                int total_len = sizeof(radiotap_hdr) + sizeof(dot11_hdr) + payload_len;
                send(raw_sock, buffer, total_len, 0);
            }
        }

        // 2. RÉCEPTION : Trames WiFi reçues (Monitor) -> Envoyées au Système (TAP)
        if (FD_ISSET(raw_sock, &fds)) {
            int rx_len = recv(raw_sock, buffer, BUFFER_SIZE, 0);
            // On vérifie que la trame capturée est bien une trame Data 802.11 (0x08)
            // L'offset dépend de la taille de l'entête Radiotap entrante de la carte rtw88 (généralement variable, ici on cherche le pattern grossier)
            if (rx_len > 32) {
                // Analyse ultra-simplifiée de l'entête Radiotap pour trouver le début du paquet 802.11
                int rtap_len = buffer[2]; // La longueur est stockée au 3e octet de l'entête Radiotap standard
                if (rtap_len > 0 && rtap_len < rx_len) {
                    unsigned char *dot11_ptr = buffer + rtap_len;
                    if ((dot11_ptr[0] & 0x0C) == 0x08) { // S'il s'agit d'une trame de type "Data"
                        int header_80211_len = 24; // Taille standard sans QoS control
                        int payload_offset = rtap_len + header_80211_len;
                        int eth_payload_len = rx_len - payload_offset;
                        if (eth_payload_len > 0) {
                            write(tap_fd, buffer + payload_offset, eth_payload_len);
                        }
                    }
                }
            }
        }
    }
    return 0;
}

