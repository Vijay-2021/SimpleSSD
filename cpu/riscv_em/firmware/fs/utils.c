#include <stdarg.h>

#include "utils.h"
#include "string.h"
#include "memory.h"

void putchar(char c)
{
    reg_uart_data = c;
}

void print(const char *p)
{
    while (*p)
        putchar(*(p++));
}

void _itoa(int num, char *buffer, unsigned int base, char is_signed) {
    // base cases :)
    if (base == 0) {
      buffer[0] = '0';
      buffer[1] = '\0';
      return;
    }
    if (base > 32) {
      buffer[0] = '\0';
      return;
    }
  
    char *p = buffer;
    unsigned int divisor = base;
  
    if (is_signed && num < 0) {
      *p++ = '-';
      buffer++;
      num = -num;
    }
  
    /* Divide num by divisor until num == 0. */
    do {
      int remainder = num % divisor;
      *p++ = (remainder < 10) ? remainder + '0' : remainder + 'a' - 10;
    } while (num /= divisor);
  
    /* Terminate BUF. */
    *p = 0;
  
    /* Reverse BUF. */
    char *p1 = buffer, *p2 = p - 1;
    while (p1 < p2) {
      char tmp = *p1;
      *p1 = *p2;
      *p2 = tmp;
      p1++;
      p2--;
    }
}

void itoa(int num, char *buffer, int base) {
    _itoa(num, buffer, base, 1);
}

void uitoa(unsigned int num, char *buffer, int base) {
    _itoa(num, buffer, base, 0);
}

// internal printf helper
void _printf(char* format, char** args) {
    char value_buffer[32];
    for (size_t i = 0; i < strlen(format); i++) {
        if (format[i] != '%') {
        putchar(format[i]);
        continue;
        }
        // read next char and clear buffer
        char curr = format[++i];
        memset(value_buffer, 0, 32);
        switch (curr) {
        case 'p':
            print("0x");
            uitoa(*((int *)args++), value_buffer, 16);
            // write leading zeros
            for (size_t i = 0; i < PTR_WIDTH_TERMINAL - strlen(value_buffer); i++) {
                putchar('0');
            }
            print(value_buffer);
            break;
        case 'x':
            print("0x");
            uitoa(*((int *)args++), value_buffer, 16);
            print(value_buffer);
            break;
        case 'd':
            itoa(*((int *)args++), value_buffer, 10);
            print(value_buffer);
            break;
        case 'u':
            uitoa(*((unsigned int *)args++), value_buffer, 10);
            print(value_buffer);
            break;
        case 's':
            char *str = *args++;
            print("bello string");
            print(str);
            break;
        default:
            print("error in printf\n");
            break;
        
        }
    }
}

void printf(char *format, ...) {
    va_list args;
    va_start(args, format);
    _printf(format, args);
    va_end(args);
}
  
