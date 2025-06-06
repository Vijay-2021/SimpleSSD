#include "cs_instructions.h"

void pread(uint32_t rd, uint32_t rs1, uint32_t rs2) {
  
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(rd), "r"(rs1), "r"(rs2)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, READ_FUNCT3, READ_FUNCT7, 5, 6, 7);
  COMPILER_BARRIER();
}
void pwrite(uint32_t rd, uint32_t rs1, uint32_t rs2) {
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(rd), "r"(rs1), "r"(rs2)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, WRITE_FUNCT3, WRITE_FUNCT7, 5, 6, 7);
  COMPILER_BARRIER();
}

void perase(uint32_t rd, uint32_t rs1, uint32_t rs2) {
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(rd), "r"(rs1), "r"(rs2)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, ERASE_FUNCT3, ERASE_FUNCT7, 5, 6, 7);
  COMPILER_BARRIER();
}

// we want to write programs that do this kind of thing

// should add some support for open/close/read/write
// should also add some support for userspace structs