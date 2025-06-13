#ifndef FILE_HELPER_H
#define FILE_HELPER_H

#include <stdint.h>

namespace SimpleSSD {

namespace CPU {

namespace RISCV {

long int get_file_size(char *file_name);
long int write_mem_from_file(char *file_name, uint8_t *memory, long int mem_size);

} // namespace RISCV

} // namespace CPU

} // namespace SimpleSSD


#endif /* FILE_HELPER_H */
