// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023
/// @file

#include <pico/stdlib.h>

template<int GPIOn>
pc_switch<GPIOn>::pc_switch(bool init_state)
{
	gpio_init(GPIOn);
	gpio_put(GPIOn, init_state);
	gpio_disable_pulls(GPIOn);
	gpio_set_dir(GPIOn, GPIO_OUT);
}

template<int GPIOn>
void pc_switch<GPIOn>::set(bool state)
{
	gpio_put(GPIOn, state);
}

template<int GPIOn>
bool pc_switch<GPIOn>::get() const
{
	return gpio_get(GPIOn);
}

template<int GPIOn>
void pc_switch<GPIOn>::toggle()
{
	set(!get());
}
