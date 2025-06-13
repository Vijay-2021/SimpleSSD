#include <stdbool.h>
#include <stdio.h> 
#include <stdarg.h>

#include "riscv_helper.hh"

namespace SimpleSSD {

namespace CPU {

namespace RISCV {

int sim_continue = 1;

void die_msg(char* fmt, ...)
{
    va_list args;
    va_start(args,fmt);
    printf(fmt, args);
    va_end(args);
    printf("Fatal error, simulation will stop now!\n");
    sim_continue = 0;
}

int getContinueSim() {
    return sim_continue;
}

void setContinueSim(int sim_cont) {
    sim_continue = sim_cont;
}

} // namespace RISCV

} // namespace CPU

} // namespace SimpleSSD