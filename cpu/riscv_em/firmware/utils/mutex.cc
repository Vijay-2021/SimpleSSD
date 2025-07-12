#include "mutex.hh"
#include "utils.h"

void Mutex::lock() {
    uint64_t my_ticket;
    uint64_t one = 1;
    asm volatile (
        "amoadd.d %0, %2, (%1)"
        : "=r"(my_ticket)
        : "r"(&next_ticket), "r"(one)
        : "memory"
    );
    while (now_serving != my_ticket) {
        // spin wait
    }
}

void Mutex::unlock() {
    now_serving++;
}
