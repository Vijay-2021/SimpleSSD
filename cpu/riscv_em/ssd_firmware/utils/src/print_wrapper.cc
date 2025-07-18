#include "print_wrapper.hh"
#include <stdarg.h>

PrintWrapper::PrintWrapper() {
    // Constructor logic if needed
}
PrintWrapper::~PrintWrapper() {
    // Destructor logic if needed
}

void PrintWrapper::printf_external(const char *format, ...) {
    va_list args;
    va_start(args, format);
    _printf(format, (char**)args);
    va_end(args);
}