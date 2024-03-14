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

#include <hardware/structs/mpu.h>

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

constexpr const unsigned CPU0_MASK = (1 << 0);
constexpr const unsigned CPU1_MASK = (1 << 1);
constexpr const unsigned CPUS_MASK = CPU0_MASK | CPU1_MASK;

void print_callback(std::string_view str)
{
	printf("syslog: %.*s\r\n", str.size(), str.data());
}

using pcrb::sys_log;

static void initialize_mpu()
{
	mpu_hw->ctrl = 5; // enable mpu with background default map
	mpu_hw->rbar = (0x0 & ~0xFFu)| M0PLUS_MPU_RBAR_VALID_BITS | 0;
	mpu_hw->rasr =
		1             // enable region
		| (0x7 << 1)  // size 2^(7 + 1) = 256
		| (0 << 8)    // Subregion disable-- don't disable any
		| 0x10000000; // Disable instruction fetch, disallow all
}

static void init_cpu_task(void* val)
{
	std::atomic_bool *cpu_init = reinterpret_cast<std::atomic_bool*>(val);
	initialize_mpu();
	*cpu_init = true;
	vTaskDelete(nullptr);
	for(;;);
}

void init_task(void*)
{
	std::array<std::atomic_bool, 2> cpu_init = {};
	stdio_init_all();
	pcrb::initialize_watchdog_tasks();
	xTaskCreateAffinitySet(
		init_cpu_task, "pcrb_cpu0", 256, &cpu_init[0], tskIDLE_PRIORITY+2, CPU0_MASK, nullptr);
	xTaskCreateAffinitySet(
		init_cpu_task, "pcrb_cpu1", 256, &cpu_init[1], tskIDLE_PRIORITY+2, CPU1_MASK, nullptr);

	// Wait until CPU init tasks are done
    while(!cpu_init[0] || !cpu_init[1])
    {
        taskYIELD();
    }

	sys_log.register_push_callback(print_callback);
	xTaskCreateAffinitySet(pcrb::wifi_management_task, "pcrb_wifi", 512, nullptr, tskIDLE_PRIORITY+2, CPUS_MASK, nullptr);

	// Wait for wifi to be ready before continuing, this variable is set by the
	// wifi management task.
	while (!pcrb::wifi_initd);

	sys_log.push(std::format("Connected with IP address {}", ip4addr_ntoa(netif_ip4_addr(netif_default))));

	// FIXME should we call this somewhere?
	//cyw43_arch_deinit();

	xTaskCreateAffinitySet(pcrb::switch_task, "pcrb_switch", 256, nullptr, tskIDLE_PRIORITY+2, CPUS_MASK, nullptr);
	xTaskCreateAffinitySet(pcrb::network_task, "pcrb_network", 512, nullptr, tskIDLE_PRIORITY+2, CPUS_MASK, nullptr);
	xTaskCreateAffinitySet(pcrb::cli_task, "pcrb_cli", 512, nullptr, tskIDLE_PRIORITY+2, CPUS_MASK, nullptr);

	vTaskDelete(nullptr);
	for(;;);
}

int main()
{
	// Alright, based on reading the pico-sdk, it's pretty much just a bad idea
	// to do ANYTHING outside of a FreeRTOS task when using FreeRTOS with the
	// pico-sdk... just do all required initialization in the init task
	xTaskCreateAffinitySet(init_task, "pcrb_init", 256, nullptr, tskIDLE_PRIORITY+2, CPUS_MASK, nullptr);
	vTaskStartScheduler();
	for(;;);
	return 0;
}
