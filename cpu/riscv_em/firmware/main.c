#include <stdint.h>
#include <stddef.h>
#include "cs_instructions.h"
#include "ext2.h"

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
	ext2_priv_data priv;
    ext2_probe(&priv);
	ext2_mount(&priv);
	ext2_touch("/home/test_csd.txt", &priv);
    return 0;
}
