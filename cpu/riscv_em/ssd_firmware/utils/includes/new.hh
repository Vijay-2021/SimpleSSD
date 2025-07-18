#ifndef __RISCV_NEW__
#define __RISCV_NEW__

#include <stddef.h>
#include <stdint.h>
#include "memory.h"

void* operator new(size_t size);
void* operator new(size_t, void* ptr);
void* operator new[](size_t size);
void* operator new[](size_t, void* ptr);
void operator delete(void* p);
void operator delete(void* ptr, size_t size);
void operator delete[](void* ptr);
void operator delete[](void* ptr, size_t size);

#endif