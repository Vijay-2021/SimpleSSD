/** @author Levente Kurusa <levex@linux.com> **/
#include <stdint.h>
#include "memory.h"
#include "utils.h"
#define MAX_PAGE_ALIGNED_ALLOCS 32

extern char _heap_start;


uint32_t heap_end = 0x88000000;
uint32_t heap_begin = (uint32_t)&_heap_start;
uint32_t last_alloc = 0;
uint32_t pheap_begin = 0;
uint32_t pheap_end = 0;
uint8_t *pheap_desc = 0;
uint32_t memory_used = 0;


void free(void *mem)
{
	alloc_t *alloc = (mem - sizeof(alloc_t));
	memory_used -= alloc->size + sizeof(alloc_t);
	alloc->status = 0;
}

void pfree(void *mem)
{
	if((uint32_t)mem < pheap_begin || (uint32_t)mem > pheap_end) return;
	uint32_t ad = (uint32_t)mem;
	ad -= pheap_begin;
	ad /= 4096;
	pheap_desc[ad] = 0;
	return;
}

char* pmalloc(size_t size)
{
	for(int i = 0; i < MAX_PAGE_ALIGNED_ALLOCS; i++)
	{
		if(pheap_desc[i]) continue;
		pheap_desc[i] = 1;
		return (char *)(pheap_begin + i*4096);
	}
	return 0;
}

char* malloc(size_t size)
{
	if(!size) return 0;
	printf("calling malloc for size %u with heap begin %x, last alloc %x, and heap end %x\n", size, heap_begin, last_alloc, heap_end);
	if (last_alloc < heap_begin) {
		last_alloc = heap_begin;
	}
	/* Loop through blocks and find a block sized the same or bigger */
	uint8_t *mem = (uint8_t *)heap_begin;
	while((uint32_t)mem < last_alloc)
	{
		alloc_t *a = (alloc_t *)mem;
		/* If the alloc has no size, we have reaced the end of allocation */
		//mprint("mem=0x%x a={.status=%d, .size=%d}\n", mem, a->status, a->size);
		if(!a->size)
			goto nalloc;
		/* If the alloc has a status of 1 (allocated), then add its size
		 * and the sizeof alloc_t to the memory and continue looking.
		 */
		if(a->status) {
			mem += a->size;
			mem += sizeof(alloc_t);
			mem += 4;
			continue;
		}
		/* If the is not allocated, and its size is bigger or equal to the
		 * requested size, then adjust its size, set status and return the location.
		 */
		if(a->size >= size)
		{
			/* Set to allocated */
			a->status = 1;
			memset(mem + sizeof(alloc_t), 0, size);
			memory_used += size + sizeof(alloc_t);
			return (char *)(mem + sizeof(alloc_t));
		}
		/* If it isn't allocated, but the size is not good, then
		 * add its size and the sizeof alloc_t to the pointer and
		 * continue;
		 */
		mem += a->size;
		mem += sizeof(alloc_t);
		mem += 4;
	}

	nalloc:;
	if(last_alloc+size+sizeof(alloc_t) >= heap_end)
	{
		return 0;
	}
	alloc_t *alloc = (alloc_t *)last_alloc;
	if((uint32_t)alloc < heap_begin || (uint32_t)alloc > heap_end)
	{
		printf("malloc: alloc out of bounds: %x\n", alloc);
		return 0;
	}
	alloc->status = 1;
	alloc->size = size;

	last_alloc += size;
	last_alloc += sizeof(alloc_t);
	last_alloc += 4;
	memory_used += size + 4 + sizeof(alloc_t);
	// memset((char *)((uint32_t)alloc + sizeof(alloc_t)), 0, size); this isn't calloc
	printf("finished malloc and returnings %x\n", (uint32_t)alloc + sizeof(alloc_t));
	return (char *)((uint32_t)alloc + sizeof(alloc_t));
}