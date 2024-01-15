// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#include <switch_task.h>

#include <switch.h>

#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>

extern "C" {
#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>
}

switch_queue switch_comms;

void switch_task(void*)
{
	static pc_remote_button::pc_switch<22> switch_(false);
	for (;;)
	{
		unsigned data = 0;
		xQueueReceive(switch_comms.get(), &data, portMAX_DELAY);
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
		switch_.set(true);
		vTaskDelay(data);
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
		switch_.set(false);
	}
}
