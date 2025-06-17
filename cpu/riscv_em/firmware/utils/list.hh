#ifndef __RISCV__LIST__
#define __RISCV__LIST__

#include <stdint.h>
#include <stddef.h>

#include "memory.h"


template<typename T>
class List {
    private:
        struct Node {
            struct Node* next;
            Node* prev;
            T data;
        };
        
        size_t length_;
        Node* sentinel;
    public:
        class iterator {
            friend class List;
            Node* cur;
            public:
                iterator(Node* p) : cur(p) {}
                T& operator*() const { return cur->data; }
                iterator& operator++() { cur = cur->next; return *this; }
                iterator& operator--() { cur = cur->prev; return *this; }
                bool operator!=(const iterator& o) const { return cur != o.cur; }
                bool operator==(const iterator& o) const { return cur == o.cur; }
        };

        iterator begin() { return iterator(sentinel->next); }
        iterator end() { return iterator(sentinel); }
        iterator cbegin() const {return iterator(sentinel->next); }
        iterator cend() const {return iterator(sentinel); }

    public:
        List() {
            sentinel = (Node*) malloc(sizeof(Node));
            sentinel->next = sentinel;
            sentinel->prev = sentinel;
            length_ = 0;
        }
        List(size_t elems) {
            sentinel = (Node*) malloc(sizeof(Node));
            sentinel->next = sentinel;
            sentinel->prev = sentinel;
            length_ = 0;
            while (length_ < elems) {
                emplace_back(T());
                length_++;
            }
        }
        ~List() { clear(); free(sentinel); }

        void clear() {
            Node* cur = sentinel->next;
            while (cur != sentinel) {
                Node* nxt = cur->next;
                free(cur);
                cur = nxt;
            }
            sentinel->next = sentinel->prev = sentinel;
            length_ = 0;
        }

        void erase(iterator iter) {
            if (iter != end()) {
                Node* cur = iter.cur;
                cur->next->prev = cur->prev;
                cur->prev->next = cur->next;
                free(cur);
                length_--;
            } 
        }

        void emplace(iterator iter, const T& data) {
            Node* n = (Node *) malloc(sizeof(Node));
            Node* cur = iter.cur;
            n->data = data;
            n->next = cur;
            n->prev = cur->prev;
            cur->prev->next = n;
            cur->prev = n;
            length_++;
        }

        void emplace_back(const T& data) {
            emplace(end(), data);
        }

        bool empty() const { return sentinel->next == sentinel; }
        const size_t length() { return length_; }
};

#endif