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
#include <wifi_management_task.h>

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

	xTaskCreate(sctu::wifi_management_task, "prb_wifi", 512, nullptr, tskIDLE_PRIORITY+2, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));

	while (!sctu::wifi_initd);

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
