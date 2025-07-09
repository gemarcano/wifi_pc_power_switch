// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#ifndef PCRB_SWITCH_H_
#define PCRB_SWITCH_H_

#include <pico/stdlib.h>

namespace pcrb
{

template<unsigned GPIOn>
class pc_switch
{
public:
	pc_switch(bool init_state);
	void set(bool state);
	bool get() const;
	void toggle();
};

}

#include "switch.ipp"

#endif//PCRB_SWITCH_H_
