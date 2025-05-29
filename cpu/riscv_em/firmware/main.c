#include <stdint.h>
#include <stddef.h>
#include "../src/core/comp_storage/cs_instructions.h"

#define reg_uart_data (*(volatile uint8_t*)0x3000000UL)

static char* test = "Hello World from a simple RV32I ISA emulator v2!\n";

void putchar(char c)
{
    reg_uart_data = c;
}

void print(const char *p)
{
    while (*p)
        putchar(*(p++));
}

int main(void)
{
    perase(17, 18, 19);
    perase(7, 8, 9);
    pread(1, 2, 3);
    pwrite(4, 5, 6);
    print(test);
    return 0;
}
