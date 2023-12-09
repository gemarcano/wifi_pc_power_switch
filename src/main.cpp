// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// Copyright: Gabriel Marcano, 2023
/// @file

#include <ntp.h>
#include <switch.h>
#include <server.h>

extern "C" {
#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>
}

#include <lwip/dns.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include <cstring>
#include <ctime>
#include <memory>
#include <expected>
#include <atomic>

// This secrets.h includes strings for WIFI_SSID and WIFI_PASSWORD
#include "secrets.h"

static QueueHandle_t comms;

void main_task(void*)
{
	printf("Connecting...\n");
	if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
		printf("failed to connect\n");
		return;
	}
	printf("Connected! %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));

	auto client = std::make_unique<ntp_client>();

	if (!client)
	{
		printf("Failed to init ntp_client\n");
		return;
	}

	while(true)
	{
		if (client->time_elapsed())
		{
			client->request();
			printf("High water mark: %lu\n", uxTaskGetStackHighWaterMark(NULL));
		}
		vTaskDelay(1000);
	}
	cyw43_arch_deinit();
}

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

void init_task(void*)
{
	// cyw43_arch_init _must_ be called within a FreeRTOS task, see
	// https://github.com/raspberrypi/pico-sdk/issues/1540
	if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA))
	{
		printf("failed to initialise cyw43\n");
		goto terminate;
	}
	cyw43_arch_enable_sta_mode();

	comms = xQueueCreate(1, sizeof(unsigned));

	TaskHandle_t handle;
	xTaskCreate(main_task, "main", 1280/4, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));
	xTaskCreate(switch_task, "blink", 256, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));
	xTaskCreate(network_task, "network", 256, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));

terminate:
	vTaskDelete(nullptr);
	for(;;);
}

__attribute__((constructor))
void initialization()
{
	stdio_init_all();
}

int main()
{
	TaskHandle_t init_handle;
	xTaskCreate(init_task, "init", 256, nullptr, tskIDLE_PRIORITY+1, &init_handle);
	vTaskCoreAffinitySet(init_handle, (1 << 1) | (1 << 0));
	vTaskStartScheduler();
	for(;;);
	return 0;
}
