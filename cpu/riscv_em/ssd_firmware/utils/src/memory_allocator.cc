#include "memory_allocator.hh"

MemoryAllocator::MemoryAllocator() {

}

MemoryAllocator::~MemoryAllocator() {
    // Destructor logic if needed
}

void *MemoryAllocator::mmalloc(size_t size) {
    if (size == 0) {
        return nullptr;
    }
    printf("Allocating %u bytes of memory\n", size);
    void *ptr = malloc(size);
    printf("Finished allocating %u bytes of memory at %p\n", size, ptr);
    if (ptr == nullptr) {
        panic("Memory allocation failed");
    }
    printf("returning? %p\n", ptr);
    return ptr;
}

void MemoryAllocator::mfree(void *ptr) {
    if (ptr != nullptr) {
        free(ptr);
    }
}  

void *MemoryAllocator::mrealloc(void *ptr, size_t new_size) {
    if (new_size == 0) {
        free(ptr);
        return nullptr;
    }
    void *new_ptr = realloc(ptr, new_size);
    if (new_ptr == nullptr) {
        panic("Memory reallocation failed");
    }
    return new_ptr;
}

void* MemoryAllocator::mcalloc(size_t size, size_t num_elems) {
    void *ptr = calloc(num_elems, size);
    return ptr;
}