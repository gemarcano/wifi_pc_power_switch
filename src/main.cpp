// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// Copyright: Gabriel Marcano, 2023
/// @file

extern "C" {
#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>
}

#include <lwip/dns.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include <cstring>
#include <ctime>
#include <memory>
#include <expected>
#include <atomic>

constexpr const char* NTP_SERVER = "pool.ntp.org";
constexpr const int NTP_MSG_LEN = 48;
constexpr const int NTP_PORT = 123;
constexpr const int NTP_DELTA = 2208988800; // seconds between 1 Jan 1900 and 1 Jan 1970
constexpr const int ntpEST_TIME = (30 * 1000);
constexpr const int NTP_FAIL_TIME = (10 * 1000);

// This secrets.h includes strings for WIFI_SSID and WIFI_PASSWORD
#include "secrets.h"

class ntp_client
{
public:
	ntp_client()
	{
		ip_addr_t dns_server;
		cyw43_arch_lwip_begin();
		inet_pton(AF_INET, "1.1.1.1", &dns_server);
		dns_setserver(0, &dns_server);
		cyw43_arch_lwip_end();
	}

	int request()
	{
		// Look up IP address of NTP server first, in case we're looking at an
		// NTP pool
		const addrinfo ntp_info = {
			.ai_flags = 0,
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_DGRAM,
			.ai_protocol = 0,
		};
		addrinfo *dns_result = NULL;
		printf("Trying DNS...\n");
		int err = getaddrinfo(NTP_SERVER, "123", &ntp_info, &dns_result);
		printf("DNS err: %d %p\n", err, dns_result);
		printf("High water mark: %lu\n", uxTaskGetStackHighWaterMark(NULL));
		printf("%d %d %d\n", dns_result->ai_family, dns_result->ai_socktype, dns_result->ai_protocol);
		int s = socket(dns_result->ai_family, dns_result->ai_socktype, dns_result->ai_protocol);
		printf("socket: %d\n", s);
		timeval timeout = {
			.tv_sec = 5,
		};
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		err = connect(s, dns_result->ai_addr, dns_result->ai_addrlen);
		printf("connect err: %d\n", err);
		freeaddrinfo(dns_result);

		unsigned char request[NTP_MSG_LEN] = {0};
		request[0] = 0x1b;
		int amount = send(s, request, NTP_MSG_LEN, 0);
		printf("amount sent: %d\n", amount);
		amount = ::recv(s, request, NTP_MSG_LEN, 0);
		printf("amount received: %d\n", amount);
		close(s);

		uint8_t mode = request[0] & 0x7;
		uint8_t stratum = request[1];

		// Check the result
		if (amount == NTP_MSG_LEN &&
			mode == 0x4 &&
			stratum != 0)
		{
			uint8_t seconds_buf[4] = {};
			memcpy(seconds_buf, request + 40, 4);
			uint32_t seconds_since_1900 = seconds_buf[0] << 24 | seconds_buf[1] << 16 | seconds_buf[2] << 8 | seconds_buf[3];
			uint32_t seconds_since_1970 = seconds_since_1900 - NTP_DELTA;
			time_t epoch = seconds_since_1970;
			struct tm *utc = gmtime(&epoch);
			printf("got ntp response: %02d/%02d/%04d %02d:%02d:%02d\n", utc->tm_mday, utc->tm_mon + 1, utc->tm_year + 1900,
				   utc->tm_hour, utc->tm_min, utc->tm_sec);
			ntp_timeout_time = make_timeout_time_ms(ntpEST_TIME);
			return 0;
		}

		printf("invalid ntp response\n");
		return -1;
	}

	bool time_elapsed() const
	{
		printf("diff: %lld\n", absolute_time_diff_us(get_absolute_time(), ntp_timeout_time));
		return absolute_time_diff_us(get_absolute_time(), ntp_timeout_time) < 0;
	}

private:
	absolute_time_t ntp_timeout_time = 0;
};

static QueueHandle_t comms;

void main_task(void*)
{
	printf("Connecting...\n");
	if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
		printf("failed to connect\n");
		return;
	}
	printf("Connected!\n");

	auto client = std::make_unique<ntp_client>();
	if (!client)
	{
		printf("Failed to init ntp_client\n");
		return;
	}

	while(true)
	{
		if (client->time_elapsed())
		{
			client->request();
			printf("High water mark: %lu\n", uxTaskGetStackHighWaterMark(NULL));
			static int count = 0;
			count++;
			count %= 2;
			unsigned data = 500;
			if (count)
				data /= 2;
			xQueueSendToBack(comms, &data, 0);
		}
		vTaskDelay(1000);
	}
	cyw43_arch_deinit();
}

void blink_task(void*)
{
	unsigned data = 0;
	xQueueReceive(comms, &data, portMAX_DELAY);
	for (;;)
	{
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
		vTaskDelay(data);
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
		vTaskDelay(data);
		if (uxQueueMessagesWaiting(comms))
		{
			xQueueReceive(comms, &data, portMAX_DELAY);
			gpio_put(22, data > 250);
		}
	}
}

void init_task(void*)
{
	// cyw43_arch_init _must_ be called within a FreeRTOS task, see
	// https://github.com/raspberrypi/pico-sdk/issues/1540
	if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA))
	{
		printf("failed to initialise cyw43\n");
		return;
	}
	cyw43_arch_enable_sta_mode();

	comms = xQueueCreate(1, sizeof(unsigned));

	TaskHandle_t handle;
	xTaskCreate(main_task, "main", 1280/4, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));
	xTaskCreate(blink_task, "blink", 256, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));
	vTaskDelete(nullptr);
	for(;;);
}

__attribute__((constructor))
void initialization()
{
	stdio_init_all();
	gpio_init(22);
	gpio_put(22, 0);
	gpio_set_dir(22, GPIO_OUT);
}

int main()
{
	TaskHandle_t init_handle;
	xTaskCreate(init_task, "init", 256, nullptr, tskIDLE_PRIORITY+1, &init_handle);
	vTaskCoreAffinitySet(init_handle, (1 << 1) | (1 << 0));
	vTaskStartScheduler();
	for(;;);
	return 0;
}
