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
    #define FUNC3_FTL 0x0
        #define FUNC7_INSTR_FREAD 0x00
        #define FUNC7_INSTR_FWRITE 0x01
        #define FUNC7_INSTR_FTRIM 0x02
    #define FUNC3_PAL 0x1
        #define FUNC7_INSTR_PREAD 0x00
        #define FUNC7_INSTR_PWRITE 0x01
        #define FUNC7_INSTR_PERASE 0x02

void pread(uint64_t rd, uint64_t rs1, uint64_t rs2);
void pwrite(uint64_t rd, uint64_t rs1, uint64_t rs2);
void perase(uint64_t rd, uint64_t rs1, uint64_t rs2);

// we want to write programs that do this kind of thing

// should add some support for open/close/read/write
// should also add some support for userspace structs