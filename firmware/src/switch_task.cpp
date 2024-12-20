// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#include <pcrb/switch_task.h>
#include <pcrb/switch.h>

#include <gpico/log.h>

#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include <format>

using gpico::sys_log;

namespace pcrb
{

switch_queue switch_comms;

void switch_task(void*)
{
	static pcrb::pc_switch<22> switch_(false);
	for (;;)
	{
		unsigned data = 0;
		xQueueReceive(switch_comms.get(), &data, portMAX_DELAY);
		sys_log.push(std::format("switch task: toggling pin for {} ms", data));
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
		switch_.set(true);
		vTaskDelay(data);
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
		switch_.set(false);
	}
}

}
