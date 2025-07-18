#include "new.hh"

void* operator new(size_t size) noexcept{
    void* p = malloc(size);
    return p;
}

void* operator new(size_t, void* ptr) noexcept {
    return ptr;
}

void* operator new[](size_t size) noexcept {
    return malloc(size);
}

void* operator new[](size_t, void* ptr) noexcept {
    return ptr;
}

void operator delete(void* p) noexcept {
    free(p);
}

void operator delete(void* ptr, size_t size) noexcept {
    (void)size; 
    free(ptr);
}

void operator delete[](void* ptr) noexcept {
    free(ptr);
}

void operator delete[](void* ptr, size_t size) noexcept {
    (void)size;
    free(ptr);
}