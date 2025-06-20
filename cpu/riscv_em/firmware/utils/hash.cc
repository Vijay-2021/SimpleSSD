#include "hash.hh"

template<>
size_t hash(const uint32_t &key) const {
    uint64_t x = key; 
    x = ~x + (x << 15);
    x = x ^ (x >> 12);
    x = x + (x << 2);
    x = x ^ (x >> 4);
    x = x * 2057;
    x = x ^ (x >> 16);
    return x;
}

template<>
size_t hash(const uint64_t &key) const {
    uint64_t x = key;
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    x ^= x >> 31;
    return x;
}

template<> 
size_t hash(const int &key) const {
    return hash(static_cast<uint32_t>(key));
}