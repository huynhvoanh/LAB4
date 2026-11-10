#ifndef INC_SCHEDULER_H_
#define INC_SCHEDULER_H_

#include <stdint.h>

#define SCH_MAX_TASKS            10U
#define SCH_INVALID_TASK_ID      UINT32_MAX
#define SCH_MAX_DELAY_TICKS      255U

typedef enum
{
  SCH_ERROR_NONE = 0U,
  SCH_ERROR_TOO_MANY_TASKS,
  SCH_ERROR_INVALID_TASK,
  SCH_ERROR_READY_QUEUE_FULL,
  SCH_ERROR_DELAY_TOO_LONG
} SCH_ErrorCode;

void SCH_Init(void);
void SCH_Update(void);
void SCH_Dispatch_Tasks(void);
uint32_t SCH_Add_Task(void (*pFunction)(void), uint32_t DELAY,
                      uint32_t PERIOD);
uint8_t SCH_Delete_Task(uint32_t taskID);
uint32_t SCH_Get_Time_MS(void);
SCH_ErrorCode SCH_Get_Last_Error(void);

#endif /* INC_SCHEDULER_H_ */
