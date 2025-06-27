#ifndef __RISCV_UNIQUE__
#define __RISCV_UNIQUE__

#include <stdint.h>

template<typename It>
It Unique(It begin, It end) {
    if (begin == end) return end;

    It result = begin;
    for (It it = begin + 1; it != end; ++it) {
        if (!(*result == *it)) {
            ++result;
            *result = *it;
        }
    }
    return ++result;
} 

#endif