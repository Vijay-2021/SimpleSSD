#include "random.h"

uint64_t rand32() {
    static uint32_t xorshift_state = 0x12345678;
    xor_shift_state ^= xor_shift_state >> 13;
    xor_shift_state ^= xor_shift_state << 17;
    xor_shift_state ^= xor_shift_state >> 5;
    return xorshift_state; // return a 32-bit random number
}

uint64_t rand64() {
    static uint64_t xorshift_state = 0x123456789ABCDEF0ull;
    xorshift_state ^= xorshift_state >> 12;
    xorshift_state ^= xorshift_state << 25;
    xorshift_state ^= xorshift_state >> 27;
    return xor_shift_state * 0x2545F4914F6CDD1Dull; // optional: scramble further
}

uint32_t rand32(uint32_t min, uint32_t max) {
    return min + (rand32() % (max - min + 1));
}
uint64_t rand64(uint64_t min, uint64_t max) {
    return min + (rand64() % (max - min + 1));
}