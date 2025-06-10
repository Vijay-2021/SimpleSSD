#include "cs_instructions.h"

void pread(uint64_t rd, uint64_t rs1, uint64_t rs2) {
  
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(rd), "r"(rs1), "r"(rs2)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, PAL_FUNCT3, PREAD_FUNCT7, 5, 6, 7);
  COMPILER_BARRIER();
}

void pwrite(uint64_t rd, uint64_t rs1, uint64_t rs2) {
  
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(rd), "r"(rs1), "r"(rs2)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, PAL_FUNCT3, PWRITE_FUNCT7, 5, 6, 7);
  COMPILER_BARRIER();
}

void perase(uint64_t rd, uint64_t rs1, uint64_t rs2) {
  
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(rd), "r"(rs1), "r"(rs2)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, PAL_FUNCT3, PERASE_FUNCT7, 5, 6, 7);
  COMPILER_BARRIER();
}

void fread(uint64_t rd, uint64_t rs1, uint64_t rs2) {
  
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(rd), "r"(rs1), "r"(rs2)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, FTL_FUNCT3, FREAD_FUNCT7, 5, 6, 7);
  COMPILER_BARRIER();
}

void fwrite(uint64_t rd, uint64_t rs1, uint64_t rs2) {
  
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(rd), "r"(rs1), "r"(rs2)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, FTL_FUNCT3, FWRITE_FUNCT7, 5, 6, 7);
  COMPILER_BARRIER();
}

void ftrim(uint64_t rd, uint64_t rs1, uint64_t rs2) {
  
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(rd), "r"(rs1), "r"(rs2)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, FTL_FUNCT3, FTRIM_FUNCT7, 5, 6, 7);
  COMPILER_BARRIER();
}

// we want to write programs that do this kind of thing

// should add some support for open/close/read/write
// should also add some support for userspace structs