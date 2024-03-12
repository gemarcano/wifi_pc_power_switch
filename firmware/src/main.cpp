// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#include <pcrb/ntp.h>
#include <pcrb/log.h>
#include <pcrb/switch_task.h>
#include <pcrb/switch.h>
#include <pcrb/server.h>
#include <pcrb/syslog.h>
#include <pcrb/watchdog.h>
#include <pcrb/switch_task.h>
#include <pcrb/network_task.h>
#include <pcrb/cli_task.h>
#include <pcrb/wifi_management_task.h>
// This secrets.h includes strings for WIFI_SSID and WIFI_PASSWORD
#include "secrets.h"

#include <pico/unique_id.h>
#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>
#include <pico/bootrom.h>

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

void print_callback(std::string_view str)
{
	printf("syslog: %.*s\r\n", str.size(), str.data());
}

using pcrb::sys_log;

void init_task(void*)
{
	stdio_init_all();
	pcrb::initialize_watchdog_tasks();

	sys_log.register_push_callback(print_callback);
	xTaskCreateAffinitySet(pcrb::wifi_management_task, "prb_wifi", 512, nullptr, tskIDLE_PRIORITY+2, (1 << 0) | (1 << 1), nullptr);

	// Wait for wifi to be ready before continuing, this variable is set by the
	// wifi management task.
	while (!pcrb::wifi_initd);

	sys_log.push(std::format("Connected with IP address {}", ip4addr_ntoa(netif_ip4_addr(netif_default))));

	// FIXME should we call this somewhere?
	//cyw43_arch_deinit();

	xTaskCreateAffinitySet(pcrb::switch_task, "prb_switch", 256, nullptr, tskIDLE_PRIORITY+2, (1 << 0) | (1 << 1), nullptr);
	xTaskCreateAffinitySet(pcrb::network_task, "prb_network", 512, nullptr, tskIDLE_PRIORITY+2, (1 << 0) | (1 << 1), nullptr);
	xTaskCreateAffinitySet(pcrb::cli_task, "prb_cli", 512, nullptr, tskIDLE_PRIORITY+2, (1 << 0) | (1 << 1), nullptr);

	vTaskDelete(nullptr);
	for(;;);
}

int main()
{
	// Alright, based on reading the pico-sdk, it's pretty much just a bad idea
	// to do ANYTHING outside of a FreeRTOS task when using FreeRTOS with the
	// pico-sdk... just do all required initialization in the init task
	xTaskCreateAffinitySet(init_task, "prb_init", 256, nullptr, tskIDLE_PRIORITY+2, (1 << 1) | (1 << 0), nullptr);
	vTaskStartScheduler();
	for(;;);
	return 0;
}
