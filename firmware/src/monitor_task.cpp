// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2025
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
#include <atomic>

using gpico::sys_log;

namespace pcrb
{

static std::atomic_bool pc_state = false;

// FIXME wouldn't it be better to do this with interrupts?
void monitor_task(void*)
{
	constexpr const unsigned on_state_gpio = 21;
	gpio_init(on_state_gpio);
	gpio_disable_pulls(on_state_gpio);
	gpio_set_dir(on_state_gpio, GPIO_IN);

	static pcrb::pc_switch<22> switch_(false);
	for (;;)
	{
		bool state = gpio_get(on_state_gpio);
		if (state != pc_state)
		{
			// FIXME MQTT or something push?
		}
		pc_state = state;
		vTaskDelay(1000);
	}
}

bool current_pc_state()
{
	return pc_state;
}

}
