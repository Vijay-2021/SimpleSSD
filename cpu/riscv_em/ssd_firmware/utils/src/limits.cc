#include "limits.hh"

template<>
uint8_t numeric_max<uint8_t>() {
    return 0xFF; // 255
}

uint8_t numeric_min<uint8_t>() {
    return 0; // 0
}

uint16_t numeric_max<uint16_t>() {
    return 0xFFFF; // 65535
}

uint16_t numeric_min<uint16_t>() {
    return 0; // 0
}

template<>
uint32_t numeric_max<uint32_t>() {
    return 0xFFFFFFFF;
}

template<> 
uint32_t numeric_min<uint32_t>() {
    return 0;
}

template<>
uint64_t numeric_max<uint64_t>() {
    return 0xFFFFFFFFFFFFFFFF;
}

template<>
uint64_t numeric_min<uint64_t>() {
    return 0;
}

template<>
int64_t numeric_max<int64_t>() {
    return 0x7FFFFFFFFFFFFFFF; // 9223372036854775807
}   

template<>
int64_t numeric_min<int64_t>() {
    return -0x7FFFFFFFFFFFFFFF - 1; // -9223372036854775808
}

template<> 
int numeric_max<int>() {
    return 0x7FFFFFFF; // 2147483647
}

template<>
int numeric_min<int>() {
    return -0x7FFFFFFF - 1; // -2147483648
}

template<>
float numeric_max<float>() {
    return 3.402823466e+38F; // Maximum float value
}

template<> 
float numeric_min<float>() {
    return -3.402823466e+38F; // Minimum float value
}