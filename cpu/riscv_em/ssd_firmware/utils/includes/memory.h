#ifndef __MEMORY_H_
#define __MEMORY_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint8_t status;
	uint64_t size;
} alloc_t;

extern void mm_init();
extern void mm_print_out();

extern void paging_init();
extern void paging_map_virtual_to_phys(uint64_t virt, uint64_t phys);

extern bool init_allocator(const void *base, const void *limit, const size_t heap_blocks, const size_t split_thresh, const size_t alignment);
extern void *malloc(size_t num);
extern void *calloc(size_t num, size_t size);
extern bool free(void *ptr);
extern void *realloc(void *ptr,size_t num);

extern size_t num_free();
extern size_t num_used();
extern size_t num_fresh();
extern bool check();

extern void* memcpy(void* dest, const void* src, size_t num );
extern void* memset (void * ptr, int value, size_t num );

extern void print_heap_top();

extern void set_heap_top(uint64_t top); // allows for fine grained control of memory regions without virtual memory
extern void set_heap_bottom(uint64_t bottom);
extern uint64_t get_heap_top();

#ifdef __cplusplus
}
#endif

#endif