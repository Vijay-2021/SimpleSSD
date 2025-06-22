#ifndef RISCV_VECTOR
#define RISCV_VECTOR

#include <stdint.h>
#include <stddef.h> 
#include "memory.h"
#include "utils.h"
#include "new.hh"

template<typename T>
class Vector {
    T* data_;
    size_t size_;
    size_t capacity_;

public:

    using iterator = T*;
    iterator begin() { return data_; }
    iterator end() { return data_ + size_; }
    iterator cbegin() const { return data_; }
    iterator cend() const { return data_ + size_; }

    Vector()
        : data_(nullptr), size_(0), capacity_(0) {}

    Vector(size_t count)
        : data_(nullptr), size_(count), capacity_(count) {
        if (count > 0) {
            data_ = (T*)malloc(count * sizeof(T));
            if (data_) {
                for (size_t i = 0; i < count; ++i)
                    new(&data_[i]) T();
            } else {
                size_ = capacity_ = 0;
            }
        }
    }
    Vector(size_t count, const T& value)
        : data_(nullptr), size_(count), capacity_(count) {
        if (count > 0) {
            data_ = (T*)malloc(count * sizeof(T));
            if (data_) {
                for (size_t i = 0; i < count; ++i) {
                    new(&data_[i]) T(value);
                }
            } else {
                size_ = capacity_ = 0;
            }
        }
    }

    Vector(iterator begin, iterator end) {
        if (end < begin) {
            size_ = 0;
            capacity_ = 0;
            data_ = nullptr;
            return;
        } else {
            size_ = end - begin;
            capacity_ = size_;
            data_ = (T*)malloc(capacity_ * sizeof(T));
            if (data_) {
                for (size_t i = 0; i < size_; ++i) {
                    new (&data_[i]) T(*(begin + i));
                }
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

    Vector(Vector<T>&& rhs) {
        capacity_ = rhs.capacity_;
        data_ = rhs.data_;
        size_ = rhs.size_;
        rhs.data_ = nullptr;
        rhs.capacity_ = 0;
        rhs.size_ = 0;
    }

    ~Vector() {
        for (size_t i = 0; i < size_; ++i) {
            data_[i].~T();  // Call destructor explicitly
        }
        free(data_);
    }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }
    T& at(size_t idx) {
        return data_[idx];
    }
    T& operator[](size_t idx) { return data_[idx]; }
    const T& operator[](size_t idx) const { return data_[idx]; }
    Vector<T>& operator=(const Vector<T>& rhs) {
        if (this != &rhs) {
            reserve(rhs.capacity_);
            clear();
            for (size_t i = 0; i < size_; i++) {
                push_back(rhs[i]);
            }
        }
        return *this;
    }
    Vector<T>& operator=(Vector<T>&& rhs) noexcept {
        if (this != &rhs) {
            free(data_);
            data_ = rhs.data_;
            size_ = rhs.size_;
            capacity_ = rhs.capacity_;

            rhs.data_ = nullptr;
            rhs.size_ = 0;
            rhs.capacity_ = 0;
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
        if (size_) {
            size_--;
            data_[size_].~T();
        }
    }

    void clear() {
        for (size_t i = 0; i < size_; ++i) {
            data_[i].~T();
        }
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
                new (&data_[i]) T();
        }
        size_ = newSize;
        return 0;
    }
};


#endif