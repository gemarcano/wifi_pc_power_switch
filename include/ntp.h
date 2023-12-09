// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// Copyright: Gabriel Marcano, 2023
/// @file

#ifndef NTP_H_
#define NTP_H_

#include <pico/stdlib.h>

class ntp_client
{
public:
	ntp_client();
	int request();
	bool time_elapsed() const;

private:
	absolute_time_t ntp_timeout_time = 0;
};

#endif//NTP_H_
