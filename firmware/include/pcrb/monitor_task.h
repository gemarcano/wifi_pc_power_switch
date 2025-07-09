// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2025
/// @file

#ifndef PCRB_MONITOR_TASK_H_
#define PCRB_MONITOR_TASK_H_

#include <FreeRTOS.h>

namespace pcrb
{

bool current_pc_state();
void monitor_task(void*);

}

#endif//PCRB_MONITOR_TASK_H_
