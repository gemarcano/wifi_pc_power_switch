// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#include <pico/stdlib.h>

namespace pcrb
{

template<unsigned GPIOn>
pc_switch<GPIOn>::pc_switch(bool init_state)
{
	gpio_init(GPIOn);
	gpio_put(GPIOn, init_state);
	gpio_disable_pulls(GPIOn);
	gpio_set_dir(GPIOn, GPIO_OUT);
}

template<unsigned GPIOn>
void pc_switch<GPIOn>::set(bool state)
{
	gpio_put(GPIOn, state);
}

template<unsigned GPIOn>
bool pc_switch<GPIOn>::get() const
{
	return gpio_get(GPIOn);
}

template<unsigned GPIOn>
void pc_switch<GPIOn>::toggle()
{
	set(!get());
}

}
