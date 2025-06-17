#ifndef __RISCV_RANDOM__
#define __RISCV_RANDOM__

#include <stdint.h>

uint64_t rand32();
uint64_t rand64();
uint32_t rand32(uint32_t min, uint32_t max);
uint64_t rand64(uint64_t min, uint64_t max);

#endif