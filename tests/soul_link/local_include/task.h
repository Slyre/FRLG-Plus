#include "global.h"
u8 CreateTask(void (*callback)(u8), u8 priority);
void DestroyTask(u8 taskId);
