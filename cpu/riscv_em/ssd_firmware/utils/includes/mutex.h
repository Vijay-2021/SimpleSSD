#ifndef __RISCV_MUTEX__
#define __RISCV_MUTEX__
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "def.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Mutex {
    volatile uint64_t next_ticket;
    volatile uint64_t now_serving;
} Mutex;


void mutex_init(Mutex *mutex);
void mutex_lock(Mutex *mutex);
void mutex_unlock(Mutex *mutex);
void mutex_lock_untracked(Mutex *mutex);

#ifdef __cplusplus
}
#endif

#endif // __RISCV_MUTEX__