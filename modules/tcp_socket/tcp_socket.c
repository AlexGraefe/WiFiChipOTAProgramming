#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>
#include <zephyr/app_version.h>

#include "wifi_utilities.h"
#include "secret/wifi_pswd.h"
#include "zephyr/net/net_ip.h"
#include "tcp_socket.h"

#include <zephyr/logging/log.h>

static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(LED2_NODE, gpios);

#define FLASH_NODE DT_ALIAS(flash0)

#ifndef TCP_SOCKET_FLASH_BASE_ADDRESS
#define TCP_SOCKET_FLASH_BASE_ADDRESS (0x320000) 
#endif

#ifndef TCP_SOCKET_FLASH_BASE_ADDRESS_0
#define TCP_SOCKET_FLASH_BASE_ADDRESS_0 (0x020000) 
#endif

#ifndef TCP_SOCKET_FLASH_BASE_ADDRESS_1
#define TCP_SOCKET_FLASH_BASE_ADDRESS_1 (0x320000) 
#endif

 // 0x320000

#ifndef TOTAL_EXPECTED_WRITE_SIZE
#define TOTAL_EXPECTED_WRITE_SIZE (900000U)
#endif

#define LED_TURN_OFF() do { gpio_pin_set_dt(&led_red, 0); gpio_pin_set_dt(&led_green, 0); gpio_pin_set_dt(&led_blue, 0); } while(0)
#define LED_TURN_RED() do { gpio_pin_set_dt(&led_red, 1); gpio_pin_set_dt(&led_green, 0); gpio_pin_set_dt(&led_blue, 0); } while(0)
#define LED_TURN_GREEN() do { gpio_pin_set_dt(&led_red, 0); gpio_pin_set_dt(&led_green, 1); gpio_pin_set_dt(&led_blue, 0); } while(0)
#define LED_TURN_BLUE() do { gpio_pin_set_dt(&led_red, 0); gpio_pin_set_dt(&led_green, 0); gpio_pin_set_dt(&led_blue, 1); } while(0)
#define LED_TURN_YELLOW() do { gpio_pin_set_dt(&led_red, 1); gpio_pin_set_dt(&led_green, 1); gpio_pin_set_dt(&led_blue, 0); } while(0)
#define LED_TURN_CYAN() do { gpio_pin_set_dt(&led_red, 0); gpio_pin_set_dt(&led_green, 1); gpio_pin_set_dt(&led_blue, 1); } while(0)
#define LED_TURN_MAGENTA() do { gpio_pin_set_dt(&led_red, 1); gpio_pin_set_dt(&led_green, 0); gpio_pin_set_dt(&led_blue, 1); } while(0)
#define LED_TURN_WHITE() do { gpio_pin_set_dt(&led_red, 1); gpio_pin_set_dt(&led_green, 1); gpio_pin_set_dt(&led_blue, 1); } while(0)


LOG_MODULE_REGISTER(tcp_socket_demo, LOG_LEVEL_DBG);

K_THREAD_DEFINE(tcp_thread, CONFIG_TCP_SOCKET_THREAD_STACK_SIZE,
				run_tcp_socket_demo, NULL, NULL, NULL,
                SOCKET_THREAD_PRIORITY, 0, 0);

#define ACK_BYTE 0xACU

static int send_ack(int sock_fd)
{
	uint8_t ack = ACK_BYTE;
	int ret = zsock_send(sock_fd, &ack, sizeof(ack), 0);

	if (ret < 0) {
		return -errno;
	}
	return 0;
}

static int recv_exact(int sock_fd, uint8_t *buffer, size_t size)
{
	size_t received = 0U;

	while (received < size) {
		int ret = zsock_recv(sock_fd, &buffer[received], size - received, 0);

		if (ret == 0) {
			return -ECONNRESET;
		}

		if (ret < 0) {
			return -errno;
		}

		received += (size_t)ret;
		// LOG_DBG("Received %d bytes, total received: %zu/%zu", ret, received, size);
	}

	return 0;
}

static const char *state_to_string(communication_state_t state)
{
	switch (state) {
	case COMM_FLASH_CHECK:
		return "COMM_FLASH_CHECK";
	case COMM_WIFI_CONNECTING:
		return "COMM_WIFI_CONNECTING";
	case COMM_WAITING_FOR_IP:
		return "COMM_WAITING_FOR_IP";
	case COMM_ESTABLISHING_SERVER:
		return "COMM_CONNECTING_TO_SERVER";
	case COMM_RECEIVING_MESSAGES:
		return "COMM_RECEIVING_MESSAGES";
	case COMM_FAILURE:
		return "COMM_FAILURE";
	case COMM_CLEANUP:
		return "COMM_CLEANUP";
	case COMM_DONE:
		return "COMM_DONE";
	default:
		return "COMM_UNKNOWN";
	}
}

static communication_state_t state_flash_check(communication_context_t *ctx)
{
	const struct device *flash_dev = DEVICE_DT_GET(FLASH_NODE);

	LED_TURN_WHITE();

	if (!device_is_ready(flash_dev)) {
		LOG_ERR("Flash device not ready");
		ctx->failure_from_state = COMM_FLASH_CHECK;
		return COMM_FAILURE;
	}

	if (z_impl_flash_get_size(flash_dev, &ctx->flash_size) != 0) {
		LOG_ERR("Failed to read flash size");
		ctx->failure_from_state = COMM_FLASH_CHECK;
		return COMM_FAILURE;
	}

	/* Validate that both slot addresses plus the write window fit within flash */
	const uint64_t slot_addrs[2] = {
		(uint64_t)TCP_SOCKET_FLASH_BASE_ADDRESS_0,
		(uint64_t)TCP_SOCKET_FLASH_BASE_ADDRESS_1,
	};
	for (int i = 0; i < 2; i++) {
		if (slot_addrs[i] >= ctx->flash_size) {
			LOG_ERR("Slot %d base address out of range (base=0x%llx flash=0x%llx)",
				i, slot_addrs[i], ctx->flash_size);
			ctx->failure_from_state = COMM_FLASH_CHECK;
			return COMM_FAILURE;
		}
		if ((slot_addrs[i] + (uint64_t)TOTAL_EXPECTED_WRITE_SIZE) > ctx->flash_size) {
			LOG_ERR("Slot %d write range exceeds flash (base=0x%llx size=%u flash=0x%llx)",
				i, slot_addrs[i],
				(uint32_t)TOTAL_EXPECTED_WRITE_SIZE,
				ctx->flash_size);
			ctx->failure_from_state = COMM_FLASH_CHECK;
			return COMM_FAILURE;
		}
	}

	ctx->flash_ready = true;
	LOG_INF("Flash detected. Size=0x%llx, slot0=0x%llx, slot1=0x%llx, write span=%u bytes",
		ctx->flash_size,
		(uint64_t)TCP_SOCKET_FLASH_BASE_ADDRESS_0,
		(uint64_t)TCP_SOCKET_FLASH_BASE_ADDRESS_1,
		(uint32_t)TOTAL_EXPECTED_WRITE_SIZE);

	return COMM_WIFI_CONNECTING;
}

static communication_state_t state_wifi_connecting(communication_context_t *ctx)
{
	LED_TURN_MAGENTA();
	if (my_wifi_init() != 0) {
		LOG_ERR("Failed to initialize WiFi module");
		ctx->failure_from_state = COMM_WIFI_CONNECTING;
		return COMM_FAILURE;
	}

	LOG_INF("Connecting to WiFi...");

	if (wifi_connect(BITCRAZE_SSID, BITCRAZE_PASSWORD)) {
		LOG_ERR("Failed to connect to WiFi");
		ctx->failure_from_state = COMM_WIFI_CONNECTING;
		return COMM_FAILURE;
	}

	ctx->wifi_connected = true;
	return COMM_WAITING_FOR_IP;
}

static communication_state_t state_waiting_for_ip(communication_context_t *ctx)
{
	LED_TURN_CYAN();

	if (wifi_wait_for_ip_addr(ctx->ip_addr) != 0) {
		LOG_ERR("Failed while waiting for IPv4 address");
		ctx->failure_from_state = COMM_WAITING_FOR_IP;
		return COMM_FAILURE;
	}

	return COMM_ESTABLISHING_SERVER;
}

static communication_state_t state_connecting_to_server(communication_context_t *ctx)
{
	int ret;

	LED_TURN_YELLOW();

	ctx->sock_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (ctx->sock_fd < 0) {
		LOG_ERR("Could not create socket (errno=%d)", errno);
		ctx->failure_from_state = COMM_ESTABLISHING_SERVER;
		return COMM_FAILURE;
	}
	ctx->socket_open = true;

	memset(&ctx->server_addr, 0, sizeof(ctx->server_addr));
	ctx->server_addr.sin_family = AF_INET;
	ctx->server_addr.sin_port = htons(SERVER_PORT);

	ret = zsock_inet_pton(AF_INET, SERVER_IP, &ctx->server_addr.sin_addr);
	if (ret != 1) {
		LOG_ERR("Invalid SERVER_IP (%s)", SERVER_IP);
		ctx->failure_from_state = COMM_ESTABLISHING_SERVER;
		return COMM_FAILURE;
	}

	ret = zsock_connect(ctx->sock_fd, (struct sockaddr *)&ctx->server_addr, sizeof(ctx->server_addr));
	if (ret < 0) {
		LOG_ERR("Could not connect to server (errno=%d)", errno);
		ctx->failure_from_state = COMM_ESTABLISHING_SERVER;
		return COMM_FAILURE;
	}

	// int nodelay = 1;
	// ret = zsock_setsockopt(ctx->sock_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
	// if (ret < 0) {
	// 	LOG_WRN("Failed to set TCP_NODELAY (errno=%d)", errno);
	// }

	LOG_INF("[Client] Connected to %s:%d", SERVER_IP, SERVER_PORT);

	if (!ctx->flash_ready) {
		LOG_ERR("Flash was not checked before network bring-up");
		ctx->failure_from_state = COMM_ESTABLISHING_SERVER;
		return COMM_FAILURE;
	}

	return COMM_RECEIVING_MESSAGES;
}

static communication_state_t state_receiving_messages(communication_context_t *ctx)
{
	const struct device *flash_dev = DEVICE_DT_GET(FLASH_NODE);
	uint32_t expected_packet_idx = 0U;
	uint32_t total_packets = 0U;
	off_t write_offset;

	if (!device_is_ready(flash_dev)) {
		LOG_ERR("Flash device not ready");
		ctx->failure_from_state = COMM_RECEIVING_MESSAGES;
		return COMM_FAILURE;
	}

	/* Slot detection: find current image, flash to the other slot */
	uint32_t image_header0 = 0U, ver0 = 0U;
	uint32_t image_header1 = 0U, ver1 = 0U;
	bool slot0_valid = false;
	bool slot1_valid = false;

	z_impl_flash_read(flash_dev, TCP_SOCKET_FLASH_BASE_ADDRESS_0, &image_header0, sizeof(image_header0));
	if (image_header0 == 0x96f3b83dU) {
		slot0_valid = true;
		z_impl_flash_read(flash_dev, TCP_SOCKET_FLASH_BASE_ADDRESS_0 + 0x14, &ver0, sizeof(ver0));
		LOG_INF("Slot 0: valid image v%u.%u.%u",
			ver0 & 0xFFU, (ver0 >> 8) & 0xFFU, (ver0 >> 16) & 0xFFU);
	} else {
		LOG_INF("Slot 0: no valid image (image_header=0x%08x)", image_header0);
	}

	z_impl_flash_read(flash_dev, TCP_SOCKET_FLASH_BASE_ADDRESS_1, &image_header1, sizeof(image_header1));
	if (image_header1 == 0x96f3b83dU) {
		slot1_valid = true;
		z_impl_flash_read(flash_dev, TCP_SOCKET_FLASH_BASE_ADDRESS_1 + 0x14, &ver1, sizeof(ver1));
		LOG_INF("Slot 1: valid image v%u.%u.%u",
			ver1 & 0xFFU, (ver1 >> 8) & 0xFFU, (ver1 >> 16) & 0xFFU);
	} else {
		LOG_INF("Slot 1: no valid image (image_header=0x%08x)", image_header1);
	}

	/*
	 * Determine the "current" slot:
	 *   - If only one slot is valid, that is current -> write to the other.
	 *   - If both are valid, the one with the higher version is current
	 *     -> write to the other. Tie goes to slot 0 (write to slot 1).
	 *   - If neither is valid, default to slot 1 as target.
	 */
	if (!slot0_valid && !slot1_valid) {
		LOG_WRN("No valid image found in either slot; defaulting to slot 1");
		write_offset = (off_t)TCP_SOCKET_FLASH_BASE_ADDRESS_1;
	} else if (slot0_valid && !slot1_valid) {
		LOG_INF("Current image: slot 0 -> writing to slot 1");
		write_offset = (off_t)TCP_SOCKET_FLASH_BASE_ADDRESS_1;
	} else if (!slot0_valid && slot1_valid) {
		LOG_INF("Current image: slot 1 -> writing to slot 0");
		write_offset = (off_t)TCP_SOCKET_FLASH_BASE_ADDRESS_0;
	} else {
		/* Both valid – compare version words directly (major.minor.patch packed the same way) */
		if (ver0 >= ver1) {
			LOG_INF("Current image: slot 0 (v%u.%u.%u >= v%u.%u.%u) -> writing to slot 1",
				ver0 & 0xFFU, (ver0 >> 8) & 0xFFU, (ver0 >> 16) & 0xFFU,
				ver1 & 0xFFU, (ver1 >> 8) & 0xFFU, (ver1 >> 16) & 0xFFU);
			write_offset = (off_t)TCP_SOCKET_FLASH_BASE_ADDRESS_1;
		} else {
			LOG_INF("Current image: slot 1 (v%u.%u.%u > v%u.%u.%u) -> writing to slot 0",
				ver1 & 0xFFU, (ver1 >> 8) & 0xFFU, (ver1 >> 16) & 0xFFU,
				ver0 & 0xFFU, (ver0 >> 8) & 0xFFU, (ver0 >> 16) & 0xFFU);
			write_offset = (off_t)TCP_SOCKET_FLASH_BASE_ADDRESS_0;
		}
	}

	/* Erase the target slot before writing */
	size_t erase_size = (size_t)TOTAL_EXPECTED_WRITE_SIZE;
	if (erase_size % 4096U != 0U) {
		erase_size += 4096U - (erase_size % 4096U);
	}
	LOG_INF("Erasing target slot [0x%llx .. 0x%llx)",
		(uint64_t)write_offset, (uint64_t)write_offset + (uint64_t)erase_size);
	int erase_ret = z_impl_flash_erase(flash_dev, write_offset, erase_size);
	if (erase_ret < 0) {
		LOG_ERR("Failed to erase target slot (%d)", erase_ret);
		ctx->failure_from_state = COMM_RECEIVING_MESSAGES;
		return COMM_FAILURE;
	}
	LOG_INF("Target slot erase complete");

	LOG_INF("Writing incoming image to flash at base address 0x%llx", (uint64_t)write_offset);
	ctx->failure_from_state = COMM_RECEIVING_MESSAGES;

	/* Signal to the server that we are ready to receive */
	int ready_ret = send_ack(ctx->sock_fd);
	if (ready_ret < 0) {
		LOG_ERR("Failed to send ready ACK (%d)", ready_ret);
		ctx->failure_from_state = COMM_RECEIVING_MESSAGES;
		return COMM_FAILURE;
	}
	LOG_INF("Ready ACK sent to server");

	for (;;) {
		iris_packet_t packet;
		int ret;
		// k_sleep(K_MSEC(1000));
		LED_TURN_GREEN();
		ret = recv_exact(ctx->sock_fd, (uint8_t *)&packet, sizeof(packet));
		if (ret < 0) {
			LOG_ERR("TCP receive failed (%d)", ret);
			return COMM_FAILURE;
		}
		LOG_DBG("Received packet idx=%u total=%u", packet.packet_idx, packet.packet_nmbr);
		if (packet.packet_idx != expected_packet_idx) {
			LOG_ERR("Out-of-order packet: expected=%u got=%u", expected_packet_idx, packet.packet_idx);
			return COMM_FAILURE;
		}

		if (expected_packet_idx == 0U) {
			total_packets = packet.packet_nmbr;
			if (total_packets == 0U) {
				LOG_ERR("Invalid total packet number 0");
				return COMM_FAILURE;
			}

			size_t total_write_size = (size_t)total_packets * IRIS_PACKET_PAYLOAD_SIZE;
			if (total_write_size > TOTAL_EXPECTED_WRITE_SIZE) {
				LOG_ERR("Incoming image (%u bytes) exceeds TOTAL_EXPECTED_WRITE_SIZE (%u bytes)",
					(uint32_t)total_write_size,
					(uint32_t)TOTAL_EXPECTED_WRITE_SIZE);
				return COMM_FAILURE;
			}
		}

		if (packet.packet_nmbr != total_packets) {
			LOG_ERR("Packet count mismatch: expected total=%u got total=%u", total_packets, packet.packet_nmbr);
			return COMM_FAILURE;
		}

		off_t packet_offset = write_offset + ((off_t)packet.packet_idx * IRIS_PACKET_PAYLOAD_SIZE);
		if (((uint64_t)packet_offset + IRIS_PACKET_PAYLOAD_SIZE) >
		    ((uint64_t)write_offset + (uint64_t)TOTAL_EXPECTED_WRITE_SIZE)) {
			LOG_ERR("Packet write exceeds configured range (packet=%u)", packet.packet_idx);
			return COMM_FAILURE;
		}

		ret = z_impl_flash_write(flash_dev, packet_offset, packet.payload, IRIS_PACKET_PAYLOAD_SIZE);
		if (ret < 0) {
			LOG_ERR("Flash write failed for packet=%u (%d)", packet.packet_idx, ret);
			return COMM_FAILURE;
		}

		ret = send_ack(ctx->sock_fd);
		if (ret < 0) {
			LOG_ERR("Failed to send ACK for packet=%u (%d)", packet.packet_idx, ret);
			return COMM_FAILURE;
		}
		LOG_DBG("ACK sent for packet=%u", packet.packet_idx);

		expected_packet_idx++;
		if (expected_packet_idx >= total_packets) {
			LOG_INF("Received and programmed all %u packets", total_packets);
			break;
		}

		LED_TURN_RED();
	}

	return COMM_CLEANUP;
}

static communication_state_t state_failure(communication_context_t *ctx)
{
	LOG_ERR("[Failure] Called from: %s", state_to_string(ctx->failure_from_state));
	LOG_ERR("[Failure] Context: sock_fd=%d socket_open=%d wifi_connected=%d exit_code=%d",
			ctx->sock_fd,
			ctx->socket_open,
			ctx->wifi_connected,
			ctx->exit_code);

	ctx->exit_code = -1;
	while (1) {
		/* Unique failure indication: blinking red */
		LED_TURN_RED();
		k_sleep(K_SECONDS(1));
		LED_TURN_OFF();
		k_sleep(K_SECONDS(1));
	}
	return COMM_CLEANUP;
}

static communication_state_t state_cleanup(communication_context_t *ctx)
{
	/* Unique cleanup indication: blinking blue */
	for (int i = 0; i < 3; i++) {
		LED_TURN_BLUE();
		k_sleep(K_MSEC(150));
		LED_TURN_OFF();
		k_sleep(K_MSEC(150));
	}

	if (ctx->socket_open) {
		zsock_close(ctx->sock_fd);
		ctx->socket_open = false;
		LOG_INF("[Server] Closed");
	}

	if (ctx->wifi_connected) {
		wifi_disconnect();
		ctx->wifi_connected = false;
	}

	return COMM_DONE;
}

int run_tcp_socket_demo(void)
{
	communication_state_t state = COMM_FLASH_CHECK;

	/* static so context does not live on the thread stack */
	static communication_context_t ctx = {
		.sock_fd = -1,
		.wifi_connected = false,
		.socket_open = false,
		.flash_ready = false,
		.exit_code = 0,
		.flash_size = 0,
		.failure_from_state = COMM_FAILURE,
	};

	int ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    ret |= gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
    ret |= gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        state = COMM_FAILURE;
    }
	LED_TURN_OFF();

	LOG_INF("TCP -> FLASH STREAMER, Version: %u.%u.%u", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL);

	while (state != COMM_DONE) {
		LOG_DBG("State: %s", state_to_string(state));
		switch (state) {
		case COMM_FLASH_CHECK:
			state = state_flash_check(&ctx);
			break;
		case COMM_WIFI_CONNECTING:
			state = state_wifi_connecting(&ctx);
			break;
		case COMM_WAITING_FOR_IP:
			state = state_waiting_for_ip(&ctx);
			break;
		case COMM_ESTABLISHING_SERVER:
			state = state_connecting_to_server(&ctx);
			break;
		case COMM_RECEIVING_MESSAGES:
			state = state_receiving_messages(&ctx);
			break;
		case COMM_FAILURE:
			state = state_failure(&ctx);
			break;
		case COMM_CLEANUP:
			state = state_cleanup(&ctx);
			break;
		case COMM_DONE:
			break;
		default:
			ctx.failure_from_state = state;
			state = COMM_FAILURE;
			break;
		}
	}
	LED_TURN_OFF();

	return ctx.exit_code;
}
