#ifndef RISCV_VECTOR
#define RISCV_VECTOR

#include <stdint.h>
#include <stddef.h> 
#include "memory.h"

template<typename T>
class Vector {
    T* data_;
    size_t size_;
    size_t capacity_;

public:
    Vector()
        : data_(nullptr), size_(0), capacity_(0) {}

    Vector(size_t count)
        : data_(nullptr), size_(count), capacity_(count) {
        if (count > 0) {
            data_ = (T*)malloc(count * sizeof(T));
            if (data_) {
                for (size_t i = 0; i < count; ++i)
                    data_[i] = T();
            } else {
                size_ = capacity_ = 0;
            }
        }
    }


    Vector(const Vector<T>& rhs) {
        capacity_ = rhs.capacity_;
        data_ = (T*)malloc(capacity_ * sizeof(T));
        size_ = 0; 
        for (size_t i = 0; i < rhs.size_; i++) {
            push_back(rhs[i]);
        }
    }

    ~Vector() {
        free(data_);
    }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }

    T& operator[](size_t idx) { return data_[idx]; }
    const T& operator[](size_t idx) const { return data_[idx]; }
    Vector<T>& operator=(const Vector<T>& rhs) {
        if (this != &rhs) {
            reserve(rhs.capacity);
            clear();
            for (size_t i = 0; i < size_; i++) {
                push_back(rhs[i]);
            }
        }
        return *this;
    }
    int push_back(const T& value) {
        if (size_ == capacity_) {
            size_t newCap = capacity_ ? capacity_ * 2 : 1;
            T* newData = (T*)realloc(data_, newCap * sizeof(T));
            if (!newData) return -1;
            data_ = newData;
            capacity_ = newCap;
        }
        data_[size_++] = value;
        return 0;
    }

    void pop_back() {
        if (size_) --size_;
    }

    void clear() {
        size_ = 0;
    }

    int reserve(size_t newCap) {
        if (newCap <= capacity_) return 0;
        T* newData = (T*)realloc(data_, newCap * sizeof(T));
        if (!newData) return -1;
        data_ = newData;
        capacity_ = newCap;
        return 0;
    }

    int resize(size_t newSize) {
        if (newSize > capacity_) {
            if (reserve(newSize) != 0) return -1;
        }
        if (newSize > size_) {
            for (size_t i = size_; i < newSize; ++i)
                data_[i] = T();
        }
        size_ = newSize;
        return 0;
    }
};


#endif