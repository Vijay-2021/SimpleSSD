#ifndef __RISCV_HASH__
#define __RISCV_HASH__

#include <stdint.h>
#include <stddef.h>

template<typename K>
size_t hash(const K& key) {
    return (size_t)&key;
}

template<>
size_t hash<uint32_t>(const uint32_t& key);

template<>
size_t hash<uint64_t>(const uint64_t& key);

template<>
size_t hash<int>(const int& key);

#endif