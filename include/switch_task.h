// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#ifndef SWITCH_TASK_H_
#define SWITCH_TASK_H_

extern "C" {
#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>
}

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

#endif//SWITCH_TASK_H_
