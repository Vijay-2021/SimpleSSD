#ifndef __RISCV_RANDOM__
#define __RISCV_RANDOM__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t rand32();
uint64_t rand64(); // tmp
uint32_t rand32_range(uint32_t min, uint32_t max);
uint64_t rand64_range(uint64_t min, uint64_t max);

#ifdef __cplusplus
}
#endif

#endif