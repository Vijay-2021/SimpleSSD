#include "fetch_unit.hh"

FetchUnit::FetchUnit(string input_path) {
    char* stuff = mmap(input_path );
}

FetchUnit::~FetchUnit(){

}

BinaryInsn tick() {
    if (PC >= 0 && PC <= BinarySize) {
        return stuff[PC];
        
    } else {
        perror("Invalid PC");
        exit(1);
    }
}

