#ifndef __PRINT_WRAPPER_HH__
#define __PRINT_WRAPPER_HH__

#include "utils.h"

class PrintWrapper {
    public:
        PrintWrapper();
        ~PrintWrapper();

        void printf_external(const char *format, ...);

};

#endif