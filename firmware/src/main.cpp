// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#include <pcrb/ntp.h>
#include <pcrb/switch_task.h>
#include <pcrb/switch.h>
#include <pcrb/server.h>
#include <pcrb/switch_task.h>
#include <pcrb/network_task.h>
#include <pcrb/cli_task.h>
#include <pcrb/wifi_management_task.h>
// This secrets.h includes strings for WIFI_SSID and WIFI_PASSWORD
#include "secrets.h"

#include <gpico/log.h>
#include <gpico/watchdog.h>
#include <gpico/cdc_device.h>
#include <gpico/usb.h>

#include <pico/unique_id.h>
#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>
#include <pico/bootrom.h>

#include <tusb_config.h>
#include <tusb.h>
#include <bsp/board_api.h>

#include <lwip/netdb.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include <cstring>
#include <ctime>
#include <memory>
#include <expected>
#include <atomic>
#include <algorithm>
#include <atomic>
#include <format>

constexpr const unsigned CPU0_MASK = (1 << 0);
constexpr const unsigned CPU1_MASK = (1 << 1);
constexpr const unsigned CPUS_MASK = CPU0_MASK | CPU1_MASK;

void print_callback(std::string_view str)
{
	printf("syslog: %.*s\r\n", str.size(), str.data());
}

using gpico::sys_log;

void init_task(void*)
{
	gpico::initialize_watchdog_tasks();
	gpico::initialize_usb_task();

	sys_log.register_push_callback(print_callback);

	xTaskCreateAffinitySet(pcrb::cli_task, "pcrb_cli", 512, nullptr, tskIDLE_PRIORITY+1, CPUS_MASK, nullptr);
	xTaskCreateAffinitySet(pcrb::wifi_management_task, "pcrb_wifi", 512, nullptr, tskIDLE_PRIORITY+2, CPUS_MASK, nullptr);

	// Wait for wifi to be ready before continuing, this variable is set by the
	// wifi management task.
	while (!pcrb::wifi_initd) {
		taskYIELD();
	}

	sys_log.push(std::format("Connected with IP address {}", ip4addr_ntoa(netif_ip4_addr(netif_default))));

	// FIXME should we call this somewhere?
	//cyw43_arch_deinit();

	xTaskCreateAffinitySet(pcrb::switch_task, "pcrb_switch", 256, nullptr, tskIDLE_PRIORITY+2, CPUS_MASK, nullptr);
	xTaskCreateAffinitySet(pcrb::network_task, "pcrb_network", 512, nullptr, tskIDLE_PRIORITY+2, CPUS_MASK, nullptr);

	vTaskDelete(nullptr);
	for(;;);
}

int main()
{
	// Alright, based on reading the pico-sdk, it's pretty much just a bad idea
	// to do ANYTHING outside of a FreeRTOS task when using FreeRTOS with the
	// pico-sdk... just do all required initialization in the init task
	xTaskCreateAffinitySet(init_task, "pcrb_init", 512, nullptr, tskIDLE_PRIORITY+1, CPUS_MASK, nullptr);
	vTaskStartScheduler();
	for(;;);
	return 0;
}
