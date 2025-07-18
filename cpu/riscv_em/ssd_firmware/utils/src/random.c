#include "random.h"


static uint32_t xor_shift_state_32 = 0x12345678;
static uint64_t xor_shift_state_64 = 0x123456789ABCDEF0ull;

uint32_t rand32() {
    xor_shift_state_32 ^= xor_shift_state_32 >> 13;
    xor_shift_state_32 ^= xor_shift_state_32 << 17;
    xor_shift_state_32 ^= xor_shift_state_32 >> 5;
    return xor_shift_state_32; // return a 32-bit random number 
}

uint64_t rand64() {
    xor_shift_state_64 ^= xor_shift_state_64 >> 12;
    xor_shift_state_64 ^= xor_shift_state_64 << 25;
    xor_shift_state_64 ^= xor_shift_state_64 >> 27;
    return xor_shift_state_64 * 0x2545F4914F6CDD1Dull; // optional: scramble further
}

uint32_t rand32_range(uint32_t min, uint32_t max) {
    return min + (rand32() % (max - min + 1));
}
uint64_t rand64_range(uint64_t min, uint64_t max) {
    return min + (rand64() % (max - min + 1));
}