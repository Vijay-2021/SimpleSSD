#ifndef __RISCV_PAIR__
#define __RISCV_PAIR__

#include "memory.h"
#include "utils.h"
#include "move.hh"

template<typename T1, typename T2>
class Pair {
    public:
        T1 first;
        T2 second;
        
        Pair(T1 _first, T2 _second) : first(_first), second(_second) {};
        
        Pair(const Pair<T1, T2>& rhs) : first(rhs.first), second(rhs.second) {
        }
        
        Pair(Pair<T1, T2> && rhs) : first(move(rhs.first)), second(move(rhs.second)) {
            
        }

        bool operator==(const Pair<T1, T2>& rhs) const {
            return (rhs.first == first && rhs.second == second);
        }

        Pair<T1, T2>& operator=(const Pair<T1, T2>& rhs) {
            if (&rhs != this) {
                printf("move constructor called\n");
                first = rhs.first;
                second = rhs.second;
            }
            return *this;
        }

        Pair<T1, T2>& operator=(Pair<T1, T2>&& rhs) {
            if (&rhs != this) {
                first = move(rhs.first);
                second = move(rhs.second);
            }
            return *this;
        } 
        
}; 

#endif