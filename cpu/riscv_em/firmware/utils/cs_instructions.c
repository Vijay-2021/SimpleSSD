#include "cs_instructions.h"
#include "utils.h"

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
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, FUNC3_PHYSADDR, FUNC7_INSTR_PREAD, 5, 6, 7);
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
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, FUNC3_PHYSADDR, FUNC7_INSTR_PWRITE, 5, 6, 7);
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
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, FUNC3_PHYSADDR, FUNC7_INSTR_PERASE, 5, 6, 7);
  COMPILER_BARRIER();
}

void lread(uint64_t rd, uint64_t rs1, uint64_t rs2) {
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(rd), "r"(rs1), "r"(rs2)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, FUNC3_LOGADDR, FUNC7_INSTR_LREAD, 5, 6, 7);
  COMPILER_BARRIER();
}

void lwrite(uint64_t rd, uint64_t rs1, uint64_t rs2) {
  
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(rd), "r"(rs1), "r"(rs2)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, FUNC3_LOGADDR, FUNC7_INSTR_LWRITE, 5, 6, 7);
  COMPILER_BARRIER();
}

void ltrim(uint64_t rd, uint64_t rs1, uint64_t rs2) {
  
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(rd), "r"(rs1), "r"(rs2)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, FUNC3_LOGADDR, FUNC7_INSTR_LTRIM, 5, 6, 7);
  COMPILER_BARRIER();
}

void read_buffer(uint64_t rd, uint64_t rs1) {
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(rd), "r"(rs1), "r"(0)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, FUNC3_SOC_INTERFACE, FUNC7_READBUFF, 5, 6, 7);
  COMPILER_BARRIER();
}

void start_sim() {
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(0), "r"(0), "r"(0)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, FUNC3_SOC_INTERFACE, FUNC7_STARTSIM, 5, 6, 7);
  COMPILER_BARRIER();
}

void stop_sim() {
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(0), "r"(0), "r"(0)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, FUNC3_SOC_INTERFACE, FUNC7_STOPSIM, 5, 6, 7);
  COMPILER_BARRIER();
}

void next_tick(uint64_t rs1) {
  COMPILER_BARRIER();
  __asm__ volatile (
      "mv t0, %0\n\t"
      "mv t1, %1\n\t"
      "mv t2, %2\n\t"
      :
      : "r"(0), "r"(rs1), "r"(0)
      : "t0", "t1", "t2"
  ); 
  EMIT_CUSTOM_INSTR(CUSTOM_OPCODE, FUNC3_SOC_INTERFACE, FUNC7_NEXTSIMTICK, 5, 6, 7);
  COMPILER_BARRIER();
}
// we want to write programs that do this kind of thing

// should add some support for open/close/read/write
// should also add some support for userspace structs