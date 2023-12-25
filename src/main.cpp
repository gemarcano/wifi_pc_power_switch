// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023
/// @file

#include <ntp.h>
#include <switch.h>
#include <server.h>
#include <syslog.h>

#include <pico/unique_id.h>
#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>
#include <pico/bootrom.h>
#include <hardware/watchdog.h>

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
static pc_remote_button::pc_switch<22> switch_(false);
static pc_remote_button::server server_;

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
	// Loop endlessly, restarting the server if there are errors
	for(;;)
	{
		// FIXME maybe move wifi initialization here?
		int err;
		do
		{
			err = server_.listen(48686);
			if (err != 0)
			{
				fprintf(stderr, "unable to listen on server, error %i\r\n", err);
			}
		} while (err != 0);

		for(;;)
		{
			auto result = server_.accept();
			if (!result)
			{
				fprintf(stderr, "unable to accept socket, error %i\r\n", result.error());
				// FIXME what if the error is terminal? Are there any terminal errors?
				break;
			}
			int32_t data = server_.handle_request(std::move(*result));
			xQueueSendToBack(comms, &data, 0);
		}

		server_.close();
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

		char foo[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
		pico_get_unique_board_id_string(foo, sizeof(foo));
		printf("unique id: %s\r\n", foo);
	}

	if (line[0] == 'r')
	{
		printf("Rebooting...\r\n");
		fflush(stdout);
		reset_usb_boot(0,0);
	}

	if (line[0] == 'k')
	{
		printf("Killing (hanging)...\r\n");
		fflush(stdout);
		TaskHandle_t handle = xTaskGetHandle("watchdog");
		vTaskDelete(handle);
		for(;;);
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

void watchdog_task(void*)
{
	for(;;)
	{
		watchdog_update();
		vTaskDelay(80);
	}
}

void init_task(void*)
{
	stdio_init_all();
	// FIXME Wait a second to give minicom some time to reconnect
	// Might be the Linux USB stack getting setup that's causing the delay
	vTaskDelay(1000);

	// Initialize watchdog hardware
	TaskHandle_t handle;
	watchdog_enable(100, true);
	xTaskCreate(watchdog_task, "watchdog", 256, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));

	printf("Started, in init\r\n");
	log.register_push_callback(print_callback);
	printf("Post log\r\n");

	// Loop indefinitely until we connect to WiFi
	for (;;)
	{
		printf("Initializing cyw43 with USA region...: ");
		// cyw43_arch_init _must_ be called within a FreeRTOS task, see
		// https://github.com/raspberrypi/pico-sdk/issues/1540
		if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA))
		{
			printf("FAILED\r\n");
			continue;
		}
		printf("DONE\r\n");
		cyw43_arch_enable_sta_mode();

		printf("Connecting to SSID %s...: ", WIFI_SSID);
		if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
			printf("FAILED\r\n");
			continue;
		}
		printf("DONE\r\n");

		cyw43_arch_lwip_begin();
		netif_set_status_callback(netif_default, status_callback);
		netif_set_link_callback(netif_default, link_callback);
		cyw43_arch_lwip_end();

		break;
	}
	printf("Connected with IP address %s\r\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));

	// FIXME should we call this somewhere?
	//cyw43_arch_deinit();1

	comms = xQueueCreate(1, sizeof(unsigned));

	xTaskCreate(switch_task, "switch", 256, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));
	xTaskCreate(network_task, "network", 256, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));
	xTaskCreate(cli_task, "cli", 256, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));

	vTaskDelete(nullptr);
	for(;;);
}

int main()
{
	// Alright, based on reading the pico-sdk, it's pretty much just a bad idea
	// to do ANYTHING outside of a FreeRTOS task when using FreeRTOS with the
	// pico-sdk... just do all required initialization in the init task
	TaskHandle_t init_handle;
	xTaskCreate(init_task, "init", 256, nullptr, tskIDLE_PRIORITY+1, &init_handle);
	vTaskCoreAffinitySet(init_handle, (1 << 1) | (1 << 0));
	vTaskStartScheduler();
	for(;;);
	return 0;
}
