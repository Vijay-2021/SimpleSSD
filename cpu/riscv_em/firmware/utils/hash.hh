#ifndef __RISCV_HASH__
#define __RISCV_HASH__

#include <stdint.h>
#include <stddef.h>

template<typename K>
size_t hash(const K& key) {
    return (size_t)&key;
}

#endif