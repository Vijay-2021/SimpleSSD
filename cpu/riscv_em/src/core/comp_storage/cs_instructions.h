#pragma once
#include <stdint.h>

#define COMPILER_BARRIER() { __asm__ __volatile__("" ::: "memory");}

#define CUSTOM_INSTR_ENCODE(funct7, rs2, rs1, funct3, rd, opcode) \
    ((((funct7) & 0x7F) << 25) | \
     (((rs2)   & 0x1F) << 20) | \
     (((rs1)   & 0x1F) << 15) | \
     (((funct3)& 0x7)  << 12) | \
     (((rd)    & 0x1F) << 7)  | \
     ((opcode) & 0x7F))

#define EMIT_CUSTOM_INSTR(opcode, funct3, funct7, rd, rs1, rs2) \
    __asm__ volatile (".word %0" :: "i"(CUSTOM_INSTR_ENCODE(funct7, rs2, rs1, funct3, rd, opcode)))

#define CUSTOM_OPCODE 0x0B
#define READ_FUNCT3 0x00
#define WRITE_FUNCT3 0x01
#define ERASE_FUNCT3 0x02

#define READ_FUNCT7 0x00
#define WRITE_FUNCT7 0x00
#define ERASE_FUNCT7 0x00

void pread(uint8_t rd, uint8_t rs1, uint8_t rs2) {
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
void pwrite(uint8_t rd, uint8_t rs1, uint8_t rs2) {
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

void perase(uint8_t rd, uint8_t rs1, uint8_t rs2) {
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