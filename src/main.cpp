// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023
/// @file

#include <ntp.h>
#include <switch.h>
#include <server.h>
#include <syslog.h>

#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>

#include <lwip/dns.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

extern "C" {
#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>
}

#include <cstring>
#include <ctime>
#include <memory>
#include <expected>
#include <atomic>

// This secrets.h includes strings for WIFI_SSID and WIFI_PASSWORD
#include "secrets.h"

static QueueHandle_t comms;
static pc_switch<22> switch_(false);
static server server_;

void switch_task(void*)
{
	unsigned data = 0;
	for (;;)
	{
		xQueueReceive(comms, &data, portMAX_DELAY);
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
		switch_.set(true);
		vTaskDelay(data);
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
		switch_.set(false);
	}
}

void network_task(void*)
{
	server_.listen(48686);
	for(;;)
	{
		int32_t data = server_.handle_request();
		xQueueSendToBack(comms, &data, 0);
	}
}

void run(const char* line)
{
	if (line[0] == 'a')
	{
		printf("aaaaaaaaaaaaaaaaaaaaaaaaah\r\n");
	}

	if (line[0] == 't')
	{
		unsigned long ms = strtoul(line + 1, nullptr, 0);
		printf("Toggling switch for %lu milliseconds\r\n", ms);
		switch_.set(true);
		vTaskDelay(ms);
		switch_.set(false);
	}

	if (line[0] == 's')
	{
		printf("IP Address: %s\r\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));
		printf("default instance: 0x%p\r\n", netif_default);
		printf("NETIF is up? %s\r\n", netif_is_up(netif_default) ? "yes" : "no");
		printf("NETIF flags: 0x%02X\r\n", netif_default->flags);
		printf("ticks: %lu\r\n", xTaskGetTickCount());
	}
}

void cli_task(void*)
{
	char line[33] = {0};
	int pos = 0;
	printf("> ");
	for(;;)
	{
		int c = fgetc(stdin);
		if (c != EOF)
		{
			if (c == '\r')
			{
				printf("\n");
				line[pos] = '\0';
				run(line);
				pos = 0;
				printf("> ");
				continue;
			}
			if (c == '\b')
			{
				if (pos > 0)
				{
					--pos;
					printf("\b \b");
				}
				continue;
			}

			if (pos < (sizeof(line) - 1))
			{
				line[pos++] = c;
				printf("%c", c);
			}
		}
		else
		{
			printf("WTF, we got an EOF?\r\n");
		}
	}
}

void status_callback(netif *netif_)
{
	printf("status changed\r\n");
}

void link_callback(netif *netif_)
{
	printf("link changed\r\n");
}

void print_callback(std::string_view str)
{
	printf("syslog: %.*s\r\n", str.size(), str.data());
}

static safe_syslog<syslog<1024*128>> log;

void init_task(void*)
{
	printf("In Init\r\n");
	log.register_push_callback(print_callback);
	printf("Post log\r\n");
	// cyw43_arch_init _must_ be called within a FreeRTOS task, see
	// https://github.com/raspberrypi/pico-sdk/issues/1540
	if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA))
	{
		printf("failed to initialise cyw43\n");
		goto terminate;
	}
	cyw43_arch_enable_sta_mode();

	cyw43_arch_lwip_begin();
	netif_set_status_callback(netif_default, status_callback);
	netif_set_link_callback(netif_default, link_callback);
	cyw43_arch_lwip_end();

	printf("Connecting...\n");
	if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
		printf("failed to connect\n");
		return;
	}
	printf("Connected! %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));
	printf("default: %p\r\n", netif_default);

	// FIXME should we call this somewhere?
	//cyw43_arch_deinit();1

	comms = xQueueCreate(1, sizeof(unsigned));

	TaskHandle_t handle;
	xTaskCreate(switch_task, "blink", 256, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));
	xTaskCreate(network_task, "network", 256, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));
	xTaskCreate(cli_task, "cli", 256, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));

terminate:
	vTaskDelete(nullptr);
	for(;;);
}

__attribute__((constructor))
void initialization()
{
	stdio_init_all();
	sleep_ms(1000);
	printf("Started\r\n");
}

int main()
{
	printf("In main\r\n");
	TaskHandle_t init_handle;
	xTaskCreate(init_task, "init", 256, nullptr, tskIDLE_PRIORITY+1, &init_handle);
	vTaskCoreAffinitySet(init_handle, (1 << 1) | (1 << 0));
	vTaskStartScheduler();
	for(;;);
	return 0;
}
