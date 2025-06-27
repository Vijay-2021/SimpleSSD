#ifndef RISCV_INSTR_H
#define RISCV_INSTR_H

#define MAX_INSTR_OPCODE      0x74  // highest INSTR_ value used (this gets used as array size so one more is added)
#define MAX_FUNC3_VALUE       0x8   // FUNC3 is 3 bits: 0b111
#define MAX_FUNC5_VALUE       0x1D  // FUNC5_INSTR_AMO_MAXU = 0x1C
#define MAX_FUNC6_VALUE       0x11  // FUNC6_INSTR_SRAI = 0x10
#define MAX_FUNC7_VALUE       0x21  // FUNC7_INSTR_SUB, etc.

/* R-Type Instructions */
#define INSTR_ADD_SUB_SLL_SLT_SLTU_XOR_SRL_SRA_OR_AND_MUL_MULH_MULHSU_MULHU_DIV_DIVU_REM_REMU 0x33
    #define FUNC3_INSTR_ADD_SUB_MUL 0x0
        #define FUNC7_INSTR_ADD 0x00
        #define FUNC7_INSTR_MUL 0x01
        #define FUNC7_INSTR_SUB 0x20
    #define FUNC3_INSTR_SLL_MULH 0x1
        #define FUNC7_INSTR_SLL 0x00
        #define FUNC7_INSTR_MULH 0x01
    #define FUNC3_INSTR_SLT_MULHSU 0x2
        #define FUNC7_INSTR_SLT 0x00
        #define FUNC7_INSTR_MULHSU 0x01
    #define FUNC3_INSTR_SLTU_MULHU 0x3
        #define FUNC7_INSTR_SLTU 0x00
        #define FUNC7_INSTR_MULHU 0x01
    #define FUNC3_INSTR_XOR_DIV 0x4
        #define FUNC7_INSTR_XOR 0x00
        #define FUNC7_INSTR_DIV 0x01
    #define FUNC3_INSTR_SRL_SRA_DIVU 0x5
        #define FUNC7_INSTR_SRL 0x00
        #define FUNC7_INSTR_SRA 0x20
        #define FUNC7_INSTR_DIVU 0x01
    #define FUNC3_INSTR_OR_REM 0x6
        #define FUNC7_INSTR_OR 0x00
        #define FUNC7_INSTR_REM 0x01
    #define FUNC3_INSTR_AND_REMU 0x7
        #define FUNC7_INSTR_AND 0x00
        #define FUNC7_INSTR_REMU 0x01

/* I-Type Instructions */
#define INSTR_JALR 0x67
    #define FUNC3_INSTR_JALR    0x0

#define INSTR_ADDI_SLTI_SLTIU_XORI_ORI_ANDI_SLLI_SRLI_SRAI 0x13
    #define FUNC3_INSTR_ADDI    0x0
    #define FUNC3_INSTR_SLTI    0x2
    #define FUNC3_INSTR_SLTIU 0x3
    #define FUNC3_INSTR_XORI    0x4
    #define FUNC3_INSTR_ORI     0x6
    #define FUNC3_INSTR_ANDI    0x7
    #define FUNC3_INSTR_SLLI    0x1
    #define FUNC3_INSTR_SRLI_SRAI  0x5
        #define FUNC7_INSTR_SRLI 0x0
        #define FUNC7_INSTR_SRAI 0x20
        #define FUNC6_INSTR_SRLI 0x0
        #define FUNC6_INSTR_SRAI 0x10

#define INSTR_LB_LH_LW_LBU_LHU_LWU_LD 0x03
    #define FUNC3_INSTR_LB 0x0
    #define FUNC3_INSTR_LH 0x1
    #define FUNC3_INSTR_LW 0x2
    #define FUNC3_INSTR_LBU 0x4
    #define FUNC3_INSTR_LHU 0x5
    #define FUNC3_INSTR_LWU 0x6
    #define FUNC3_INSTR_LD 0x3

/* S-Type Instructions */
#define INSTR_SB_SH_SW_SD 0x23
    #define FUNC3_INSTR_SB 0x0
    #define FUNC3_INSTR_SH 0x1
    #define FUNC3_INSTR_SW 0x2
    #define FUNC3_INSTR_SD 0x3

/* B-Type Instructions */
#define INSTR_BEQ_BNE_BLT_BGE_BLTU_BGEU 0x63
    #define FUNC3_INSTR_BEQ 0x0
    #define FUNC3_INSTR_BNE 0x1
    #define FUNC3_INSTR_BLT 0x4
    #define FUNC3_INSTR_BGE 0x5
    #define FUNC3_INSTR_BLTU 0x6
    #define FUNC3_INSTR_BGEU 0x7

/* U-Type Instructions */
#define INSTR_LUI 0x37   /* LOAD UPPER IMMEDIATE INTO DESTINATION REGISTER */
#define INSTR_AUIPC 0x17 /* ADD UPPER IMMEDIATE TO PROGRAM COUNTER */

/* J-Type Instructions */
#define INSTR_JAL 0x6F   /* JUMP and Link */

/* System level instructions */
#define INSTR_FENCE_FENCE_I 0x0F
    #define FUNC3_INSTR_FENCE 0x0
    #define FUNC3_INSTR_FENCE_I 0x1

#define INSTR_ECALL_EBREAK_MRET_SRET_URET_WFI_CSRRW_CSRRS_CSRRC_CSRRWI_CSRRSI_CSRRCI_SFENCEVMA 0x73
    #define FUNC3_INSTR_ECALL_EBREAK_MRET_SRET_URET_WFI_SFENCEVMA 0x0
        /* FUNC12 will be splitted in FUNC7 and FUNC5 */
        #define FUNC7_INSTR_ECALL_EBREAK_URET 0x0
            #define FUNC5_INSTR_ECALL 0x0
            #define FUNC5_INSTR_EBREAK 0x1
            #define FUNC5_INSTR_URET 0x2
        #define FUNC7_INSTR_SRET_WFI 0x8
            #define FUNC5_INSTR_SRET 0x2
            #define FUNC5_INSTR_WFI 0x5
        #define FUNC7_INSTR_MRET 0x18
            #define FUNC5_INSTR_MRET 0x2
        #define FUNC7_INSTR_SFENCEVMA 0x9
    #define FUNC3_INSTR_CSRRW 0x1
    #define FUNC3_INSTR_CSRRS 0x2
    #define FUNC3_INSTR_CSRRC 0x3
    #define FUNC3_INSTR_CSRRWI 0x5
    #define FUNC3_INSTR_CSRRSI 0x6
    #define FUNC3_INSTR_CSRRCI 0x7

#define INSTR_ADDIW_SLLIW_SRLIW_SRAIW 0x1B
    #define FUNC3_INSTR_SLLIW 0x1
    #define FUNC3_INSTR_SRLIW_SRAIW 0x5
        #define FUNC7_INSTR_SRLIW 0x0
        #define FUNC7_INSTR_SRAIW 0x20
    #define FUNC3_INSTR_ADDIW 0x0

#define INSTR_ADDW_SUBW_SLLW_SRLW_SRAW_MULW_DIVW_DIVUW_REMW_REMUW 0x3B
    #define FUNC3_INSTR_ADDW_SUBW_MULW 0x0
        #define FUNC7_INSTR_ADDW 0x00
        #define FUNC7_INSTR_SUBW 0x20
        #define FUNC7_INSTR_MULW 0x01
    #define FUNC3_INSTR_SLLW 0x1
    #define FUNC3_INSTR_DIVW 0x4
    #define FUNC3_INSTR_SRLW_SRAW_DIVUW 0x5
        #define FUNC7_INSTR_SRLW 0x00
        #define FUNC7_INSTR_SRAW 0x20
        #define FUNC7_INSTR_DIVUW 0x01
    #define FUNC3_INSTR_REMW 0x6
    #define FUNC3_INSTR_REMUW 0x7

/* Atomic Instructions */
#define INSTR_AMO_W_D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU 0x2F
    #define FUNC3_INSTR_W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU 0x2
    #define FUNC3_INSTR_D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU 0x3
    #define INSTR_AMO_W_D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_SIZE 0x4
        #define FUNC5_INSTR_AMO_LR 0x2
        #define FUNC5_INSTR_AMO_SC 0x3
        #define FUNC5_INSTR_AMO_SWAP 0x1
        #define FUNC5_INSTR_AMO_ADD 0x0
        #define FUNC5_INSTR_AMO_XOR 0x4
        #define FUNC5_INSTR_AMO_AND 0xC
        #define FUNC5_INSTR_AMO_OR 0x8
        #define FUNC5_INSTR_AMO_MIN 0x10
        #define FUNC5_INSTR_AMO_MAX 0x14
        #define FUNC5_INSTR_AMO_MINU 0x18
        #define FUNC5_INSTR_AMO_MAXU 0x1C

#define INSTR_FLT_ARITH 0x53
    #define FLT_ADD 0x00
    #define FLT_SUB 0x01
    #define FLT_MUL 0x02 
    #define FLT_DIV 0x03 
    #define FLT_SQRT 0x0B
    #define FLT_SGNINJ 0x04
        #define FLT_SGNJ 0x00
        #define FLT_SGNJN 0x01
        #define FLT_SGNJX 0x02
    #define FLT_MAX_MIN 0x05
        #define FLT_MIN 0x00
        #define FLT_MAX 0x01
    #define FLT_CMP 0x14
        #define FLT_CMPEQ 0x02
        #define FLT_CMPLT 0x01
        #define FLT_CMPLE 0x00
    #define FLT_CLASS_FUNC7 0x1C
    #define FLT_CLASS_FUNC3 0x01
    #define FLT_CVT_TO_INT 0x60
        #define FLT_CVT_TO_SINT 0x00
        #define FLT_CVT_TO_UINT 0x01
        #define FLT_CVT_TO_LONG 0x02
        #define FLT_CVT_TO_ULONG 0x03
    #define FLT_CVT_FROM_INT 0x68
        #define FLT_CVT_FROM_SINT 0x00
        #define FLT_CVT_FROM_UINT 0x01
        #define FLT_CVT_FROM_LONG 0x02
        #define FLT_CVT_FROM_ULONG 0x03
    #define FLT_MV_FUNC3 0x00
        #define FLT_MV_TO_INT 0x70
        #define FLT_MV_FROM_INT 0x78

#define INSTR_FLT_LOAD 0x07
#define INSTR_FLT_STORE 0x27

#define INSTR_FMADD 0x43
#define INSTR_FMSUB 0x47
#define INSTR_FMNSUB 0x4B
#define INSTR_FMNADD 0x4F

// custom instructions for interfacing with the ssd simulator

#define CUSTOM_OPCODE 0x0B
    #define FUNC3_LOGADDR 0x0
        #define FUNC7_INSTR_LREAD 0x00
        #define FUNC7_INSTR_LWRITE 0x01
        #define FUNC7_INSTR_LTRIM 0x02
    #define FUNC3_PHYSADDR 0x1
        #define FUNC7_INSTR_PREAD 0x00
        #define FUNC7_INSTR_PWRITE 0x01
        #define FUNC7_INSTR_PERASE 0x02
    #define FUNC3_SOC_INTERFACE 0x2
        #define FUNC7_READBUFF 0x00
        #define FUNC7_STARTSIM 0x01 // stop continuos execution
        #define FUNC7_STOPSIM 0x02 // execute once every cycle
        #define FUNC7_NEXTSIMTICK 0x03 // sets the next tick
        #define FUNC7_PUTC 0x04
        #define FUNC7_WRITEBUFF 0x05 // write to the buffer
        
#endif /* RISCV_INSTR_H */
