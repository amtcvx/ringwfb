/*
gcc -O2 tun_bridge.c -o tun_bridge

*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/if_tun.h>

#define BUF_SIZE 2048

int tun_alloc(char *dev) {
    struct ifreq ifr;
    int fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) { perror("open /dev/net/tun"); exit(1); }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI; // Paquets IP purs
    strncpy(ifr.ifr_name, dev, IFNAMSIZ);

    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
        perror("ioctl TUNSETIFF"); close(fd); exit(1);
    }
    return fd;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <tun_name> <local_udp_rx_port> <remote_udp_tx_port>\n", argv[0]);
        return 1;
    }

    int tun_fd = tun_alloc(argv[1]);
    int rx_port = atoi(argv[2]);
    int tx_port = atoi(argv[3]);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in local_addr = { .sin_family = AF_INET, .sin_port = htons(rx_port), .sin_addr.s_addr = INADDR_ANY };
    struct sockaddr_in radio_addr = { .sin_family = AF_INET, .sin_port = htons(tx_port) };
    inet_pton(AF_INET, "127.0.0.1", &radio_addr.sin_addr);

    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind"); return 1;
    }

    uint8_t buf[BUF_SIZE];
    fd_set fds;
    int max_fd = (tun_fd > sock ? tun_fd : sock) + 1;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(tun_fd, &fds);
        FD_SET(sock, &fds);

        if (select(max_fd, &fds, NULL, NULL, NULL) < 0) break;

        // 1. Paquet IP (SSH / Télémétrie) -> Lien Radio Devourer
        if (FD_ISSET(tun_fd, &fds)) {
            ssize_t len = read(tun_fd, buf, sizeof(buf));
            if (len > 0) sendto(sock, buf, len, 0, (struct sockaddr *)&radio_addr, sizeof(radio_addr));
        }

        // 2. Lien Radio Devourer -> Interface IP (mesh0)
        if (FD_ISSET(sock, &fds)) {
            ssize_t len = recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);
            if (len > 0) write(tun_fd, buf, len);
        }
    }
    return 0;
}

