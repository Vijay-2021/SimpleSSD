#ifndef __UTILS_H_
#define __UTILS_H_
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define reg_uart_data (*(volatile uint8_t*)0x3000000UL)
#define PTR_WIDTH_TERMINAL 16 // this is in the context of the terminal, so 16 characters per 64 bit pointer

void itoa(int64_t num, char *buffer, int base);
void uitoa(uint64_t num, char *buffer, int base);
void putchar(char c);
void print(const char *p);
void printf(char *format, ...);
void panic(char *format, ...);


#ifdef __cplusplus
}
#endif

#endif