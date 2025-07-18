#include "memory.h"
#include <stdint.h>
#include <stddef.h>


void *memset(void *s, int c, size_t n) {
  uint64_t *long_s = (uint64_t *)s;
  size_t blocks = n >> 3;
  // construct mask
  uint64_t c_mask = ((uint64_t)c << 56) | ((uint64_t)c << 48) | ((uint64_t)c << 40) 
                  | ((uint64_t)c << 32) | ((uint64_t)c << 24) | ((uint64_t)c << 16) 
                  | ((uint64_t)c << 8)  | (uint64_t)c;
  for (size_t i = 0; i < blocks; ++i) {
    long_s[i] = c_mask;
  }
  // n is multiple of 8
  size_t n_complete = blocks << 3;
  if (n_complete == n) return s;

  // set remainder byte-wise
  uint8_t *byte_s = (uint8_t *)s;
  for (size_t i = n_complete; i < n; ++i) {
    byte_s[i] = (uint8_t)c;
  }
  return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
  uint64_t *long_src = (uint64_t *)src;
  uint64_t *long_dest = (uint64_t *)dest;
  size_t blocks = n >> 3;
  for (size_t i = 0; i < blocks; ++i) {
    long_dest[i] = long_src[i];
  }
  // n is multiple of 8
  size_t n_complete = blocks << 3;
  if (n_complete == n) return dest;

  // set remainder byte-wise
  uint8_t *byte_src = (uint8_t *)src;
  uint8_t *byte_dest = (uint8_t *)dest;
  for (size_t i = n_complete; i < n; ++i) {
    byte_dest[i] = byte_src[i];
  }
  return dest;
}