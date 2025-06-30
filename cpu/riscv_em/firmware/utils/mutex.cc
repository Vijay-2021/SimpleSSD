#include "mutex.hh"

void Mutex::lock() {
    uint64_t my_ticket;
    asm volatile (
        "amoadd.d %0, x1, (%1)"  // my_ticket = ticket_++; x1 = 1
        : "=r"(my_ticket)
        : "r"(&next_ticket)
        : "memory"
    );
    
    while (now_serving != my_ticket) {
        // spin wait
    }
}

void Mutex::unlock() {
    now_serving++;
}
