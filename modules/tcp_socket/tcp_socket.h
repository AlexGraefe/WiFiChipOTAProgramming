#ifndef TCP_SOCKET_H
#define TCP_SOCKET_H

#define SERVER_IP        "10.161.131.33" //"192.168.5.29"
#define SERVER_PORT      8081
#define BUFFER_SIZE      256

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/net/socket.h>

#define TCP_DATA_MAX_SIZE 60000

#define SOCKET_THREAD_PRIORITY 100

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)

#define IRIS_PACKET_PAYLOAD_SIZE 1024

typedef struct __attribute__((packed)) {
    uint32_t packet_idx;
    uint32_t packet_nmbr;
    uint8_t payload[IRIS_PACKET_PAYLOAD_SIZE];
} iris_packet_t;

typedef enum {
	COMM_FLASH_CHECK,
	COMM_WIFI_CONNECTING,
	COMM_WAITING_FOR_IP,
	COMM_ESTABLISHING_SERVER,
	COMM_RECEIVING_MESSAGES,
	COMM_FAILURE,
	COMM_CLEANUP,
	COMM_DONE,
} communication_state_t;

typedef struct {
	struct sockaddr_in server_addr;
	char               buffer[BUFFER_SIZE];
	char               ip_addr[NET_IPV4_ADDR_LEN];
	int  sock_fd;
	bool wifi_connected;
	bool socket_open;
	bool flash_ready;
	int  exit_code;
	uint64_t flash_size;
	communication_state_t failure_from_state;
} communication_context_t;

int run_tcp_socket_demo(void);

#endif /* TCP_SOCKET_H */
