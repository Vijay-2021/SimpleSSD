#include <stdbool.h>
#include <stdio.h> 
#include <stdarg.h>

#include "riscv_helper.h"

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

int continue_sim() {
    return sim_continue;
}