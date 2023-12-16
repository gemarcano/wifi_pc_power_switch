#include <FreeRTOS.h>
#include <semphr.h>
#include <malloc.h>

struct malloc_mutex_
{
	StaticSemaphore_t mutex_memory;
	SemaphoreHandle_t mutex;
	malloc_mutex_()
	{
		mutex = xSemaphoreCreateRecursiveMutexStatic(&mutex_memory);
	}
};

static malloc_mutex_ mutex;

extern "C"
{
	void __malloc_lock(struct _reent *ptr)
	{
		xSemaphoreTakeRecursive(mutex.mutex, portMAX_DELAY);
	}

	void __malloc_unlock(struct _reent *ptr)
	{
		xSemaphoreGiveRecursive(mutex.mutex);
	}

	void vApplicationGetIdleTaskMemory(StaticTask_t **idle_task_tcb, StackType_t **idle_task_stack, uint32_t *idle_stack_size, BaseType_t core_id)
	{
		static StaticTask_t task_tcb[configNUMBER_OF_CORES];
		static StackType_t task_stack[configNUMBER_OF_CORES][configMINIMAL_STACK_SIZE];

		*idle_task_tcb = &task_tcb[core_id];
		*idle_task_stack = task_stack[core_id];
		*idle_stack_size = sizeof(*task_stack)/sizeof(**task_stack);
	}

	void vApplicationGetTimerTaskMemory(StaticTask_t **timer_task_tcb, StackType_t **timer_task_stack, uint32_t *timer_stack_size)
	{
		static StaticTask_t task_tcb;
		static StackType_t task_stack[configTIMER_TASK_STACK_DEPTH];

		*timer_task_tcb = &task_tcb;
		*timer_task_stack = task_stack;
		*timer_stack_size = sizeof(task_stack)/sizeof(*task_stack);
	}
}
