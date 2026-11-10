/*
 * scheduler.c
 *
 * Cooperative scheduler using an O(1) free list, timing wheel and ready FIFO.
 */
#include "scheduler.h"
#include "stm32f1xx_hal.h"

#define SCH_NO_TASK              0xFFU
#define SCH_TICK_MS              10U
#define SCH_WHEEL_SIZE           256U
#define SCH_READY_QUEUE_LENGTH   32U

typedef struct
{
  void (*pTask)(void);
  uint32_t Period;
  uint8_t WheelSlot;
  uint8_t Next;
  uint8_t Previous;
  uint8_t Generation;
} sTask;

typedef struct
{
  uint8_t TaskID;
  uint8_t Generation;
} sReadyItem;

static sTask SCH_tasks_G[SCH_MAX_TASKS];
static uint8_t SCH_free_head_G;

static uint8_t SCH_wheel_head_G[SCH_WHEEL_SIZE];
static uint8_t SCH_wheel_tail_G[SCH_WHEEL_SIZE];
static uint8_t SCH_current_slot_G;

static sReadyItem SCH_ready_queue_G[SCH_READY_QUEUE_LENGTH];
static uint8_t SCH_ready_head_G;
static uint8_t SCH_ready_tail_G;
static uint8_t SCH_ready_count_G;

static volatile uint32_t SCH_time_ms_G;
static volatile SCH_ErrorCode SCH_error_G;

static uint32_t SCH_Enter_Critical(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void SCH_Exit_Critical(uint32_t primask)
{
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static uint8_t SCH_Is_Scheduled(uint8_t task_id)
{
  uint8_t slot = SCH_tasks_G[task_id].WheelSlot;

  return (uint8_t)((SCH_tasks_G[task_id].Previous != SCH_NO_TASK) ||
                   (SCH_tasks_G[task_id].Next != SCH_NO_TASK) ||
                   (SCH_wheel_head_G[slot] == task_id));
}

static void SCH_Schedule_Task(uint8_t task_id, uint32_t delay)
{
  uint8_t slot;
  uint8_t old_tail;

  /* A zero-delay task becomes ready on the next scheduler tick. */
  if (delay == 0U)
  {
    delay = 1U;
  }

  slot = (uint8_t)(SCH_current_slot_G + (uint8_t)delay);
  old_tail = SCH_wheel_tail_G[slot];

  SCH_tasks_G[task_id].WheelSlot = slot;
  SCH_tasks_G[task_id].Previous = old_tail;
  SCH_tasks_G[task_id].Next = SCH_NO_TASK;

  if (old_tail == SCH_NO_TASK)
  {
    SCH_wheel_head_G[slot] = task_id;
  }
  else
  {
    SCH_tasks_G[old_tail].Next = task_id;
  }

  SCH_wheel_tail_G[slot] = task_id;
}

static void SCH_Unschedule_Task(uint8_t task_id)
{
  uint8_t slot;
  uint8_t previous;
  uint8_t next;

  if (SCH_Is_Scheduled(task_id) == 0U)
  {
    return;
  }

  slot = SCH_tasks_G[task_id].WheelSlot;
  previous = SCH_tasks_G[task_id].Previous;
  next = SCH_tasks_G[task_id].Next;

  if (previous == SCH_NO_TASK)
  {
    SCH_wheel_head_G[slot] = next;
  }
  else
  {
    SCH_tasks_G[previous].Next = next;
  }

  if (next == SCH_NO_TASK)
  {
    SCH_wheel_tail_G[slot] = previous;
  }
  else
  {
    SCH_tasks_G[next].Previous = previous;
  }

  SCH_tasks_G[task_id].Next = SCH_NO_TASK;
  SCH_tasks_G[task_id].Previous = SCH_NO_TASK;
}

static void SCH_Queue_Ready_Task(uint8_t task_id)
{
  if (SCH_ready_count_G >= SCH_READY_QUEUE_LENGTH)
  {
    SCH_error_G = SCH_ERROR_READY_QUEUE_FULL;
    return;
  }

  SCH_ready_queue_G[SCH_ready_tail_G].TaskID = task_id;
  SCH_ready_queue_G[SCH_ready_tail_G].Generation =
      SCH_tasks_G[task_id].Generation;
  SCH_ready_tail_G = (uint8_t)((SCH_ready_tail_G + 1U) %
                               SCH_READY_QUEUE_LENGTH);
  SCH_ready_count_G++;
}

void SCH_Init(void)
{
  uint32_t primask = SCH_Enter_Critical();
  uint16_t slot;
  uint8_t index;

  for (slot = 0U; slot < SCH_WHEEL_SIZE; slot++)
  {
    SCH_wheel_head_G[slot] = SCH_NO_TASK;
    SCH_wheel_tail_G[slot] = SCH_NO_TASK;
  }

  for (index = 0U; index < SCH_MAX_TASKS; index++)
  {
    SCH_tasks_G[index].pTask = NULL;
    SCH_tasks_G[index].Period = 0U;
    SCH_tasks_G[index].WheelSlot = 0U;
    SCH_tasks_G[index].Next =
        (index + 1U < SCH_MAX_TASKS) ? (uint8_t)(index + 1U) : SCH_NO_TASK;
    SCH_tasks_G[index].Previous = SCH_NO_TASK;
    SCH_tasks_G[index].Generation = 0U;
  }

  SCH_free_head_G = 0U;
  SCH_current_slot_G = 0U;
  SCH_ready_head_G = 0U;
  SCH_ready_tail_G = 0U;
  SCH_ready_count_G = 0U;
  SCH_time_ms_G = 0U;
  SCH_error_G = SCH_ERROR_NONE;
  SCH_Exit_Critical(primask);
}

uint32_t SCH_Add_Task(void (*pFunction)(void), uint32_t DELAY,
                      uint32_t PERIOD)
{
  uint32_t primask;
  uint8_t task_id;

  if (pFunction == NULL)
  {
    SCH_error_G = SCH_ERROR_INVALID_TASK;
    return SCH_INVALID_TASK_ID;
  }

  if ((DELAY > SCH_MAX_DELAY_TICKS) ||
      (PERIOD > SCH_MAX_DELAY_TICKS))
  {
    SCH_error_G = SCH_ERROR_DELAY_TOO_LONG;
    return SCH_INVALID_TASK_ID;
  }

  primask = SCH_Enter_Critical();
  task_id = SCH_free_head_G;
  if (task_id == SCH_NO_TASK)
  {
    SCH_error_G = SCH_ERROR_TOO_MANY_TASKS;
    SCH_Exit_Critical(primask);
    return SCH_INVALID_TASK_ID;
  }

  SCH_free_head_G = SCH_tasks_G[task_id].Next;
  SCH_tasks_G[task_id].pTask = pFunction;
  SCH_tasks_G[task_id].Period = PERIOD;
  SCH_tasks_G[task_id].Next = SCH_NO_TASK;
  SCH_tasks_G[task_id].Previous = SCH_NO_TASK;
  SCH_tasks_G[task_id].Generation++;

  SCH_Schedule_Task(task_id, DELAY);
  SCH_Exit_Critical(primask);
  return (uint32_t)task_id;
}

uint8_t SCH_Delete_Task(uint32_t taskID)
{
  uint32_t primask;
  uint8_t task_id;

  if (taskID >= SCH_MAX_TASKS)
  {
    SCH_error_G = SCH_ERROR_INVALID_TASK;
    return 0U;
  }

  task_id = (uint8_t)taskID;
  primask = SCH_Enter_Critical();
  if (SCH_tasks_G[task_id].pTask == NULL)
  {
    SCH_error_G = SCH_ERROR_INVALID_TASK;
    SCH_Exit_Critical(primask);
    return 0U;
  }

  SCH_Unschedule_Task(task_id);
  SCH_tasks_G[task_id].pTask = NULL;
  SCH_tasks_G[task_id].Period = 0U;
  SCH_tasks_G[task_id].Generation++;
  SCH_tasks_G[task_id].Previous = SCH_NO_TASK;
  SCH_tasks_G[task_id].Next = SCH_free_head_G;
  SCH_free_head_G = task_id;

  SCH_Exit_Critical(primask);
  return 1U;
}

void SCH_Update(void)
{
  uint8_t task_id;

  SCH_time_ms_G += SCH_TICK_MS;
  SCH_current_slot_G++;

  task_id = SCH_wheel_head_G[SCH_current_slot_G];
  SCH_wheel_head_G[SCH_current_slot_G] = SCH_NO_TASK;
  SCH_wheel_tail_G[SCH_current_slot_G] = SCH_NO_TASK;

  while (task_id != SCH_NO_TASK)
  {
    uint8_t next_task = SCH_tasks_G[task_id].Next;

    SCH_tasks_G[task_id].Next = SCH_NO_TASK;
    SCH_tasks_G[task_id].Previous = SCH_NO_TASK;

    if (SCH_tasks_G[task_id].pTask != NULL)
    {
      SCH_Queue_Ready_Task(task_id);

      if (SCH_tasks_G[task_id].Period > 0U)
      {
        SCH_Schedule_Task(task_id, SCH_tasks_G[task_id].Period);
      }
    }

    task_id = next_task;
  }
}

void SCH_Dispatch_Tasks(void)
{
  for (;;)
  {
    uint32_t primask;
    uint8_t task_id;
    uint8_t generation;
    uint32_t period;
    void (*task)(void);

    primask = SCH_Enter_Critical();
    if (SCH_ready_count_G == 0U)
    {
      SCH_Exit_Critical(primask);
      break;
    }

    task_id = SCH_ready_queue_G[SCH_ready_head_G].TaskID;
    generation = SCH_ready_queue_G[SCH_ready_head_G].Generation;
    SCH_ready_head_G = (uint8_t)((SCH_ready_head_G + 1U) %
                                 SCH_READY_QUEUE_LENGTH);
    SCH_ready_count_G--;

    if ((SCH_tasks_G[task_id].pTask == NULL) ||
        (SCH_tasks_G[task_id].Generation != generation))
    {
      SCH_Exit_Critical(primask);
      continue;
    }

    task = SCH_tasks_G[task_id].pTask;
    period = SCH_tasks_G[task_id].Period;
    SCH_Exit_Critical(primask);

    task();

    if (period == 0U)
    {
      primask = SCH_Enter_Critical();
      if ((SCH_tasks_G[task_id].pTask != NULL) &&
          (SCH_tasks_G[task_id].Generation == generation))
      {
        SCH_Exit_Critical(primask);
        (void)SCH_Delete_Task(task_id);
      }
      else
      {
        SCH_Exit_Critical(primask);
      }
    }
  }
}

uint32_t SCH_Get_Time_MS(void)
{
  return SCH_time_ms_G;
}

SCH_ErrorCode SCH_Get_Last_Error(void)
{
  return SCH_error_G;
}
