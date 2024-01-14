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
#include <algorithm>

// This secrets.h includes strings for WIFI_SSID and WIFI_PASSWORD
#include "secrets.h"

static QueueHandle_t comms;
static pc_remote_button::server server_;
static safe_syslog<syslog<1024*128>> log;

void switch_task(void*)
{
	unsigned data = 0;
	static pc_remote_button::pc_switch<22> switch_(false);
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
	if (line[0] == 't')
	{
		unsigned long ms = strtoul(line + 1, nullptr, 0);
		printf("Toggling switch for %lu milliseconds\r\n", ms);
		int32_t data = std::clamp<unsigned long>(ms, 0, std::numeric_limits<int32_t>::max());
		xQueueSendToBack(comms, &data, 0);
	}

	if (line[0] == 's')
	{
		printf("IP Address: %s\r\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));
		printf("default instance: 0x%p\r\n", netif_default);
		printf("NETIF is up? %s\r\n", netif_is_up(netif_default) ? "yes" : "no");
		printf("NETIF flags: 0x%02X\r\n", netif_default->flags);
		printf("ticks: %lu\r\n", xTaskGetTickCount());
		UBaseType_t number_of_tasks = uxTaskGetNumberOfTasks();
		printf("Tasks active: %lu\r\n", number_of_tasks);
		std::vector<TaskStatus_t> tasks(number_of_tasks);
		uxTaskGetSystemState(tasks.data(), tasks.size(), nullptr);
		for (auto& status: tasks)
		{
			printf("  task name: %s\r\n", status.pcTaskName);
		}

		char foo[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
		pico_get_unique_board_id_string(foo, sizeof(foo));
		printf("unique id: %s\r\n", foo);

		printf("log size: %zu\r\n", log.size());
		for (size_t i = 0; i < log.size(); ++i)
		{
			printf("log %zu: %s\r\n", i, log[i].c_str());
		}
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
		TaskHandle_t handle = xTaskGetHandle("prb_watchdog");
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
	log.push("status changed");
}

void link_callback(netif *netif_)
{
	log.push("link changed");
}

void print_callback(std::string_view str)
{
	printf("syslog: %.*s\r\n", str.size(), str.data());
}

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
	xTaskCreate(watchdog_task, "prb_watchdog", 256, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));

	printf("Started, in init\r\n");
	log.register_push_callback(print_callback);

	// Loop indefinitely until we connect to WiFi
	for (;;)
	{
		log.push("Initializing cyw43 with USA region...: ");
		// cyw43_arch_init _must_ be called within a FreeRTOS task, see
		// https://github.com/raspberrypi/pico-sdk/issues/1540
		if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA))
		{
			log.push("    FAILED");
			continue;
		}
		log.push("    DONE");
		cyw43_arch_enable_sta_mode();

		char buffer[64] = {};
		snprintf(buffer, 64, "Connecting to SSID %s...: ", WIFI_SSID);
		log.push(buffer);
		if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
			log.push("    FAILED");
			continue;
		}
		log.push("    DONE");

		cyw43_arch_lwip_begin();
		netif_set_status_callback(netif_default, status_callback);
		netif_set_link_callback(netif_default, link_callback);
		cyw43_arch_lwip_end();

		break;
	}
	char buffer[64] = {};
	snprintf(buffer, 64, "Connected with IP address %s", ip4addr_ntoa(netif_ip4_addr(netif_list)));
	log.push(buffer);

	// FIXME should we call this somewhere?
	//cyw43_arch_deinit();1

	comms = xQueueCreate(1, sizeof(unsigned));

	xTaskCreate(switch_task, "prb_switch", 256, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));
	xTaskCreate(network_task, "prb_network", 256, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));
	xTaskCreate(cli_task, "prb_cli", 256, nullptr, tskIDLE_PRIORITY+1, &handle);
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
	xTaskCreate(init_task, "prb_init", 256, nullptr, tskIDLE_PRIORITY+1, &init_handle);
	vTaskCoreAffinitySet(init_handle, (1 << 1) | (1 << 0));
	vTaskStartScheduler();
	for(;;);
	return 0;
}
