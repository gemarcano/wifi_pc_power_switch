// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2024
/// @file

#ifndef SCTU_WIFI_MANAGEMENT_TASK_H_
#define SCTU_WIFI_MANAGEMENT_TASK_H_

#include <atomic>

namespace sctu
{
	extern std::atomic_bool wifi_initd;
	void wifi_management_task(void*);
}

#endif//SCTU_WIFI_MANAGEMENT_TASK_H_