#ifndef __FW_ALLOCATOR__
#define __FW_ALLOCATOR__

#include "def.hh"
#include "memory.h"

class MemoryAllocator {
    public:
        MemoryAllocator();
        ~MemoryAllocator();

        void *mmalloc(size_t size);
        void *mcalloc(size_t num_elems, size_t size);
        void mfree(void *ptr);
        void *mrealloc(void *ptr, size_t new_size);

};

#endif