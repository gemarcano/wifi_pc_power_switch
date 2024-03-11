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

std::atomic_int watchdog_cpu0_status = 0;
void watchdog_cpu0_task(void*)
{
	for(;;)
	{
		watchdog_cpu0_status = 1;
		vTaskDelay(50);
	}
}

std::atomic_int watchdog_cpu1_status = 0;
void watchdog_cpu1_task(void*)
{
	for(;;)
	{
		watchdog_cpu1_status = 1;
		vTaskDelay(50);
	}
}

void watchdog_task(void*)
{
	// The watchdog period needs to be long enough so long lock periods
	// (apparently something in the wifi subsystem holds onto a lock for a
	// while) are tolerated.
	watchdog_enable(200, true);
	for(;;)
	{
		if (watchdog_cpu1_status && watchdog_cpu0_status)
		{
			watchdog_update();
			watchdog_cpu0_status = 0;
			watchdog_cpu1_status = 0;
		}
		vTaskDelay(30);
	}
}

void init_task(void*)
{
	stdio_init_all();
	// FIXME Wait a second to give minicom some time to reconnect
	// Might be the Linux USB stack getting setup that's causing the delay
	vTaskDelay(1000);

	// Watchdog priority is higher
	// Dedicated watchdog tasks on each core, and have a central watchdog task
	// aggregate the watchdog flags from both other tasks.
	// If one core locks up, the central task will detect it and not pet the
	// watchdog, or it will itself be hung, leading to a system reset.
	TaskHandle_t handle;
	xTaskCreate(watchdog_cpu0_task, "watchdog_cpu0", 256, nullptr, tskIDLE_PRIORITY+2, &handle);
	vTaskCoreAffinitySet(handle, (1 << 0) );
	xTaskCreate(watchdog_cpu1_task, "watchdog_cpu1", 256, nullptr, tskIDLE_PRIORITY+2, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) );
	xTaskCreate(watchdog_task, "watchdog", 256, nullptr, tskIDLE_PRIORITY+2, &handle);
	vTaskCoreAffinitySet(handle, (1 << 0) | (1 << 1) );

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
