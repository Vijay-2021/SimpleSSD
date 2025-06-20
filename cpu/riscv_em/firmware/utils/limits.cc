#include "limits.hh"

template<>
uint32_t numeric_max<uint32_t>() {
    return 0xFFFFFFFF;
}

template<> 
uint32_t numeric_min<uint32_t>() {
    return 0;
}

template<> 
int numeric_max<int>() {
    return 0x7FFFFFFF; // 2147483647
}

template<>
int numeric_min<int>() {
    return -0x7FFFFFFF - 1; // -2147483648
}