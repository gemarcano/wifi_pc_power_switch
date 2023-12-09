// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// Copyright: Gabriel Marcano, 2023
/// @file

#ifndef SWITCH_H_

#include <pico/stdlib.h>

template<int GPIOn>
class pc_switch
{
public:
	pc_switch(bool init_state)
	{
		gpio_init(GPIOn);
		gpio_put(GPIOn, init_state);
		gpio_disable_pulls(GPIOn);
		gpio_set_dir(GPIOn, GPIO_OUT);
	}

	void set(bool state)
	{
		gpio_put(GPIOn, state);
	}

	bool get() const
	{
		return gpio_get(GPIOn);
	}

	void toggle()
	{
		set(!get());
	}
};

#endif//SWITCH_H_
