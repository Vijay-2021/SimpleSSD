#ifndef __RISCV_SORT__
#define __RISCV_SORT__

#include <stddef.h>  // for size_t

// Merge two sorted halves: [begin, mid) and [mid, end)
template<typename T, typename Compare>
void merge_ranges(T* begin, T* mid, T* end, Compare comp, T* buffer) {
    T* left = begin;
    T* right = mid;
    T* out = buffer;

    while (left < mid && right < end) {
        if (comp(*right, *left)) {
            *out++ = *right++;
        } else {
            *out++ = *left++;
        }
    }
    while (left < mid)  { *out++ = *left++; }
    while (right < end) { *out++ = *right++; }

    // Copy merged region back
    T* src = buffer;
    for (T* d = begin; d < end; ++d, ++src) {
        *d = *src;
    }
}

template<typename T, typename Compare>
void merge_sort_impl(T* begin, T* end, Compare comp, T* buffer) {
    size_t n = end - begin;
    if (n < 2) return;
    T* mid = begin + n/2;
    merge_sort_impl(begin, mid, comp, buffer);
    merge_sort_impl(mid, end, comp, buffer);
    merge_ranges(begin, mid, end, comp, buffer);
}

// Public API with comparator
template<typename T, typename Compare>
void merge_sort(T* begin, T* end, Compare comp) {
    size_t n = end - begin;
    // Allocate temporary buffer with new/delete - replaceable for static buffer
    T* buffer = new T[n];
    merge_sort_impl(begin, end, comp, buffer);
    delete[] buffer;
}

// Public default API using operator<
template<typename T>
void merge_sort(T* begin, T* end) {
    merge_sort(begin, end, [](const T& a, const T& b) {
        return a < b;
    });
}

#endif // __RISCV_SORT__