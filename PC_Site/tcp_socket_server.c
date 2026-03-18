#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SERVER_PORT 8081
#define ACK_BYTE 0xACU
#define BINARY_FILE_PATH "/home/alex/Documents/bitcraze/WiFiChipOTAProgramming/build/WiFiChipOTAProgramming/zephyr/zephyr.signed.bin"
#define IRIS_PACKET_PAYLOAD_SIZE 1024

typedef struct __attribute__((packed)) {
    uint32_t packet_idx;
    uint32_t packet_nmbr;
    uint8_t payload[IRIS_PACKET_PAYLOAD_SIZE];
} iris_packet_t;

static int recv_ack(int sock_fd)
{
    uint8_t ack = 0;
    ssize_t ret = recv(sock_fd, &ack, sizeof(ack), MSG_WAITALL);

    if (ret <= 0) {
        return -1;
    }
    if (ack != ACK_BYTE) {
        fprintf(stderr, "[Server] Unexpected ACK byte: 0x%02X\n", ack);
        return -1;
    }
    return 0;
}

static int send_all(int sock_fd, const void *buffer, size_t size)
{
    size_t sent = 0U;

    while (sent < size) {
        ssize_t ret = send(sock_fd, (const uint8_t *)buffer + sent, size - sent, 0);
        if (ret <= 0) {
            if (ret < 0 && errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (size_t)ret;
    }

    return 0;
}

static void print_listen_addresses(void)
{
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *ifa = NULL;
    char ip_str[INET_ADDRSTRLEN];

    if (getifaddrs(&ifaddr) != 0) {
        printf("[Server] Listening on port %d...\n", SERVER_PORT);
        return;
    }

    printf("[Server] Listening on port %d - reachable at:\n", SERVER_PORT);
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if (ifa->ifa_name != NULL && strncmp(ifa->ifa_name, "lo", 2) == 0) {
            continue;
        }

        inet_ntop(AF_INET,
                  &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr,
                  ip_str,
                  sizeof(ip_str));
        printf("[Server]   %s (iface: %s)\n", ip_str, ifa->ifa_name);
    }

    freeifaddrs(ifaddr);
}

int main(void)
{
    int server_fd = -1;
    int client_fd = -1;
    FILE *binary_file = NULL;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    struct stat file_stat;
    uint32_t total_packets;

    if (stat(BINARY_FILE_PATH, &file_stat) != 0) {
        perror("[Server] stat");
        fprintf(stderr, "[Server] Failed to open binary file path: %s\n", BINARY_FILE_PATH);
        return EXIT_FAILURE;
    }

    if (file_stat.st_size <= 0) {
        fprintf(stderr, "[Server] Binary file is empty: %s\n", BINARY_FILE_PATH);
        return EXIT_FAILURE;
    }

    total_packets = (uint32_t)((file_stat.st_size + IRIS_PACKET_PAYLOAD_SIZE - 1) / IRIS_PACKET_PAYLOAD_SIZE);
    printf("[Server] Binary file size: %lld bytes, total packets to send: %u\n", (long long)file_stat.st_size, total_packets);

    binary_file = fopen(BINARY_FILE_PATH, "rb");
    if (binary_file == NULL) {
        perror("[Server] fopen");
        return EXIT_FAILURE;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[Server] socket");
        fclose(binary_file);
        return EXIT_FAILURE;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("[Server] setsockopt");
        close(server_fd);
        fclose(binary_file);
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    print_listen_addresses();

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[Server] bind");
        close(server_fd);
        fclose(binary_file);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 1) < 0) {
        perror("[Server] listen");
        close(server_fd);
        fclose(binary_file);
        return EXIT_FAILURE;
    }

    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_fd < 0) {
        perror("[Server] accept");
        close(server_fd);
        fclose(binary_file);
        return EXIT_FAILURE;
    }

    // int nodelay = 1;
    // if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
    //     perror("[Server] setsockopt TCP_NODELAY");
    // }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    printf("[Server] Client connected from %s\n", client_ip);

    printf("[Server] Waiting for client ready ACK...\n");
    if (recv_ack(client_fd) != 0) {
        fprintf(stderr, "[Server] Did not receive initial ready ACK from client\n");
        close(client_fd);
        close(server_fd);
        fclose(binary_file);
        return EXIT_FAILURE;
    }
    printf("[Server] Client ready. Streaming '%s' in %u packets\n", BINARY_FILE_PATH, total_packets);

    for (uint32_t packet_idx = 0; packet_idx < total_packets; ++packet_idx) {
        iris_packet_t packet;
        size_t bytes_read;

        memset(&packet, 0, sizeof(packet));
        packet.packet_idx = packet_idx;
        packet.packet_nmbr = total_packets;

        bytes_read = fread(packet.payload, 1, IRIS_PACKET_PAYLOAD_SIZE, binary_file);
        if (bytes_read == 0 && ferror(binary_file)) {
            perror("[Server] fread");
            close(client_fd);
            close(server_fd);
            fclose(binary_file);
            return EXIT_FAILURE;
        }

        if (send_all(client_fd, &packet, sizeof(packet)) != 0) {
            perror("[Server] send");
            close(client_fd);
            close(server_fd);
            fclose(binary_file);
            return EXIT_FAILURE;
        }

        if (recv_ack(client_fd) != 0) {
            fprintf(stderr, "[Server] ACK not received for packet %u\n", packet_idx);
            close(client_fd);
            close(server_fd);
            fclose(binary_file);
            return EXIT_FAILURE;
        }
        printf("[Server] Sent packet %u/%u, size: %zu bytes\n", packet_idx + 1, total_packets, bytes_read);
    }

    printf("[Server] Stream complete. Closing connection.\n");
    close(client_fd);
    close(server_fd);
    fclose(binary_file);
    return EXIT_SUCCESS;
}
