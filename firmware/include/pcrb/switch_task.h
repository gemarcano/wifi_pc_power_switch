// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#ifndef PCRB_SWITCH_TASK_H_
#define PCRB_SWITCH_TASK_H_

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

namespace pcrb
{

// get() must be called from within a FreeRTOS task!
class switch_queue
{
public:
	QueueHandle_t get()
	{
		if (!initialized)
		{
			handle = xQueueCreate(1, sizeof(unsigned));
			initialized = true;
		}
		return handle;
	}

private:
	bool initialized = false;
	QueueHandle_t handle;
};

extern switch_queue switch_comms;
void switch_task(void*);

}

#endif//PCRB_ SWITCH_TASK_H_
