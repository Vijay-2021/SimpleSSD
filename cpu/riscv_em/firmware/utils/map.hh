#ifndef __RISCV_MAP__
#define __RISCV_MAP__

#include "memory.h"
#include "utils.h"
#include "pair.hh"
#include "hash.hh"

template<typename K, typename V>
class HashMap {
public:
    struct Entry {
        Pair<K, V> data;
        bool occupied;
    };

    class iterator {
        friend class HashMap;
        Entry* ptr;
        Entry* end;
    public:
        iterator(Entry* p, Entry* e) : ptr(p), end(e) {
            while (ptr != end && !ptr->occupied) ++ptr;
        }
        iterator() : ptr(nullptr), end(nullptr) {}
        Pair<K, V> operator*() const { return ptr->data; }
        iterator& operator++() { 
            do { ++ptr; } while (ptr != end && !ptr->occupied);
            return *this;
        }
        iterator& operator--() {
            do { --ptr; } while (ptr != end && !ptr->occupied);
            return *this;
        }
        bool operator!=(const iterator& other) const { return ptr != other.ptr; }
        bool operator==(const iterator& other) const { return ptr == other.ptr; }
        iterator& operator=(const iterator& other) {
            if (this != &other) {
                ptr = other.ptr;
                end = other.end;
            }
            return *this;
        }
        Pair<K,V>* operator->() const { return &(ptr->data); }
    };

    HashMap(size_t cap) : capacity_(cap), size_(0) {
        table_ = (Entry*)malloc(sizeof(Entry) * capacity_);
        for (size_t i = 0; i < capacity_; i++) {
            table_[i].occupied = false;
        }
    }

    ~HashMap() { free(table_); }

    V& operator[](const K& key) {
        if (size_ >= capacity_) {
            panic("HashMap is full, cannot insert new element\n");
        }
        size_t idx = hash(key) % capacity_;
        while (table_[idx].occupied) {
            if (table_[idx].data.first == key)
                return table_[idx].value;
            idx = (idx + 1) % capacity_;
        }
        new (&table_[idx].data) Pair<K, V>();
        table_[idx].occupied = true;
        ++size_;
        return table_[idx].value;
    }

    iterator find(const K& key) {
        size_t idx = hash(key) % capacity_;
        while (table_[idx].occupied) {
            if (table_[idx].data.first == key)
                return iterator(&table_[idx], table_ + capacity_);
            idx = (idx + 1) % capacity_;
        }
        return end();
    }
    Pair<iterator, bool> emplace(const K& key, const V& value) {
        if (size_ >= capacity_) {
            panic("HashMap is full, cannot insert new element\n");
        }
        size_t idx = hash(key) % capacity_;
        while (table_[idx].occupied) {
            if (table_[idx].data.first == key) {
                return Pair<iterator, bool>(iterator(&table_[idx], table_ + capacity_), false);
            }
            idx = (idx + 1) % capacity_;
        }
        new (&table_[idx].data) Pair<K, V>();
        table_[idx].occupied = true;
        ++size_;
        return Pair<iterator, bool>(iterator(&table_[idx], table_ + capacity_), true);
    }
    size_t count(const K& key) const {
        size_t idx = hash(key) % capacity_;
        while (table_[idx].occupied) {
            if (table_[idx].data.first == key)
                return 1;
            idx = (idx + 1) % capacity_;
        }
        return 0;
    }
    
    void erase(const K& key) {
        size_t idx = hash(key) % capacity_;
        while (table_[idx].occupied) {
            if (table_[idx].data.first == key) {
                table_[idx].data.~Pair<K, V>();
                table_[idx].occupied = false;
                --size_;
                return;
            }
            idx = (idx + 1) % capacity_;
        }
    }

    void erase(iterator iter) {
        if (iter != end()) {
            iter.ptr->data.~Pair<K, V>();
            iter.ptr->occupied = false;
            --size_;
            ++iter; // Move iterator to next valid position
        }
    }
    iterator begin() { return iterator(table_, table_ + capacity_); }
    iterator end()   { return iterator(table_ + capacity_, table_ + capacity_); }

    size_t size() const { return size_; }

private:
    Entry* table_;
    size_t capacity_;
    size_t size_;
};

#endif
