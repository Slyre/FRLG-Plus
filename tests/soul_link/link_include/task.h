#include "global.h"
#define TASK_NONE 0xFF
#define NUM_TASKS 16
u8 CreateTask(void (*callback)(u8), u8 priority);
void DestroyTask(u8 taskId);
u8 GetTaskCount(void);
u8 FindTaskIdByFunc(void (*callback)(u8));
