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

void pread(uint32_t rd, uint32_t rs1, uint32_t rs2);
void pwrite(uint32_t rd, uint32_t rs1, uint32_t rs2);
void perase(uint32_t rd, uint32_t rs1, uint32_t rs2);

// we want to write programs that do this kind of thing

// should add some support for open/close/read/write
// should also add some support for userspace structs