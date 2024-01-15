// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#include <ntp.h>
#include <log.h>
#include <switch_task.h>
#include <switch.h>
#include <server.h>
#include <syslog.h>

#include <pico/unique_id.h>
#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>
#include <pico/bootrom.h>
#include <hardware/watchdog.h>

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

#include <switch_task.h>
#include <network_task.h>
#include <cli_task.h>

void status_callback(netif *netif_)
{
	sys_log.push("status changed");
}

void link_callback(netif *netif_)
{
	sys_log.push("link changed");
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
	sys_log.register_push_callback(print_callback);

	// Loop indefinitely until we connect to WiFi
	for (;;)
	{
		sys_log.push("Initializing cyw43 with USA region...: ");
		// cyw43_arch_init _must_ be called within a FreeRTOS task, see
		// https://github.com/raspberrypi/pico-sdk/issues/1540
		if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA))
		{
			sys_log.push("    FAILED");
			continue;
		}
		sys_log.push("    DONE");
		cyw43_arch_enable_sta_mode();

		sys_log.push(std::format("Connecting to SSID {}:", WIFI_SSID));
		if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
			sys_log.push("    FAILED");
			continue;
		}
		sys_log.push("    DONE");

		cyw43_arch_lwip_begin();
		netif_set_status_callback(netif_default, status_callback);
		netif_set_link_callback(netif_default, link_callback);
		cyw43_arch_lwip_end();

		break;
	}
	sys_log.push(std::format("Connected with IP address {}", ip4addr_ntoa(netif_ip4_addr(netif_default))));

	// FIXME should we call this somewhere?
	//cyw43_arch_deinit();

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
