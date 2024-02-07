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
#include <atomic>

// This secrets.h includes strings for WIFI_SSID and WIFI_PASSWORD
#include "secrets.h"

#include <switch_task.h>
#include <network_task.h>
#include <cli_task.h>

void status_callback(netif *netif_)
{
	sys_log.push("status: changed");
	sys_log.push(std::format("status: IP Address: {}", ip4addr_ntoa(netif_ip4_addr(netif_))));
	sys_log.push(std::format("status: NETIF flags: {:#02x}", netif_->flags));
	int32_t rssi = 0;
	cyw43_wifi_get_rssi(&cyw43_state, &rssi);
	sys_log.push(std::format("status: RSSI: {}", rssi));
	sys_log.push(std::format("status: Wifi state: {}", cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA)));
}

void link_callback(netif *netif_)
{
	sys_log.push("link changed");
	sys_log.push(std::format("link: IP Address: {}", ip4addr_ntoa(netif_ip4_addr(netif_))));
	sys_log.push(std::format("link: NETIF flags: {:#02x}", netif_->flags));
	int32_t rssi = 0;
	cyw43_wifi_get_rssi(&cyw43_state, &rssi);
	sys_log.push(std::format("link: RSSI: {}", rssi));
	sys_log.push(std::format("link: Wifi state: {}", cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA)));
}

static void init_wifi()
{
	sys_log.push(std::format("Connecting to SSID {}:", WIFI_SSID));
	for (;;)
	{
		int result = 0;
		if (!(result = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000))) {
			sys_log.push("    DONE");
			break;
		}
		sys_log.push(std::format("    FAILED: {}", result));
	}
}

static std::atomic_bool wifi_initd = false;

void wifi_management_task(void*)
{
	sys_log.push("Initializing cyw43 with USA region...: ");
	for (;;)
	{
		// cyw43_arch_init _must_ be called within a FreeRTOS task, see
		// https://github.com/raspberrypi/pico-sdk/issues/1540
		int result = 0;
		if (!(result = cyw43_arch_init_with_country(CYW43_COUNTRY_USA)))
		{
			sys_log.push("    DONE");
			break;
		}
		sys_log.push(std::format("    FAILED: {}", result));
	}

	cyw43_arch_enable_sta_mode();
	// Turn off powersave completely
	cyw43_wifi_pm(&cyw43_state, CYW43_DEFAULT_PM & ~0xf);

	// Setup link/status callbacks
	cyw43_arch_lwip_begin();
	netif_set_status_callback(netif_default, status_callback);
	netif_set_link_callback(netif_default, link_callback);
	cyw43_arch_lwip_end();

	init_wifi();
	wifi_initd = true;

	int wifi_state = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
	TickType_t last = xTaskGetTickCount();
	for(;;)
	{
		int current_state = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
		if (current_state != CYW43_LINK_JOIN)
		{
			sys_log.push(std::format("wifi: state is bad? {}", current_state));
			if (current_state != CYW43_LINK_DOWN)
			{
				sys_log.push(std::format("wifi: disconnecting from network"));
				cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
			}
			int connect_result;
			sys_log.push(std::format("wifi: trying to reconnect"));
			while (connect_result = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
				sys_log.push(std::format("FAILED to reconnect, result {}, trying again", connect_result));
			}
			sys_log.push(std::format("wifi: hopefully succeeded in connecting"));
			if (current_state != wifi_state)
				wifi_state = current_state;
		}
		vTaskDelayUntil(&last, 1000);
	}
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
	xTaskCreate(watchdog_task, "prb_watchdog", 256, nullptr, tskIDLE_PRIORITY+2, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));
	sys_log.register_push_callback(print_callback);

	xTaskCreate(wifi_management_task, "prb_wifi", 512, nullptr, tskIDLE_PRIORITY+2, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));

	while (!wifi_initd);

	sys_log.push(std::format("Connected with IP address {}", ip4addr_ntoa(netif_ip4_addr(netif_default))));

	// FIXME should we call this somewhere?
	//cyw43_arch_deinit();

	xTaskCreate(switch_task, "prb_switch", 256, nullptr, tskIDLE_PRIORITY+2, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));
	xTaskCreate(network_task, "prb_network", 512, nullptr, tskIDLE_PRIORITY+2, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));
	xTaskCreate(cli_task, "prb_cli", 512, nullptr, tskIDLE_PRIORITY+2, &handle);
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
	xTaskCreate(init_task, "prb_init", 256, nullptr, tskIDLE_PRIORITY+2, &init_handle);
	vTaskCoreAffinitySet(init_handle, (1 << 1) | (1 << 0));
	vTaskStartScheduler();
	for(;;);
	return 0;
}
