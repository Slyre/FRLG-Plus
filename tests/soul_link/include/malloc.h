#ifndef TEST_SOUL_LINK_MALLOC_H
#define TEST_SOUL_LINK_MALLOC_H
#include <stdlib.h>
#include "global.h"
extern bool8 gSoulLinkTestCacheAllocationFailure;
static inline void *Alloc(u32 size) { return gSoulLinkTestCacheAllocationFailure ? NULL : malloc(size); }
static inline void Free(void *pointer) { free(pointer); }
#endif
