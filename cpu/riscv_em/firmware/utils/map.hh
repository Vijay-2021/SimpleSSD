#ifndef __RISCV_MAP__
#define __RISCV_MAP__

#include "memory.h"
#include "utils.h"
#include "pair.hh"
#include "hash.hh"

class HashMap {
public:
    struct Entry {
        K key;
        V value;
        bool occupied;
    };

    class iterator {
        Entry* ptr;
        Entry* end;
    public:
        iterator(Entry* p, Entry* e) : ptr(p), end(e) {
            while (ptr != end && !ptr->occupied) ++ptr;
        }
        Pair<K, V> operator*() const { return Pair<K, V>(ptr->key, ptr->value); }
        iterator& operator++() {
            do { ++ptr; } while (ptr != end && !ptr->occupied);
            return *this;
        }
        bool operator!=(const iterator& other) const { return ptr != other.ptr; }
        bool operator==(const iterator& other) const { return ptr == other.ptr; }
    };

    HashMap(size_t cap) : capacity_(cap), size_(0) {
        table_ = (Entry*)malloc(sizeof(Entry) * capacity_);
        for (size_t i = 0; i < capacity_; i++) {
            table_[i].occupied = false;
        }
    }

    ~HashMap() { free(table_); }

    V& operator[](const K& key) {
        size_t idx = hash(key) % capacity_;
        while (table_[idx].occupied) {
            if (table_[idx].key == key)
                return table_[idx].value;
            idx = (idx + 1) % capacity_;
        }
        table_[idx].key = key;
        table_[idx].value = V();
        table_[idx].occupied = true;
        ++size_;
        return table_[idx].value;
    }

    bool count(const K& key) const {
        size_t idx = hash(key) % capacity_;
        while (table_[idx].occupied) {
            if (table_[idx].key == key)
                return true;
            idx = (idx + 1) % capacity_;
        }
        return false;
    }

    iterator begin() { return iterator(table_, table_ + capacity_); }
    iterator end()   { return iterator(table_ + capacity_, table_ + capacity_); }

    size_t size() const { return size_; }

private:
    Entry* table_;
    size_t capacity_;
    size_t size_;

    size_t hash(const K& key) const {
        // Plug in your real hash function here
        return int_hash(key);
    }
};

