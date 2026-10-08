#include "global.h"
struct Task { s16 data[16]; };
extern struct Task gTasks[16];
void DestroyTask(u8 taskId);
