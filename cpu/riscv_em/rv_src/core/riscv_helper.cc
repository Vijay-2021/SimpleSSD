#include <stdbool.h>
#include <stdio.h> 
#include <stdarg.h>

#include "riscv_helper.hh"

namespace SimpleSSD {

namespace CPU {

namespace RISCV {

SOC_RUN_MODE soc_run_mode_ = PAUSED_MODE; // default mode is paused

void die_msg(char* fmt, ...)
{
    va_list args;
    va_start(args,fmt);
    printf(fmt, args);
    va_end(args);
    printf("Fatal error, simulation will stop now!\n");
    soc_run_mode_ = FAILED_MODE; // stop the simulation
}

} // namespace RISCV

} // namespace CPU

} // namespace SimpleSSD