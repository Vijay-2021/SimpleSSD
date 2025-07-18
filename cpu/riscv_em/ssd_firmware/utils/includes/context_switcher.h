#ifndef __RISCV_CONTEXT_SWITCHER__
#define __RISCV_CONTEXT_SWITCHER__

typedef enum {
    CSD_MODE = 0, 
    FIRMWARE_MODE = 1,
} context_switch_mode;

void context_switch(); // what should this do? 

#endif