#pragma once
#include <cstdint>
#include <iostream>
#include <string>

typedef uint64_t Address; 
typedef uint64_t BinaryInsn;

using namespace std;

// this class should manage the binary and read from a location(marked by PC)
class FetchUnit {
    public: 
        FetchUnit(string binary_name);
        ~FetchUnit();

        virtual BinaryInsn tick();
    private:
        Address last_pc;
        Address next_pc;
        char* mem_region;

};