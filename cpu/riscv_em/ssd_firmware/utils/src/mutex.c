#include "mutex.h"
#include "utils.h"
#include "cs_instructions.h"


void mutex_init(Mutex *mutex) {
    mutex->next_ticket = 0;
    mutex->now_serving = 0;
}

void mutex_lock(Mutex *mutex) {
    uint64_t my_ticket;
    uint64_t one = 1;
    asm volatile (
        "amoadd.d %0, %2, (%1)"
        : "=r"(my_ticket)
        : "r"(&mutex->next_ticket), "r"(one)
        : "memory"
    );
    write_buffer(0, 0, ACQUIRE_MUTEX_LOCK);
    asm volatile("fence rw, rw" ::: "memory");
    while (mutex->now_serving != my_ticket) {
        // spin wait
    }
    asm volatile("fence rw, rw" ::: "memory");
    write_buffer(0, 0, ACQUIRED_MUTEX_LOCK);
}

void mutex_lock_untracked(Mutex *mutex) {
    uint64_t my_ticket;
    uint64_t one = 1;
    asm volatile (
        "amoadd.d %0, %2, (%1)"
        : "=r"(my_ticket)
        : "r"(&mutex->next_ticket), "r"(one)
        : "memory"
    );
    asm volatile("fence rw, rw" ::: "memory");
    while (mutex->now_serving != my_ticket) {
        // spin wait
    }
    asm volatile("fence rw, rw" ::: "memory");
}

void mutex_unlock(Mutex *mutex) {
    asm volatile("fence rw, rw" ::: "memory");
    mutex->now_serving++;
    asm volatile("fence rw, rw" ::: "memory");
}
