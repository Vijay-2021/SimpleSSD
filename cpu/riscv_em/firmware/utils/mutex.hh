#ifndef __RISCV_MUTEX__
#define __RISCV_MUTEX__
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

class Mutex {
    private: 
        volatile uint64_t next_ticket;
        volatile uint64_t now_serving;
    public:
        Mutex() : next_ticket(0), now_serving(0) {}
        ~Mutex() {}
        void lock();
        void unlock();

}

#endif // __RISCV_MUTEX__