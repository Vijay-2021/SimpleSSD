#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmath> 

#include "riscv_types.hh"
#include "riscv_helper.hh"
#include "riscv_instr.hh"
#include "rv_src/soc/riscv_soc.hh"
#include "rv_src/core/mmu/mmu.hh"
#include "core.hh"

#include "sim/simulator.hh"
#include "util/def.hh"
#include "util/bitset.hh"
#include "instruction_timings.hh"

// #define CORE_DEBUG
#ifdef CORE_DEBUG
    #define CORE_DBG(...) do{ printf( __VA_ARGS__ ); } while( 0 )
#else
    #define CORE_DBG(...) do{ } while ( 0 )
#endif

#define ADDR_MISALIGNED(addr) (addr & 0x3)

namespace SimpleSSD {

namespace CPU {

namespace RISCV {

/*
 * Functions for internal use
 */
static inline void prepare_sync_trap(Core *rv_core, rv_word_t cause, rv_word_t tval)
{
    if(!rv_core->sync_trap_pending)
    {
        rv_core->sync_trap_pending = 1;
        rv_core->sync_trap_cause = cause;
        rv_core->sync_trap_tval = tval;
    }
}

static inline privilege_level check_mprv_override(Core *rv_core, bus_access_type access_type)
{
    if(access_type == bus_instr_access)
        return rv_core->curr_priv_mode;

    int mprv = extractxlen(*rv_core->trap.m.regs[trap_reg_status], TRAP_XSTATUS_MPRV_BIT, 1);
    privilege_level ret_val = (privilege_level) extractxlen(*rv_core->trap.m.regs[trap_reg_status], TRAP_XSTATUS_MPP_BIT, 2);
    return mprv ? ret_val : rv_core->curr_priv_mode;
}

uint64_t pmp_checked_bus_access(void *priv, privilege_level priv_level, bus_access_type access_type, rv_word_t addr, void *value, uint8_t len)
{
    (void) priv_level;
    Core *rv_core = (Core *)priv;

    rv_word_t trap_cause = (access_type == bus_instr_access) ? trap_cause_instr_access_fault :
                              (access_type == bus_read_access) ? trap_cause_load_access_fault :
                              trap_cause_store_amo_access_fault;

    if(pmp_mem_check(&rv_core->pmp, priv_level, addr, len, access_type))
    {
        printf("PMP Violation!\n");
        prepare_sync_trap(rv_core, trap_cause, addr);
        return 0; // return 0 to indicate error
    }

    return rv_core->bus_access(rv_core->pSOC, priv_level, access_type, addr, value, len);
}

uint64_t mmu_checked_bus_access(void *priv, privilege_level priv_level, bus_access_type access_type, rv_word_t addr, void *value, uint8_t len)
{
    
    Core *rv_core = (Core *)priv;
    #ifdef USE_MMU
        (void) priv_level;
        privilege_level internal_priv_level = check_mprv_override(rv_core, access_type);
        rv_word_t trap_cause = (access_type == bus_instr_access) ? trap_cause_instr_page_fault :
                                (access_type == bus_read_access) ? trap_cause_load_page_fault :
                                trap_cause_store_amo_page_fault;
        mmu_ret mmu_ret_val = mmu_ok;

        uint8_t mxr = CHECK_BIT(*rv_core->trap.m.regs[trap_reg_status], TRAP_XSTATUS_MXR_BIT) ? 1 : 0;
        uint8_t sum = CHECK_BIT(*rv_core->trap.m.regs[trap_reg_status], TRAP_XSTATUS_SUM_BIT) ? 1 : 0;

        rv_word_t tmp = 0;
        memcpy(&tmp, value, len);
        uint64_t phys_addr = mmu_virt_to_phys(&rv_core->mmu, internal_priv_level, addr, access_type, mxr, sum, &mmu_ret_val, rv_core, tmp);

        if(mmu_ret_val != mmu_ok)
        {
            prepare_sync_trap(rv_core, trap_cause, addr);
            return 0; // return 0 to indicate error
        }
        #ifdef PMP_SUPPORT 
            return rv_core->mmu.bus_access(rv_core->mmu.priv, internal_priv_level, access_type, phys_addr, value, len);
        #else
            return rv_core->bus_access((void *)rv_core->pSOC, priv_level, access_type, addr, value, len);
        #endif
    #else
        #ifdef PMP_SUPPORT 
            return rv_core->mmu.bus_access(rv_core->mmu.priv, internal_priv_level, access_type, phys_addr, value, len);
        #else
            return rv_core->bus_access((void *)rv_core->pSOC, priv_level, access_type, addr, value, len);
        #endif
    #endif
}

/*
 * Implementations of the RISCV instructions
 */
static uint64_t instr_NOP(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    (void) rv_core;
    return 1; // 1 cycle
}

/* RISCV Instructions */
static uint64_t instr_LUI(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_core->reg_file[rv_core->rd] = (rv_core->immediate << 12);
    return LUI_CYCLE_COUNT;
}

static uint64_t instr_AUIPC(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_core->reg_file[rv_core->rd] = (rv_core->pc) + (rv_core->immediate << 12);
    return AUIPC_CYCLE_COUNT;
}

static uint64_t instr_JAL(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_core->reg_file[rv_core->rd] = rv_core->pc + 4;

    if(ADDR_MISALIGNED(rv_core->jump_offset))
    {
        die_msg("Addr misaligned!\n");
        prepare_sync_trap(rv_core, trap_cause_instr_addr_misalign, 0);
        return 0;
    }

    rv_core->next_pc = rv_core->pc + rv_core->jump_offset;
    return JAL_CYCLE_COUNT;
}

static uint64_t instr_JALR(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_word_t curr_pc = rv_core->pc + 4;
    rv_core->jump_offset = SIGNEX_BIT_11(rv_core->immediate);

    rv_core->next_pc = (rv_core->reg_file[rv_core->rs1] + rv_core->jump_offset);
    rv_core->next_pc &= ~(1<<0);

    if(ADDR_MISALIGNED(rv_core->next_pc))
    {
        die_msg("Addr misaligned!\n");
        prepare_sync_trap(rv_core, trap_cause_instr_addr_misalign, 0);
        return 0;
    }

    rv_core->reg_file[rv_core->rd] = curr_pc;
    return JALR_CYCLE_COUNT;
}

static uint64_t instr_BEQ(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    if(rv_core->reg_file[rv_core->rs1] == rv_core->reg_file[rv_core->rs2])
    {
        if(ADDR_MISALIGNED(rv_core->jump_offset))
        {
            die_msg("Addr misaligned!\n");
            prepare_sync_trap(rv_core, trap_cause_instr_addr_misalign, 0);
            return 0;
        }

        rv_core->next_pc = rv_core->pc + rv_core->jump_offset;
    }
    return BEQ_CYCLE_COUNT;
}

static uint64_t instr_BNE(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    if(rv_core->reg_file[rv_core->rs1] != rv_core->reg_file[rv_core->rs2])
    {
        if(ADDR_MISALIGNED(rv_core->jump_offset))
        {
            die_msg("Addr misaligned!\n");
            prepare_sync_trap(rv_core, trap_cause_instr_addr_misalign, 0);
            return 0;
        }

        rv_core->next_pc = rv_core->pc + rv_core->jump_offset;
    }
    return BNE_CYCLE_COUNT;
}

static uint64_t instr_BLT(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_sword_t signed_rs = rv_core->reg_file[rv_core->rs1];
    rv_sword_t signed_rs2 = rv_core->reg_file[rv_core->rs2];

    if(signed_rs < signed_rs2)
    {
        if(ADDR_MISALIGNED(rv_core->jump_offset))
        {
            die_msg("Addr misaligned!\n");
            prepare_sync_trap(rv_core, trap_cause_instr_addr_misalign, 0);
            return 0;
        }

        rv_core->next_pc = rv_core->pc + rv_core->jump_offset;
    }
    return BLT_CYCLE_COUNT;
}

static uint64_t instr_BGE(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_sword_t signed_rs = rv_core->reg_file[rv_core->rs1];
    rv_sword_t signed_rs2 = rv_core->reg_file[rv_core->rs2];

    if(signed_rs >= signed_rs2)
    {
        if(ADDR_MISALIGNED(rv_core->jump_offset))
        {
            die_msg("Addr misaligned!\n");
            prepare_sync_trap(rv_core, trap_cause_instr_addr_misalign, 0);
            return 0;
        }

        rv_core->next_pc = rv_core->pc + rv_core->jump_offset;
    }
    return BGE_CYCLE_COUNT;
}

static uint64_t instr_BLTU(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    if(rv_core->reg_file[rv_core->rs1] < rv_core->reg_file[rv_core->rs2])
    {
        if(ADDR_MISALIGNED(rv_core->jump_offset))
        {
            die_msg("Addr misaligned!\n");
            prepare_sync_trap(rv_core, trap_cause_instr_addr_misalign, 0);
            return 0;
        }

        rv_core->next_pc = rv_core->pc + rv_core->jump_offset;
    }
    return BLTU_CYCLE_COUNT;
}

static uint64_t instr_BGEU(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    if(rv_core->reg_file[rv_core->rs1] >= rv_core->reg_file[rv_core->rs2])
    {
        if(ADDR_MISALIGNED(rv_core->jump_offset))
        {
            die_msg("Addr misaligned!\n");
            prepare_sync_trap(rv_core, trap_cause_instr_addr_misalign, 0);
            return 0;
        }

        rv_core->next_pc = rv_core->pc + rv_core->jump_offset;
    }
    return BGEU_CYCLE_COUNT;
}

static uint64_t instr_ADDI(Core *rv_core)
{
    CORE_DBG("%s: %x "PRINTF_FMT"\n", __func__, rv_core->instruction, rv_core->pc);
    rv_sword_t signed_immediate = SIGNEX_BIT_11(rv_core->immediate);
    rv_sword_t signed_rs_val = rv_core->reg_file[rv_core->rs1];
    CORE_DBG("%s: "PRINTF_FMT" "PRINTF_FMT" "PRINTF_FMT" %x\n", __func__, rv_core->reg_file[rv_core->rs1], signed_rs_val, signed_immediate, rv_core->rs1);
    rv_core->reg_file[rv_core->rd] = (signed_immediate + signed_rs_val);
    return ADDI_CYCLE_COUNT;
}

static uint64_t instr_SLTI(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_sword_t signed_immediate = SIGNEX_BIT_11(rv_core->immediate);
    rv_sword_t signed_rs_val = rv_core->reg_file[rv_core->rs1];

    if(signed_rs_val < signed_immediate)
        rv_core->reg_file[rv_core->rd] = 1;
    else
        rv_core->reg_file[rv_core->rd] = 0;
    return SLTI_CYCLE_COUNT;
}

static uint64_t instr_SLTIU(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_word_t unsigned_immediate = SIGNEX_BIT_11(rv_core->immediate);
    rv_word_t unsigned_rs_val = rv_core->reg_file[rv_core->rs1];

    if(unsigned_rs_val < unsigned_immediate)
        rv_core->reg_file[rv_core->rd] = 1;
    else
        rv_core->reg_file[rv_core->rd] = 0;
    return SLTIU_CYCLE_COUNT;
}

static uint64_t instr_XORI(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_sword_t signed_immediate = SIGNEX_BIT_11(rv_core->immediate);
    rv_core->immediate = signed_immediate;

    if(signed_immediate == -1)
        rv_core->reg_file[rv_core->rd] = rv_core->reg_file[rv_core->rs1] ^ -1;
    else
        rv_core->reg_file[rv_core->rd] = rv_core->reg_file[rv_core->rs1] ^ rv_core->immediate;
    return XORI_CYCLE_COUNT;
}

static uint64_t instr_ORI(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_core->immediate = SIGNEX_BIT_11(rv_core->immediate);
    rv_core->reg_file[rv_core->rd] = rv_core->reg_file[rv_core->rs1] | rv_core->immediate;
    return ORI_CYCLE_COUNT;
}

static uint64_t instr_ANDI(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_core->immediate = SIGNEX_BIT_11(rv_core->immediate);
    rv_core->reg_file[rv_core->rd] = rv_core->reg_file[rv_core->rs1] & rv_core->immediate;
    return ANDI_CYCLE_COUNT;
}

static uint64_t instr_SLLI(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_core->reg_file[rv_core->rd] = (rv_core->reg_file[rv_core->rs1] << (rv_core->immediate & SHIFT_OP_MASK));
    return SLLI_CYCLE_COUNT;
}

static uint64_t instr_SRAI(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_sword_t rs_val = rv_core->reg_file[rv_core->rs1];

    /* a right shift on signed ints seem to be always arithmetic */
    rs_val = rs_val >> (rv_core->immediate & SHIFT_OP_MASK);
    rv_core->reg_file[rv_core->rd] = rs_val;
    return SRAI_CYCLE_COUNT;
}

static uint64_t instr_SRLI(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_core->reg_file[rv_core->rd] = (rv_core->reg_file[rv_core->rs1] >> (rv_core->immediate & SHIFT_OP_MASK));
    return SRLI_CYCLE_COUNT;
}

static uint64_t instr_ADD(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    CORE_DBG("%s: "PRINTF_FMT" %x\n", __func__, rv_core->reg_file[rv_core->rs1], rv_core->rs1);
    rv_core->reg_file[rv_core->rd] = rv_core->reg_file[rv_core->rs1] + rv_core->reg_file[rv_core->rs2];
    return ADD_CYCLE_COUNT;
}

static uint64_t instr_SUB(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_core->reg_file[rv_core->rd] = rv_core->reg_file[rv_core->rs1] - rv_core->reg_file[rv_core->rs2];
    return SUB_CYCLE_COUNT;
}

static uint64_t instr_SLL(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_core->reg_file[rv_core->rd] = rv_core->reg_file[rv_core->rs1] << (rv_core->reg_file[rv_core->rs2] & SHIFT_OP_MASK);
    return SLL_CYCLE_COUNT;
}

static uint64_t instr_SLT(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_sword_t signed_rs = rv_core->reg_file[rv_core->rs1];
    rv_sword_t signed_rs2 = rv_core->reg_file[rv_core->rs2];

    if(signed_rs < signed_rs2) rv_core->reg_file[rv_core->rd] = 1;
    else rv_core->reg_file[rv_core->rd] = 0;
    return SLT_CYCLE_COUNT;
}

static uint64_t instr_SLTU(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    if(rv_core->rs1 == 0)
    {
        if(rv_core->reg_file[rv_core->rs2])
            rv_core->reg_file[rv_core->rd] = 1;
        else
            rv_core->reg_file[rv_core->rd] = 0;
    }
    else
    {
        if(rv_core->reg_file[rv_core->rs1] < rv_core->reg_file[rv_core->rs2])
            rv_core->reg_file[rv_core->rd] = 1;
        else
            rv_core->reg_file[rv_core->rd] = 0;
    }
    return SLTU_CYCLE_COUNT;
}

static uint64_t instr_XOR(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_core->reg_file[rv_core->rd] = rv_core->reg_file[rv_core->rs1] ^ rv_core->reg_file[rv_core->rs2];
    return XOR_CYCLE_COUNT;
}

static uint64_t instr_SRL(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_core->reg_file[rv_core->rd] = rv_core->reg_file[rv_core->rs1] >> (rv_core->reg_file[rv_core->rs2] & SHIFT_OP_MASK);
    return SRL_CYCLE_COUNT;
}

static uint64_t instr_OR(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_core->reg_file[rv_core->rd] = rv_core->reg_file[rv_core->rs1] | (rv_core->reg_file[rv_core->rs2]);
    return OR_CYCLE_COUNT;
}

static uint64_t instr_AND(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_core->reg_file[rv_core->rd] = rv_core->reg_file[rv_core->rs1] & (rv_core->reg_file[rv_core->rs2]);
    return AND_CYCLE_COUNT;
}

static uint64_t instr_SRA(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_sword_t signed_rs = rv_core->reg_file[rv_core->rs1];
    rv_core->reg_file[rv_core->rd] = signed_rs >> (rv_core->reg_file[rv_core->rs2] & SHIFT_OP_MASK);
    return SRA_CYCLE_COUNT;
}

static uint64_t instr_LB(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    uint8_t tmp_load_val = 0;
    rv_sword_t signed_offset = SIGNEX_BIT_11(rv_core->immediate);
    rv_word_t address = rv_core->reg_file[rv_core->rs1] + signed_offset;
    uint64_t ret_val = mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_read_access, address, &tmp_load_val, 1);
    if(ret_val != 0)
        rv_core->reg_file[rv_core->rd] = SIGNEX_BIT_7(tmp_load_val);
    return ret_val;
    
}

static uint64_t instr_LH(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    uint16_t tmp_load_val = 0;
    rv_sword_t signed_offset = SIGNEX_BIT_11(rv_core->immediate);
    rv_word_t address = rv_core->reg_file[rv_core->rs1] + signed_offset;
    uint64_t ret_val = mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_read_access, address, &tmp_load_val, 2);
    if (ret_val != 0)
        rv_core->reg_file[rv_core->rd] = SIGNEX_BIT_15(tmp_load_val);
    return ret_val;
}

static uint64_t instr_LW(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    int32_t tmp_load_val = 0;
    rv_sword_t signed_offset = SIGNEX_BIT_11(rv_core->immediate);
    rv_word_t address = rv_core->reg_file[rv_core->rs1] + signed_offset;
    uint64_t ret_val = mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_read_access, address, &tmp_load_val, 4);
    if (ret_val != 0)
        rv_core->reg_file[rv_core->rd] = tmp_load_val;
    return ret_val + LW_EXTRA_CYCLE_COUNT;
}

static uint64_t instr_LBU(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    uint8_t tmp_load_val = 0;
    rv_sword_t signed_offset = SIGNEX_BIT_11(rv_core->immediate);
    rv_word_t address = rv_core->reg_file[rv_core->rs1] + signed_offset;
    uint64_t ret_val = mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_read_access, address, &tmp_load_val, 1);
    if (ret_val != 0) 
        rv_core->reg_file[rv_core->rd] = tmp_load_val;
    return ret_val + LBU_EXTRA_CYCLE_COUNT;
}

static uint64_t instr_LHU(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    uint16_t tmp_load_val = 0;
    rv_sword_t signed_offset = SIGNEX_BIT_11(rv_core->immediate);
    rv_word_t address = rv_core->reg_file[rv_core->rs1] + signed_offset;
    uint64_t ret_val = mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_read_access, address, &tmp_load_val, 2);
    if (ret_val != 0)
        rv_core->reg_file[rv_core->rd] = tmp_load_val;
    return ret_val + LHU_EXTRA_CYCLE_COUNT;
}

static uint64_t instr_SB(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_sword_t signed_offset = SIGNEX_BIT_11(rv_core->immediate);
    rv_word_t address = rv_core->reg_file[rv_core->rs1] + signed_offset;
    uint8_t value_to_write = (uint8_t)rv_core->reg_file[rv_core->rs2];
    return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &value_to_write, 1) + SB_EXTRA_CYCLE_COUNT;
}

static uint64_t instr_SH(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_sword_t signed_offset = SIGNEX_BIT_11(rv_core->immediate);
    rv_word_t address = rv_core->reg_file[rv_core->rs1] + signed_offset;
    uint16_t value_to_write = (uint16_t)rv_core->reg_file[rv_core->rs2];
    return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &value_to_write, 2) + SH_EXTRA_CYCLE_COUNT;
}

static uint64_t instr_SW(Core *rv_core)
{
    CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
    rv_sword_t signed_offset = SIGNEX_BIT_11(rv_core->immediate);
    rv_word_t address = rv_core->reg_file[rv_core->rs1] + signed_offset;
    rv_word_t value_to_write = (rv_word_t)rv_core->reg_file[rv_core->rs2];
    return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &value_to_write, 4) + SW_EXTRA_CYCLE_COUNT;
}

#ifdef RV64
    static uint64_t instr_LWU(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        uint32_t tmp_load_val = 0;
        rv_word_t unsigned_offset = SIGNEX_BIT_11(rv_core->immediate);
        rv_word_t address = rv_core->reg_file[rv_core->rs1] + unsigned_offset;
        uint64_t ret_val = mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_read_access, address, &tmp_load_val, 4);
        if (ret_val != 0)
            rv_core->reg_file[rv_core->rd] = tmp_load_val;
        return ret_val + LWU_EXTRA_CYCLE_COUNT;
    }

    static uint64_t instr_LD(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t tmp_load_val = 0;
        rv_sword_t signed_offset = SIGNEX_BIT_11(rv_core->immediate);
        rv_sword_t address = rv_core->reg_file[rv_core->rs1] + signed_offset;
        uint64_t ret_val = mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_read_access, address, &tmp_load_val, 8);
        if (ret_val != 0)
            rv_core->reg_file[rv_core->rd] = tmp_load_val;
        return ret_val + LD_EXTRA_CYCLE_COUNT;
    }

    static uint64_t instr_SD(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_sword_t signed_offset = SIGNEX_BIT_11(rv_core->immediate);
        rv_word_t address = rv_core->reg_file[rv_core->rs1] + signed_offset;
        rv_word_t value_to_write = (rv_word_t)rv_core->reg_file[rv_core->rs2];
        return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &value_to_write, 8) + SD_EXTRA_CYCLE_COUNT;
    }

    static uint64_t instr_SRAIW(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        int32_t signed_rs_val = rv_core->reg_file[rv_core->rs1];
        rv_core->reg_file[rv_core->rd] = (signed_rs_val >> (rv_core->immediate & 0x1F));
        return SRAIW_CYCLE_COUNT;
        
    }

    static uint64_t instr_ADDIW(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        int32_t signed_immediate = SIGNEX_BIT_11(rv_core->immediate);
        int32_t signed_rs_val = rv_core->reg_file[rv_core->rs1];
        rv_core->reg_file[rv_core->rd] = (signed_rs_val + signed_immediate);
        return ADDIW_CYCLE_COUNT;
    }

    static uint64_t instr_SLLIW(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        int32_t signed_tmp32 = (rv_core->reg_file[rv_core->rs1] << (rv_core->immediate & 0x1F)) & 0xFFFFFFFF;
        rv_core->reg_file[rv_core->rd] = (rv_sword_t)signed_tmp32;
        return SLLIW_CYCLE_COUNT;
    }

    static uint64_t instr_SRLIW(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        uint32_t unsigned_rs_val = rv_core->reg_file[rv_core->rs1];
        int32_t signed_tmp32 = (unsigned_rs_val >> (rv_core->immediate & 0x1F));
        rv_core->reg_file[rv_core->rd] = (rv_sword_t)signed_tmp32;
        return SRLIW_CYCLE_COUNT;
    }

    static uint64_t instr_SRLW(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        uint32_t rs1_val = rv_core->reg_file[rv_core->rs1];
        uint32_t rs2_val = (rv_core->reg_file[rv_core->rs2] & 0x1F);
        int32_t signed_tmp32 = rs1_val >> rs2_val;
        rv_core->reg_file[rv_core->rd] = (rv_sword_t)signed_tmp32;
        return SRLW_CYCLE_COUNT;
    }

    static uint64_t instr_SRAW(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        int32_t rs1_val_signed = rv_core->reg_file[rv_core->rs1];
        uint32_t rs2_val = (rv_core->reg_file[rv_core->rs2] & 0x1F);
        int32_t signed_tmp32 = rs1_val_signed >> rs2_val;
        rv_core->reg_file[rv_core->rd] = (rv_sword_t)signed_tmp32;
        return SRAW_CYCLE_COUNT;
    }

    static uint64_t instr_SLLW(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        uint32_t rs1_val = rv_core->reg_file[rv_core->rs1];
        uint32_t rs2_val = (rv_core->reg_file[rv_core->rs2] & 0x1F);
        int32_t signed_tmp32 = rs1_val << rs2_val;
        rv_core->reg_file[rv_core->rd] = (rv_sword_t)signed_tmp32;
        return SLLW_CYCLE_COUNT;
    }

    static uint64_t instr_ADDW(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        uint32_t rs1_val = rv_core->reg_file[rv_core->rs1];
        uint32_t rs2_val = rv_core->reg_file[rv_core->rs2];
        int32_t signed_tmp32 = rs1_val + rs2_val;
        rv_core->reg_file[rv_core->rd] = (rv_sword_t)signed_tmp32;
        return ADDW_CYCLE_COUNT;
    }

    static uint64_t instr_SUBW(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        uint32_t rs1_val = rv_core->reg_file[rv_core->rs1];
        uint32_t rs2_val = rv_core->reg_file[rv_core->rs2];
        int32_t signed_tmp32 = rs1_val - rs2_val;
        rv_core->reg_file[rv_core->rd] = (rv_sword_t)signed_tmp32;
        return SUBW_CYCLE_COUNT;
    }
#endif

#ifdef CSR_SUPPORT
    static inline uint64_t CSRRWx(Core *rv_core, rv_word_t new_val)
    {
        CORE_DBG("%s: %x "PRINTF_FMT" priv level: %d\n", __func__, rv_core->instruction, rv_core->pc, rv_core->curr_priv_mode);
        rv_word_t csr_val = 0;
        uint16_t csr_addr = rv_core->immediate;
        rv_word_t csr_mask = csr_get_mask(rv_core->csr_regs, csr_addr);
        rv_word_t not_allowed_bits = 0;
        rv_word_t new_csr_val = 0;

        if(rv_core->rd != 0)
        {
            if(csr_read_reg(rv_core->csr_regs, rv_core->curr_priv_mode, csr_addr, &csr_val))
            {
                // die_msg("Error reading CSR %x "PRINTF_FMT"\n", csr_addr, rv_core->pc);
                prepare_sync_trap(rv_core, trap_cause_illegal_instr, 0);
                return 0;
            }
        }

        not_allowed_bits = csr_val & ~csr_mask;
        new_csr_val = not_allowed_bits | (new_val & csr_mask);

        if(csr_write_reg(rv_core->csr_regs, rv_core->curr_priv_mode, csr_addr, new_csr_val))
        {
            // die_msg("Error reading CSR %x "PRINTF_FMT"\n", csr_addr, rv_core->pc);
            prepare_sync_trap(rv_core, trap_cause_illegal_instr, 0);
            return 0;
        }

        rv_core->reg_file[rv_core->rd] = csr_val & csr_mask;
        return CSRRW_CYCLE_COUNT;
    }

    static inline uint64_t CSRRSx(Core *rv_core, rv_word_t new_val)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t csr_val = 0;
        uint16_t csr_addr = rv_core->immediate;
        rv_word_t csr_mask = csr_get_mask(rv_core->csr_regs, csr_addr);
        rv_word_t new_csr_val = 0;

        if(csr_read_reg(rv_core->csr_regs, rv_core->curr_priv_mode, csr_addr, &csr_val))
        {
            // die_msg("Error reading CSR %x "PRINTF_FMT"\n", csr_addr, rv_core->pc);
            prepare_sync_trap(rv_core, trap_cause_illegal_instr, 0);
            return 0;
        }

        new_csr_val = (new_val & csr_mask);

        if(rv_core->rs1 != 0)
        {
            if(csr_write_reg(rv_core->csr_regs, rv_core->curr_priv_mode, csr_addr, csr_val | new_csr_val))
            {
                // die_msg("Error reading CSR %x "PRINTF_FMT"\n", csr_addr, rv_core->pc);
                prepare_sync_trap(rv_core, trap_cause_illegal_instr, 0);
                return 0;
            }
        }

        rv_core->reg_file[rv_core->rd] = csr_val & csr_mask;
        return CSRRS_CYCLE_COUNT;
    }

    static inline uint64_t CSRRCx(Core *rv_core, rv_word_t new_val)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t csr_val = 0;
        uint16_t csr_addr = rv_core->immediate;
        rv_word_t csr_mask = csr_get_mask(rv_core->csr_regs, csr_addr);
        rv_word_t new_csr_val = 0;

        if(csr_read_reg(rv_core->csr_regs, rv_core->curr_priv_mode, csr_addr, &csr_val))
        {
            // die_msg("Error reading CSR %x "PRINTF_FMT"\n", csr_addr, rv_core->pc);
            prepare_sync_trap(rv_core, trap_cause_illegal_instr, 0);
            return 0;
        }

        new_csr_val = (new_val & csr_mask);

        if(rv_core->rs1 != 0)
        {
            if(csr_write_reg(rv_core->csr_regs, rv_core->curr_priv_mode, csr_addr, csr_val & ~new_csr_val))
            {
                // die_msg("Error reading CSR %x "PRINTF_FMT"\n", csr_addr, rv_core->pc);
                prepare_sync_trap(rv_core, trap_cause_illegal_instr, 0);
                return 0;
            }
        }
        rv_core->reg_file[rv_core->rd] = csr_val & csr_mask;
        return CSRRC_CYCLE_COUNT;
    }

    static uint64_t instr_CSRRW(Core *rv_core)
    {
        return CSRRWx(rv_core, rv_core->reg_file[rv_core->rs1]);
    }

    static uint64_t instr_CSRRS(Core *rv_core)
    {
        return CSRRSx(rv_core, rv_core->reg_file[rv_core->rs1]);
    }

    static uint64_t instr_CSRRC(Core *rv_core)
    {
        return CSRRCx(rv_core, rv_core->reg_file[rv_core->rs1]);
    }

    static uint64_t instr_CSRRWI(Core *rv_core)
    {
        return CSRRWx(rv_core, rv_core->rs1);
    }

    static uint64_t instr_CSRRSI(Core *rv_core)
    {
        return CSRRSx(rv_core, rv_core->rs1);
    }

    static uint64_t instr_CSRRCI(Core *rv_core)
    {
        return CSRRCx(rv_core, rv_core->rs1);
    }

    static uint64_t instr_ECALL(Core *rv_core)
    {
        // printf("%s: %x from: %d\n", __func__, rv_core->instruction, trap_cause_user_ecall + rv_core->curr_priv_mode);
        prepare_sync_trap(rv_core, trap_cause_user_ecall + rv_core->curr_priv_mode, 0);
        return ECALL_CYCLE_COUNT;
    }

    static uint64_t instr_EBREAK(Core *rv_core)
    {
        /* not implemented */
        (void)rv_core;
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        return EBREAK_CYCLE_COUNT;
    }

    static uint64_t instr_MRET(Core *rv_core)
    {
        CORE_DBG("%s: "PRINTF_FMT"\n", __func__, *rv_core->trap.m.regs[trap_reg_ip]);
        privilege_level restored_priv_level = trap_restore_irq_settings(&rv_core->trap, rv_core->curr_priv_mode);
        rv_core->curr_priv_mode = restored_priv_level;
        rv_core->next_pc = *rv_core->trap.m.regs[trap_reg_epc];
        return MRET_CYCLE_COUNT;
    }

    static uint64_t instr_SRET(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        privilege_level restored_priv_level = trap_restore_irq_settings(&rv_core->trap, rv_core->curr_priv_mode);
        rv_core->curr_priv_mode = restored_priv_level;
        rv_core->next_pc = *rv_core->trap.s.regs[trap_reg_epc];
        return SRET_CYCLE_COUNT;
    }

    static uint64_t instr_URET(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        printf("URET! \n");
        while(1);
        /* not implemented */
        (void)rv_core;
        return URET_CYCLE_COUNT;
    }
#endif

#ifdef ATOMIC_SUPPORT
    static uint64_t instr_LR_W(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_core->lr_address = rv_core->reg_file[rv_core->rs1];
        rv_core->lr_valid = 1;
        return instr_LW(rv_core);
    }

    static uint64_t instr_SC_W(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        uint64_t ret_val = 0;
        if(rv_core->lr_valid && (rv_core->lr_address == rv_core->reg_file[rv_core->rs1]))
        {
            ret_val = instr_SW(rv_core);
            rv_core->reg_file[rv_core->rd] = 0;
        }
        else
        {
            rv_core->reg_file[rv_core->rd] = 1;
        }

        rv_core->lr_valid = 0;
        rv_core->lr_address = 0;
        return ret_val;
    }

    static uint64_t instr_AMOSWAP_W(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t address = rv_core->reg_file[rv_core->rs1];
        uint32_t rs2_val = rv_core->reg_file[rv_core->rs2];
        uint32_t result = 0;

        uint64_t load_time = instr_LW(rv_core);
        result = rs2_val;

        return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 4) + load_time;
    }

    static uint64_t instr_AMOADD_W(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t address = rv_core->reg_file[rv_core->rs1];
        uint32_t rd_val = 0;
        uint32_t rs2_val = rv_core->reg_file[rv_core->rs2];
        uint32_t result = 0;

        uint64_t load_time = instr_LW(rv_core);
        rd_val = rv_core->reg_file[rv_core->rd];
        result = rd_val + rs2_val;

        return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 4) + load_time;
    }

    static uint64_t instr_AMOXOR_W(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t address = rv_core->reg_file[rv_core->rs1];
        uint32_t rd_val = 0;
        uint32_t rs2_val = rv_core->reg_file[rv_core->rs2];
        uint32_t result = 0;
    
        uint64_t load_time = instr_LW(rv_core);
        rd_val = rv_core->reg_file[rv_core->rd];
        result = rd_val ^ rs2_val;

        return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 4) + load_time;
    }

    static uint64_t instr_AMOAND_W(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t address = rv_core->reg_file[rv_core->rs1];
        uint32_t rd_val = 0;
        uint32_t rs2_val = rv_core->reg_file[rv_core->rs2];
        uint32_t result = 0;

        uint64_t load_time = instr_LW(rv_core);
        rd_val = rv_core->reg_file[rv_core->rd];
        result = rd_val & rs2_val;

        return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 4) + load_time;
    }

    static uint64_t instr_AMOOR_W(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t address = rv_core->reg_file[rv_core->rs1];
        uint32_t rd_val = 0;
        uint32_t rs2_val = rv_core->reg_file[rv_core->rs2];
        uint32_t result = 0;

        uint64_t load_time = instr_LW(rv_core);
        rd_val = rv_core->reg_file[rv_core->rd];
        result = rd_val | rs2_val;

        return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 4) + load_time;
    }

    static uint64_t instr_AMOMIN_W(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t address = rv_core->reg_file[rv_core->rs1];
        int32_t rd_val = 0;
        int32_t rs2_val = rv_core->reg_file[rv_core->rs2];
        rv_word_t result = 0;

        uint64_t load_time = instr_LW(rv_core);

        rd_val = rv_core->reg_file[rv_core->rd];
        result = ASSIGN_MIN(rd_val, rs2_val);

        return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 4) + load_time;
    }

    static uint64_t instr_AMOMAX_W(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t address = rv_core->reg_file[rv_core->rs1];
        int32_t rd_val = 0;
        int32_t rs2_val = rv_core->reg_file[rv_core->rs2];
        rv_word_t result = 0;

        uint64_t load_time = instr_LW(rv_core);

        rd_val = rv_core->reg_file[rv_core->rd];
        result = ASSIGN_MAX(rd_val, rs2_val);

        return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 4) + load_time;
    }

    static uint64_t instr_AMOMINU_W(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t address = rv_core->reg_file[rv_core->rs1];
        uint32_t rd_val = 0;
        uint32_t rs2_val = rv_core->reg_file[rv_core->rs2];
        rv_word_t result = 0;

        uint64_t load_time = instr_LW(rv_core);

        rd_val = rv_core->reg_file[rv_core->rd];
        result = ASSIGN_MIN(rd_val, rs2_val);

        return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 4) + load_time;
    }

    static uint64_t instr_AMOMAXU_W(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t address = rv_core->reg_file[rv_core->rs1];
        uint32_t rd_val = 0;
        uint32_t rs2_val = rv_core->reg_file[rv_core->rs2];
        rv_word_t result = 0;

        uint64_t load_time = instr_LW(rv_core);

        rd_val = rv_core->reg_file[rv_core->rd];
        result = ASSIGN_MAX(rd_val, rs2_val);

        return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 4) + load_time;
    }

    #ifdef RV64
        static uint64_t instr_LR_D(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            rv_core->lr_valid = 1;
            rv_core->lr_address = rv_core->reg_file[rv_core->rs1];
            return instr_LD(rv_core);
        }

        static uint64_t instr_SC_D(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            uint64_t ret_time = 0;
            if(rv_core->lr_valid && (rv_core->lr_address == rv_core->reg_file[rv_core->rs1]))
            {
                ret_time = instr_SD(rv_core);
                rv_core->reg_file[rv_core->rd] = 0;
            }
            else
            {
                rv_core->reg_file[rv_core->rd] = 1;
            }

            rv_core->lr_valid = 0;
            rv_core->lr_address = 0;
            return ret_time;
        }

        static uint64_t instr_AMOSWAP_D(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            rv_word_t address = rv_core->reg_file[rv_core->rs1];
            rv_word_t rs2_val = rv_core->reg_file[rv_core->rs2];
            rv_word_t result = 0;
            uint64_t load_time = instr_LD(rv_core);

            result = rs2_val;

            return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 8) + load_time;
        }

        static uint64_t instr_AMOADD_D(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            rv_word_t address = rv_core->reg_file[rv_core->rs1];
            rv_word_t rd_val = 0;
            rv_word_t rs2_val = rv_core->reg_file[rv_core->rs2];
            rv_word_t result = 0;

            uint64_t load_time = instr_LD(rv_core);
            rd_val = rv_core->reg_file[rv_core->rd];
            result = rd_val + rs2_val;

            return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 8) + load_time;
        }

        static uint64_t instr_AMOXOR_D(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            rv_word_t address = rv_core->reg_file[rv_core->rs1];
            rv_word_t rd_val = 0;
            rv_word_t rs2_val = rv_core->reg_file[rv_core->rs2];
            rv_word_t result = 0;

            uint64_t load_time = instr_LD(rv_core);
            rd_val = rv_core->reg_file[rv_core->rd];
            result = rd_val ^ rs2_val;

            return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 8) + load_time;
        }

        static uint64_t instr_AMOAND_D(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            rv_word_t address = rv_core->reg_file[rv_core->rs1];
            rv_word_t rd_val = 0;
            rv_word_t rs2_val = rv_core->reg_file[rv_core->rs2];
            rv_word_t result = 0;

            uint64_t load_time = instr_LD(rv_core);
            rd_val = rv_core->reg_file[rv_core->rd];
            result = rd_val & rs2_val;

            return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 8) + load_time;
        }

        static uint64_t instr_AMOOR_D(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            rv_word_t address = rv_core->reg_file[rv_core->rs1];
            rv_word_t rd_val = 0;
            rv_word_t rs2_val = rv_core->reg_file[rv_core->rs2];
            rv_word_t result = 0;

            uint64_t load_time = instr_LD(rv_core);
            rd_val = rv_core->reg_file[rv_core->rd];
            result = rd_val | rs2_val;

            return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 8) + load_time;
        }

        static uint64_t instr_AMOMIN_D(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            rv_word_t address = rv_core->reg_file[rv_core->rs1];
            rv_sword_t rd_val = 0;
            rv_sword_t rs2_val = rv_core->reg_file[rv_core->rs2];
            rv_word_t result = 0;

            uint64_t load_time = instr_LD(rv_core);

            rd_val = rv_core->reg_file[rv_core->rd];
            result = ASSIGN_MIN(rd_val, rs2_val);

            return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 8) + load_time;
        }

        static uint64_t instr_AMOMAX_D(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            rv_word_t address = rv_core->reg_file[rv_core->rs1];
            rv_sword_t rd_val = 0;
            rv_sword_t rs2_val = rv_core->reg_file[rv_core->rs2];
            rv_word_t result = 0;

            uint64_t load_time = instr_LD(rv_core);

            rd_val = rv_core->reg_file[rv_core->rd];
            result = ASSIGN_MAX(rd_val, rs2_val);

            return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 8) + load_time;
        }

        static uint64_t instr_AMOMINU_D(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            rv_word_t address = rv_core->reg_file[rv_core->rs1];
            rv_word_t rd_val = 0;
            rv_word_t rs2_val = rv_core->reg_file[rv_core->rs2];
            rv_word_t result = 0;

            uint64_t load_time = instr_LD(rv_core);

            rd_val = rv_core->reg_file[rv_core->rd];
            result = ASSIGN_MIN(rd_val, rs2_val);

            return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 8) + load_time;
        }

        static uint64_t instr_AMOMAXU_D(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            rv_word_t address = rv_core->reg_file[rv_core->rs1];
            rv_word_t rd_val = 0;
            rv_word_t rs2_val = rv_core->reg_file[rv_core->rs2];
            rv_word_t result = 0;

            uint64_t load_time = instr_LD(rv_core);

            rd_val = rv_core->reg_file[rv_core->rd];
            result = ASSIGN_MAX(rd_val, rs2_val);

            return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &result, 8) + load_time;
        }
    #endif
#endif

#ifdef MULTIPLY_SUPPORT
    static int divCyclesUnsigned(uint64_t a, uint64_t b) {
        int ret_val = 0;
        if (b == 0) 
            ret_val = DIV_CYCLES_BASE + 1; // division-by-zero case
        double delta = std::log2((double)a) - std::log2((double)b);
        int iters = std::max(0, (int)std::floor(delta));

        // Total cycles = base latency + iteration count (capped)
        ret_val = DIV_CYCLES_BASE + iters;
        return ret_val;
    }

    static int divCyclesSigned(int64_t a, int64_t b) {
        int ret_val = 0;
        if (b == 0) 
            ret_val = DIV_CYCLES_BASE + 3; // extra cycles for sign handling?
        int base = divCyclesUnsigned(a < 0 ? -a : a, b < 0 ? -b : b);
        int negCycles = (a < 0) + (( (a<0) ^ (b<0) ) ? 1 : 0);
        ret_val = base + negCycles;
        return ret_val;
    }
    static uint64_t instr_DIV(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_sword_t signed_rs = rv_core->reg_file[rv_core->rs1];
        rv_sword_t signed_rs2 = rv_core->reg_file[rv_core->rs2];

        /* division by zero */
        if(signed_rs2 == 0)
        {
            rv_core->reg_file[rv_core->rd] = -1;
            return 0;
        }

        /* overflow */
        if(((rv_word_t)signed_rs == XLEN_INT_MIN) && (signed_rs2 == -1))
        {
            rv_core->reg_file[rv_core->rd] = XLEN_INT_MIN;
            return 0;
        }

        rv_core->reg_file[rv_core->rd] = (signed_rs/signed_rs2);
        return divCyclesSigned(signed_rs, signed_rs2);
    }

    static uint64_t instr_DIVU(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t unsigned_rs = rv_core->reg_file[rv_core->rs1];
        rv_word_t unsigned_rs2 = rv_core->reg_file[rv_core->rs2];

        /* division by zero */
        if(unsigned_rs2 == 0)
        {
            rv_core->reg_file[rv_core->rd] = -1;
            return 0;
        }

        rv_core->reg_file[rv_core->rd] = (unsigned_rs/unsigned_rs2);
        return divCyclesUnsigned(unsigned_rs, unsigned_rs2);
    }

    static uint64_t instr_REM(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_sword_t signed_rs = rv_core->reg_file[rv_core->rs1];
        rv_sword_t signed_rs2 = rv_core->reg_file[rv_core->rs2];

        /* division by zero */
        if(signed_rs2 == 0)
        {
            rv_core->reg_file[rv_core->rd] = signed_rs;
            return 0;
        }

        /* overflow */
        if(((rv_word_t)signed_rs == XLEN_INT_MIN) && (signed_rs2 == -1))
        {
            rv_core->reg_file[rv_core->rd] = 0;
            return 0;
        }

        rv_core->reg_file[rv_core->rd] = (signed_rs%signed_rs2);
        return divCyclesSigned(signed_rs, signed_rs2);
    }

    static uint64_t instr_REMU(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t unsigned_rs = rv_core->reg_file[rv_core->rs1];
        rv_word_t unsigned_rs2 = rv_core->reg_file[rv_core->rs2];

        /* division by zero */
        if(unsigned_rs2 == 0)
        {
            rv_core->reg_file[rv_core->rd] = unsigned_rs;
            return 0;
        }

        rv_core->reg_file[rv_core->rd] = (unsigned_rs%unsigned_rs2);
        return divCyclesUnsigned(unsigned_rs, unsigned_rs2);
    }

    static uint64_t instr_MUL(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_sword_t signed_rs = rv_core->reg_file[rv_core->rs1];
        rv_sword_t signed_rs2 = rv_core->reg_file[rv_core->rs2];
        rv_core->reg_file[rv_core->rd] = signed_rs * signed_rs2;
        return MUL_CYCLE_COUNT;
    }

    static uint64_t instr_MULH(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_sword_t result_hi = 0;
        rv_sword_t result_lo = 0;
        MULI(rv_core->reg_file[rv_core->rs1], rv_core->reg_file[rv_core->rs2], &result_hi, &result_lo);
        rv_core->reg_file[rv_core->rd] = result_hi;
        return MULH_CYCLE_COUNT;
    }

    static uint64_t instr_MULHU(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_word_t result_hi = 0;
        rv_word_t result_lo = 0;
        UMULI(rv_core->reg_file[rv_core->rs1], rv_core->reg_file[rv_core->rs2], &result_hi, &result_lo);
        rv_core->reg_file[rv_core->rd] = result_hi;
        return MULHU_CYCLE_COUNT;
    }

    static uint64_t instr_MULHSU(Core *rv_core)
    {
        CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
        rv_sword_t result_hi = 0;
        rv_sword_t result_lo = 0;
        MULHSU(rv_core->reg_file[rv_core->rs1], rv_core->reg_file[rv_core->rs2], &result_hi, &result_lo);
        rv_core->reg_file[rv_core->rd] = result_hi;
        return MULHSU_CYCLE_COUNT;
    }

    #ifdef RV64
        static uint64_t instr_MULW(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            int32_t signed_rs = rv_core->reg_file[rv_core->rs1];
            int32_t signed_rs2 = rv_core->reg_file[rv_core->rs2];
            rv_core->reg_file[rv_core->rd] = (rv_sword_t)(signed_rs * signed_rs2);
            return MULW_CYCLE_COUNT;
        }

        static uint64_t instr_DIVW(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            int32_t signed_rs = rv_core->reg_file[rv_core->rs1];
            int32_t signed_rs2 = rv_core->reg_file[rv_core->rs2];
            int32_t result = 0;

            /* division by zero */
            if(signed_rs2 == 0)
            {
                rv_core->reg_file[rv_core->rd] = -1;
                return 0;
            }

            /* overflow */
            if((signed_rs == INT32_MIN) && (signed_rs2 == -1))
            {
                rv_core->reg_file[rv_core->rd] = INT32_MIN;
                return 0;
            }

            result = (signed_rs/signed_rs2);

            rv_core->reg_file[rv_core->rd] = (rv_sword_t)result;
            return divCyclesSigned(signed_rs, signed_rs2);
        }

        static uint64_t instr_DIVUW(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            uint32_t unsigned_rs = rv_core->reg_file[rv_core->rs1];
            uint32_t unsigned_rs2 = rv_core->reg_file[rv_core->rs2];
            uint32_t result = 0;

            /* division by zero */
            if(unsigned_rs2 == 0)
            {
                rv_core->reg_file[rv_core->rd] = -1;
                return 0;
            }

            result = (unsigned_rs/unsigned_rs2);

            rv_core->reg_file[rv_core->rd] = SIGNEX_BIT_31(result);
            return divCyclesUnsigned(unsigned_rs, unsigned_rs2);
        }

        static uint64_t instr_REMW(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            int32_t signed_rs = rv_core->reg_file[rv_core->rs1];
            int32_t signed_rs2 = rv_core->reg_file[rv_core->rs2];
            int32_t result = 0;

            /* division by zero */
            if(signed_rs2 == 0)
            {
                rv_core->reg_file[rv_core->rd] = (rv_sword_t)signed_rs;
                return 0;
            }

            /* overflow */
            if((signed_rs == INT32_MIN) && (signed_rs2 == -1))
            {
                rv_core->reg_file[rv_core->rd] = 0;
                return 0;
            }

            result = (signed_rs%signed_rs2);

            rv_core->reg_file[rv_core->rd] = (rv_sword_t)result;
            return divCyclesSigned(signed_rs, signed_rs2);
        }

        static uint64_t instr_REMUW(Core *rv_core)
        {
            CORE_DBG("%s: %x\n", __func__, rv_core->instruction);
            uint32_t unsigned_rs = rv_core->reg_file[rv_core->rs1];
            uint32_t unsigned_rs2 = rv_core->reg_file[rv_core->rs2];
            uint32_t result = 0;

            /* division by zero */
            if(unsigned_rs2 == 0)
            {
                rv_core->reg_file[rv_core->rd] = SIGNEX_BIT_31(unsigned_rs);
                return 0;
            }

            result = (unsigned_rs%unsigned_rs2);

            rv_core->reg_file[rv_core->rd] = SIGNEX_BIT_31(result);
            return divCyclesUnsigned(unsigned_rs, unsigned_rs2);
        }
    #endif

#endif

static uint64_t instr_LREAD(Core *rv_core) {
    rv_core->pSOC->lread(rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)), rv_core->reg_file[rv_core->rs1], rv_core->reg_file[rv_core->rs2]);
    return 1;
}

static uint64_t instr_LWRITE(Core *rv_core) {
    rv_core->pSOC->lwrite(rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)), rv_core->reg_file[rv_core->rs1], rv_core->reg_file[rv_core->rs2]);
    return 1;
}

static uint64_t instr_LTRIM(Core * rv_core) {
    rv_core->pSOC->ltrim(rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)), rv_core->reg_file[rv_core->rs1], rv_core->reg_file[rv_core->rs2]);
    return 1;
}

static uint64_t instr_PREAD(Core * rv_core) {
    PAL::Request* req = (PAL::Request *) (rv_core->pSOC->get_ram() + (rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR));
    uint8_t* stored_addr = req->ioFlag.data;
    req->ioFlag.data = rv_core->pSOC->get_ram() + ((uint64_t)req->ioFlag.data - RAM_BASE_ADDR);
    rv_core->pSOC->pread((uint8_t*) req, (uint64_t*) (rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rs1] - RAM_BASE_ADDR))));
    req->ioFlag.data = stored_addr;
    return 1;
}

static uint64_t instr_PWRITE(Core * rv_core) {
    PAL::Request* req = (PAL::Request *) (rv_core->pSOC->get_ram() + (rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR));
    uint8_t* stored_addr = req->ioFlag.data;
    req->ioFlag.data = rv_core->pSOC->get_ram() + ((uint64_t)req->ioFlag.data - RAM_BASE_ADDR);
    printf("PWRITE: %p, %p, %p\n", req, req->ioFlag.data, (uint64_t*) (rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rs1] - RAM_BASE_ADDR))));
    rv_core->pSOC->pwrite((uint8_t*) req, (uint64_t*) (rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rs1] - RAM_BASE_ADDR))));
    req->ioFlag.data = stored_addr;
    return 1;
}

static uint64_t instr_PERASE(Core * rv_core) {
    PAL::Request* req = (PAL::Request *) (rv_core->pSOC->get_ram() + (rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR));
    uint8_t* stored_addr = req->ioFlag.data;
    req->ioFlag.data = rv_core->pSOC->get_ram() + ((uint64_t)req->ioFlag.data - RAM_BASE_ADDR);
    rv_core->pSOC->perase((uint8_t*) req, (uint64_t*) (rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rs1] - RAM_BASE_ADDR))));
    req->ioFlag.data = stored_addr;
    return 1;
}

static uint64_t instr_READBUFF(Core *rv_core) {
    if (rv_core->reg_file[rv_core->rs2] == FIRMWARE_CYCLE) {
        memcpy(rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)), &rv_core->curr_cycle, sizeof(rv_core->curr_cycle));
        if (rv_core->curr_cycle < 10) {
            rv_core->curr_cycle = 0;
        } else {
            rv_core->curr_cycle -= 10; // since get cycle is supposed to be purely for statistics, we subtract 10 cycles for overhead(based on firmware binary)
        }
    } else if (rv_core->reg_file[rv_core->rs2] == FIRMWARE_TICK) {
        uint64_t tick = getTick();
        memcpy(rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)), &tick, sizeof(tick));
        if (rv_core->curr_cycle < 10) {
            rv_core->curr_cycle = 0;
        } else {
            rv_core->curr_cycle -= 10; // since get tick is supposed to be purely for statistics, we subtract 10 cycles for overhead(based on firmware binary)
        }
    } else if (rv_core->reg_file[rv_core->rs2] == FIRMWARE_CSD_JOB_LOADED) {
        memcpy(rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)), &rv_core->csd_job_loaded, sizeof(rv_core->csd_job_loaded));
    } else if (rv_core->reg_file[rv_core->rs2] == FIRMWARE_CSD_JOB_FINISHED) {
        memcpy(rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)), &rv_core->csd_job_finished, sizeof(rv_core->csd_job_finished));
    }
    else if (rv_core->reg_file[rv_core->rs2] == FIRMWARE_CORE_ID) {
        memcpy(rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)), &rv_core->core_id, sizeof(rv_core->core_id));
    } else {
        rv_core->pSOC->read_buffer(rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)), rv_core->reg_file[rv_core->rs1], rv_core->reg_file[rv_core->rs2]);
    }
    return 1;
}

static uint64_t instr_WRITEBUFF(Core *rv_core) {
    if (rv_core->reg_file[rv_core->rs2] == ICL_STAT_LOC) {
        rv_core->pSOC->setICLStats((ICLStats *) (rv_core->pSOC->get_ram() + (rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)));
    } else if (rv_core->reg_file[rv_core->rs2] == FTL_STAT_LOC) {
        rv_core->pSOC->setFTLStats((FTLStats *) (rv_core->pSOC->get_ram() + (rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)));
    } else if (rv_core->reg_file[rv_core->rs2] == ICL_LOW) {
        rv_core->pSOC->setICLLow((uint64_t *) (rv_core->pSOC->get_ram() + (rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)));
    } else if (rv_core->reg_file[rv_core->rs2] == ICL_HIGH) {
        rv_core->pSOC->setICLHigh((uint64_t *) (rv_core->pSOC->get_ram() + (rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)));
    }  else if (rv_core->reg_file[rv_core->rs2] == CORE_SETUP_COMPLETED) {
        rv_core->pSOC->coreSetup(rv_core->reg_file[rv_core->rd], rv_core->reg_file[rv_core->rs1]);
    } else if (rv_core->reg_file[rv_core->rs2] == START_CSD_JOB) {
        CSDData tmp;
        printf("Calling start csd job!!!\n");
        memcpy(&tmp, rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)), sizeof(CSDData));
        printf("CSD start pc: %lu and alloc ptr: %lu and mem ptr %lu\n", tmp.start_pc, (uint64_t)tmp.allocator, (uint64_t)tmp.ext2);
        rv_core->init_csd_job(&tmp);
    } else if (rv_core->reg_file[rv_core->rs2] == CSD_JOB_FINISHED) {
        printf("CSD job finished!!!\n");
        uint32_t* pos = (uint32_t*)(rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)));
        int *chars_compared = (int *)(rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rs1] - RAM_BASE_ADDR)));
        printf("pos %u and chars compared %d\n", *pos, *chars_compared);
        rv_core->csd_job_finished = true;
    } else if (rv_core->reg_file[rv_core->rs2] == CLEANED_CSD_JOB) {
        rv_core->clean_csd_job();
    } else if (rv_core->reg_file[rv_core->rs2] == FIRMWARE_REQ_DONE && rv_core->in_csd_mode) { 
        // do nothing
    } else if (rv_core->reg_file[rv_core->rs2] == ACQUIRE_MUTEX_LOCK) {
        rv_core->mutex_lock_acquire_start = rv_core->curr_cycle;
        rv_core->mutex_lock_acquires += 1;
    } else if (rv_core->reg_file[rv_core->rs2] == ACQUIRED_MUTEX_LOCK) {
        rv_core->mutex_lock_acquire_cycles += (rv_core->curr_cycle - rv_core->mutex_lock_acquire_start);  
    } else {
        rv_core->pSOC->write_buffer(rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)), rv_core->reg_file[rv_core->rs1], rv_core->reg_file[rv_core->rs2] );
    }
    return 1;
}

static uint64_t instr_DRAMREAD(Core *rv_core) {
    return rv_core->pSOC->read(rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)), rv_core->reg_file[rv_core->rs1]);
    
}

static uint64_t instr_DRAMWRITE(Core *rv_core) {
    return rv_core->pSOC->write(rv_core->pSOC->get_ram() + ((rv_core->reg_file[rv_core->rd] - RAM_BASE_ADDR)), rv_core->reg_file[rv_core->rs1]);
}

static uint64_t instr_STARTSIM(Core *rv_core) {
    rv_core->pSOC->start_simulation();
    return 1;
}

static uint64_t instr_STOPSIM(Core *rv_core) {
    rv_core->pSOC->stop_simulation();
    return 1;
}

static uint64_t instr_NEXTSIMTICK(Core *rv_core) {
    rv_core->pSOC->next_simulation_tick(rv_core->reg_file[rv_core->rs1]);
    return 1;
}

static uint64_t instr_PUTC(Core *rv_core) {
    if(rv_core->reg_file[rv_core->rs1] == 0) {
        printf("\n");
    } else {
        printf("%c", (char)rv_core->reg_file[rv_core->rs1]);
    }
    return 1;
}

static uint64_t instr_FADD(Core *rv_core) {
    rv_core->float_reg_file[rv_core->rd] = rv_core->float_reg_file[rv_core->rs1] + rv_core->float_reg_file[rv_core->rs2];
    return FLT_ADD_CYCLE_COUNT;
}

static uint64_t instr_FSUB(Core *rv_core) {
    rv_core->float_reg_file[rv_core->rd] = rv_core->float_reg_file[rv_core->rs1] - rv_core->float_reg_file[rv_core->rs2];
    return FLT_SUB_CYCLE_COUNT;
}

static uint64_t instr_FMUL(Core *rv_core) {
    rv_core->float_reg_file[rv_core->rd] = rv_core->float_reg_file[rv_core->rs1] * rv_core->float_reg_file[rv_core->rs2];
    return FLT_MUL_CYCLE_COUNT;
}

static uint64_t instr_FDIV(Core *rv_core) {
    rv_core->float_reg_file[rv_core->rd] = rv_core->float_reg_file[rv_core->rs1] / rv_core->float_reg_file[rv_core->rs2];
    return FLT_DIV_CYCLE_COUNT;
}

static uint64_t instr_FSQRT(Core *rv_core) {
    rv_core->float_reg_file[rv_core->rd] = std::sqrt(rv_core->float_reg_file[rv_core->rs1]);
    printf("executing float sqrt on %f and %f for result %f\n",  rv_core->float_reg_file[rv_core->rs1], rv_core->float_reg_file[rv_core->rs2], rv_core->float_reg_file[rv_core->rd]);
    return FLT_SQRT_CYCLE_COUNT;
}

static uint64_t instr_FSGNJ(Core *rv_core) {
    rv_core->float_reg_file[rv_core->rd] = std::copysign(rv_core->float_reg_file[rv_core->rs1], rv_core->float_reg_file[rv_core->rs2]);
    printf("executing float sign injunction on %f and %f for result %f\n at pc %lx",  rv_core->float_reg_file[rv_core->rs1], rv_core->float_reg_file[rv_core->rs2], rv_core->float_reg_file[rv_core->rd], rv_core->pc);
    return FLT_SGNJ_CYCLE_COUNT;
}

static uint64_t instr_FSGNJN(Core *rv_core) {
    rv_core->float_reg_file[rv_core->rd] = std::copysign(rv_core->float_reg_file[rv_core->rs1], -rv_core->float_reg_file[rv_core->rs2]);
    return FLT_SGNJN_CYCLE_COUNT;
}

// TO-DO implement
static uint64_t instr_FSGNJX(Core *rv_core) {
    // rv_core->float_reg_file[rv_core->rd] = std::copysign(rv_core->float_reg_file[rv_core->rs1], rv_core->float_reg_file[rv_core->rs2]);
    return FLT_SGNJX_CYCLE_COUNT;
}

static uint64_t instr_FMIN(Core *rv_core) {
    rv_core->float_reg_file[rv_core->rd] = std::min(rv_core->float_reg_file[rv_core->rs1], rv_core->float_reg_file[rv_core->rs2]);
    printf("executing float min on %f and %f for result %f\n",  rv_core->float_reg_file[rv_core->rs1], rv_core->float_reg_file[rv_core->rs2], rv_core->float_reg_file[rv_core->rd]);
    return FLT_MIN_CYCLE_COUNT;
}

static uint64_t instr_FMAX(Core *rv_core) {
    rv_core->float_reg_file[rv_core->rd] = std::max(rv_core->float_reg_file[rv_core->rs1], rv_core->float_reg_file[rv_core->rs2]);
    printf("executing float max on %f and %f for result %f\n",  rv_core->float_reg_file[rv_core->rs1], rv_core->float_reg_file[rv_core->rs2], rv_core->float_reg_file[rv_core->rd]);
    return FLT_MAX_CYCLE_COUNT;
}

static uint64_t instr_FEQ(Core *rv_core) {
    rv_core->reg_file[rv_core->rd] = (rv_core->float_reg_file[rv_core->rs1] == rv_core->float_reg_file[rv_core->rs2]);
    printf("executing float equal on %f and %f for result %f\n",  rv_core->float_reg_file[rv_core->rs1], rv_core->float_reg_file[rv_core->rs2], rv_core->float_reg_file[rv_core->rd]);
    return FLT_CMPEQ_CYCLE_COUNT;
}

static uint64_t instr_FLT(Core *rv_core) {
    rv_core->reg_file[rv_core->rd] = (rv_core->float_reg_file[rv_core->rs1] < rv_core->float_reg_file[rv_core->rs2]);
    printf("executing float less than on %f and %f for result %f\n",  rv_core->float_reg_file[rv_core->rs1], rv_core->float_reg_file[rv_core->rs2], rv_core->float_reg_file[rv_core->rd]);
    return FLT_CMPLT_CYCLE_COUNT;
}

static uint64_t instr_FLE(Core *rv_core) {
    rv_core->reg_file[rv_core->rd] = (rv_core->float_reg_file[rv_core->rs1] <= rv_core->float_reg_file[rv_core->rs2]);
    printf("executing float equal on %f and %f for result %f\n",  rv_core->float_reg_file[rv_core->rs1], rv_core->float_reg_file[rv_core->rs2], rv_core->float_reg_file[rv_core->rd]);
    return FLT_CMPLE_CYCLE_COUNT;
}

// TO-DO implement
static uint64_t instr_CLASSIFY(Core *rv_core) {
    return 1;
}

static uint64_t instr_FLW(Core *rv_core) {
    float tmp_load_val = 0;
    rv_sword_t signed_offset = SIGNEX_BIT_11(rv_core->immediate);
    rv_sword_t address = rv_core->reg_file[rv_core->rs1] + signed_offset;
    uint64_t ret_val = mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_read_access, address, &tmp_load_val, 4);
    if (ret_val != 0)
        rv_core->float_reg_file[rv_core->rd] = tmp_load_val;
    return ret_val + FLT_LOAD_EXTRA_CYCLE_COUNT;
}

static uint64_t instr_FSW(Core *rv_core) {
    rv_sword_t signed_offset = SIGNEX_BIT_11(rv_core->immediate);
    rv_word_t address = rv_core->reg_file[rv_core->rs1] + signed_offset;
    float value_to_write = rv_core->float_reg_file[rv_core->rs2];
    return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_write_access, address, &value_to_write, 4) + FLT_STORE_EXTRA_CYCLE_COUNT; 
}

static uint64_t instr_FTOSINT(Core *rv_core) {
    rv_core->reg_file[rv_core->rd] = static_cast<int>(rv_core->float_reg_file[rv_core->rs1]);
    return FLT_CVT_TO_SINT_CYCLE_COUNT;
}

static uint64_t instr_FTOLONG(Core *rv_core) {
    rv_core->reg_file[rv_core->rd] = static_cast<long long>(rv_core->float_reg_file[rv_core->rs1]);
    return FLT_CVT_TO_LONG_CYCLE_COUNT;
}

static uint64_t instr_FTOUINT(Core *rv_core) {
    rv_core->reg_file[rv_core->rd] = static_cast<uint32_t>(rv_core->float_reg_file[rv_core->rs1]);
    return FLT_CVT_TO_UINT_CYCLE_COUNT;
}

static uint64_t instr_FTOULONG(Core *rv_core) {
    rv_core->reg_file[rv_core->rd] = static_cast<uint64_t>(rv_core->float_reg_file[rv_core->rs1]);
    return FLT_CVT_TO_ULONG_CYCLE_COUNT;
}

static uint64_t instr_FFROMSINT(Core *rv_core) {

    rv_core->float_reg_file[rv_core->rd] = static_cast<float>(static_cast<rv_sword_t>(rv_core->reg_file[rv_core->rs1]));
    return FLT_CVT_FROM_SINT_CYCLE_COUNT;
}

static uint64_t instr_FFROMUINT(Core *rv_core) {
    rv_core->float_reg_file[rv_core->rd] = static_cast<float>(rv_core->reg_file[rv_core->rs1]);
    return FLT_CVT_FROM_UINT_CYCLE_COUNT;
}

static uint64_t instr_FMVTOINT(Core *rv_core) {
    memcpy(rv_core->reg_file + rv_core->rs1, rv_core->float_reg_file + rv_core->rd, sizeof(float));
    printf("executing float move to int on %f and %f for result %f\n",  rv_core->float_reg_file[rv_core->rs1], rv_core->float_reg_file[rv_core->rs2], rv_core->float_reg_file[rv_core->rd]);
    return FLT_MV_TO_INT_CYCLE_COUNT;
}

static uint64_t instr_FMVFROMINT(Core *rv_core) {
    memcpy(rv_core->float_reg_file + rv_core->rd, rv_core->reg_file + rv_core->rs1, sizeof(int));
    printf("executing float from int on %f and %f for result %f\n",  rv_core->float_reg_file[rv_core->rs1], rv_core->float_reg_file[rv_core->rs2], rv_core->float_reg_file[rv_core->rd]);
    return FLT_MV_FROM_INT_CYCLE_COUNT;
}

static uint64_t instr_FMADD(Core *rv_core) {
    rv_core->float_reg_file[rv_core->rd] = rv_core->float_reg_file[rv_core->rs1]*rv_core->float_reg_file[rv_core->rs2] + rv_core->float_reg_file[rv_core->rs3];
    return FMADD_CYCLE_COUNT;
}

static uint64_t instr_FMSUB(Core *rv_core) {
    rv_core->float_reg_file[rv_core->rd] = rv_core->float_reg_file[rv_core->rs1]*rv_core->float_reg_file[rv_core->rs2] - rv_core->float_reg_file[rv_core->rs3];
    return FMSUB_CYCLE_COUNT;
}

static uint64_t instr_FMNADD(Core *rv_core) {
    rv_core->float_reg_file[rv_core->rd] = -(rv_core->float_reg_file[rv_core->rs1]*rv_core->float_reg_file[rv_core->rs2] + rv_core->float_reg_file[rv_core->rs3]);
    return FMNADD_CYCLE_COUNT;
}

static uint64_t instr_FMNSUB(Core *rv_core) {
    rv_core->float_reg_file[rv_core->rd] = -(rv_core->float_reg_file[rv_core->rs1]*rv_core->float_reg_file[rv_core->rs2] - rv_core->float_reg_file[rv_core->rs3]);
    return FMNSUB_CYCLE_COUNT;
}

#ifdef ATOMIC_SUPPORT
    static void preparation_func5(Core *rv_core, int32_t *next_subcode)
    {
        rv_core->func5 = ((rv_core->instruction >> 27) & 0x1F);
        *next_subcode = rv_core->func5;
    }
#endif

#ifdef RV64
    static void preparation_func6(Core *rv_core, int32_t *next_subcode)
    {
        rv_core->func6 = ((rv_core->instruction >> 26) & 0x3F);
        *next_subcode = rv_core->func6;
    }
#endif

static void preparation_func7(Core *rv_core, int32_t *next_subcode)
{
    rv_core->func7 = ((rv_core->instruction >> 25) & 0x7F);
    if (next_subcode != NULL) {
        *next_subcode = rv_core->func7;
    }
}

static void preparation_func7_func12_sub5_extended(Core *rv_core, int32_t *next_subcode)
{
    rv_core->func5 = ((rv_core->instruction >> 20) & 0x1F);
    if (next_subcode != NULL) {
        *next_subcode = rv_core->func5;
    }
}

static void R_type_preparation(Core *rv_core, int32_t *next_subcode)
{
    rv_core->rd = ((rv_core->instruction >> 7) & 0x1F);
    rv_core->func3 = ((rv_core->instruction >> 12) & 0x7);
    rv_core->rs1 = ((rv_core->instruction >> 15) & 0x1F);
    rv_core->rs2 = ((rv_core->instruction >> 20) & 0x1F);
    if (next_subcode != NULL) {
        *next_subcode = rv_core->func3;
    }
}

static void I_type_preparation(Core *rv_core, int32_t *next_subcode)
{
    rv_core->rd = ((rv_core->instruction >> 7) & 0x1F);
    rv_core->func3 = ((rv_core->instruction >> 12) & 0x7);
    rv_core->rs1 = ((rv_core->instruction >> 15) & 0x1F);
    rv_core->immediate = ((rv_core->instruction >> 20) & 0xFFF);
    if (next_subcode != NULL) {
        *next_subcode = rv_core->func3;
    }
}

static void S_type_preparation(Core *rv_core, int32_t *next_subcode)
{
    rv_core->func3 = ((rv_core->instruction >> 12) & 0x7);
    rv_core->rs1 = ((rv_core->instruction >> 15) & 0x1F);
    rv_core->rs2 = ((rv_core->instruction >> 20) & 0x1F);
    rv_core->immediate = (((rv_core->instruction >> 25) << 5) | ((rv_core->instruction >> 7) & 0x1F));
    if (next_subcode != NULL) {
        *next_subcode = rv_core->func3;
    }
}

static void B_type_preparation(Core *rv_core, int32_t *next_subcode)
{
    rv_core->rd = ((rv_core->instruction >> 7) & 0x1F);
    rv_core->func3 = ((rv_core->instruction >> 12) & 0x7);
    rv_core->rs1 = ((rv_core->instruction >> 15) & 0x1F);
    rv_core->rs2 = ((rv_core->instruction >> 20) & 0x1F);
    rv_core->jump_offset=((extract32(rv_core->instruction, 8, 4) << 1) |
                          (extract32(rv_core->instruction, 25, 6) << 5) |
                          (extract32(rv_core->instruction, 7, 1) << 11) |
                          (extract32(rv_core->instruction, 31, 1) << 12) );
    rv_core->jump_offset = SIGNEX_BIT_12(rv_core->jump_offset);
    if (next_subcode != NULL) {
        *next_subcode = rv_core->func3;
    }
}

static void U_type_preparation(Core *rv_core, int32_t *next_subcode)
{
    rv_core->rd = ((rv_core->instruction >> 7) & 0x1F);
    rv_core->immediate = ((rv_core->instruction >> 12) & 0xFFFFF);
    rv_core->immediate = SIGNEX_BIT_19(rv_core->immediate);
    if (next_subcode != NULL) {
        *next_subcode = -1;
    }
}

static void J_type_preparation(Core *rv_core, int32_t *next_subcode)
{
    rv_core->rd = ((rv_core->instruction >> 7) & 0x1F);
    rv_core->jump_offset=((extract32(rv_core->instruction, 21, 10) << 1) |
                          (extract32(rv_core->instruction, 20, 1) << 11) |
                          (extract32(rv_core->instruction, 12, 8) << 12) |
                          (extract32(rv_core->instruction, 31, 1) << 20));
    /* sign extend the 20 bit number */
    rv_core->jump_offset = SIGNEX_BIT_20(rv_core->jump_offset);
    if (next_subcode != NULL) {
        *next_subcode = -1;
    }
}

static void R4_type_preparation(Core *rv_core, int32_t *next_subcode) {
    (void) next_subcode;
    rv_core->rd = ((rv_core->instruction >> 7) & 0x1F);
    rv_core->func3 = ((rv_core->instruction >> 12) & 0x7);
    rv_core->rs1 = ((rv_core->instruction >> 15) & 0x1F);
    rv_core->rs2 = ((rv_core->instruction >> 20) & 0x1F);
    rv_core->rs3 = ((rv_core->instruction >> 27) & 0x1F);
    if (next_subcode != NULL) {
        *next_subcode = -1;
    }
}

static void R_float_type_preparation(Core *rv_core, int32_t *next_subcode) {
    (void) next_subcode;
    rv_core->rd = ((rv_core->instruction >> 7) & 0x1F);
    rv_core->func3 = ((rv_core->instruction >> 12) & 0x7);
    rv_core->rs1 = ((rv_core->instruction >> 15) & 0x1F);
    rv_core->rs2 = ((rv_core->instruction >> 20) & 0x1F);
    rv_core->func7 = ((rv_core->instruction >> 25) & 0x7F);
    uint8_t set_instr = 1;
    switch((rv_core->func7 >> 2)) {
        case FLT_ADD: 
            rv_core->execute_cb = instr_FADD;
            break;
        case FLT_SUB:
            rv_core->execute_cb = instr_FSUB; 
            break;
        case FLT_MUL:
            rv_core->execute_cb = instr_FMUL;
            break;
        case FLT_DIV:
            rv_core->execute_cb = instr_FDIV;
            break;
        case FLT_SQRT:
            rv_core->execute_cb = instr_FSQRT;
            break;
        case FLT_SGNINJ:
            if (rv_core->func3 == FLT_SGNJ) {
                rv_core->execute_cb = instr_FSGNJ;
            } else if (rv_core->func3 == FLT_SGNJN) {
                rv_core->execute_cb = instr_FSGNJN;
            } else if (rv_core->func3 == FLT_SGNJX) {
                rv_core->execute_cb = instr_FSGNJX; 
            }
            break;
        case FLT_MAX_MIN:
            if (rv_core->func3 == FLT_MIN) {
                rv_core->execute_cb = instr_FMIN;
            } else if (rv_core->func3 == FLT_MAX) {
                rv_core->execute_cb = instr_FMAX;
            }
            break;
        case FLT_CMP:
            printf("executing float compare with func3 %d and func7 %d\n", rv_core->func3, rv_core->func7);
            if (rv_core->func3 == FLT_CMPEQ) {
                rv_core->execute_cb = instr_FEQ;
            } else if (rv_core->func3 == FLT_CMPLT) {
                rv_core->execute_cb = instr_FLT;
            } else if (rv_core->func3 == FLT_CMPLE) {
                rv_core->execute_cb = instr_FLE;
            }
            break;
        default:
            set_instr = 0;
            break;
    } 
    if (!set_instr) {
        set_instr = 1;
        if(rv_core->func7 == FLT_CVT_TO_INT) {
            if (rv_core->rs2 == FLT_CVT_TO_SINT) {
                rv_core->execute_cb = instr_FTOSINT;
            } else if (rv_core->rs2 == FLT_CVT_TO_UINT) {
                rv_core->execute_cb = instr_FTOUINT;
            } else if (rv_core->rs2 == FLT_CVT_TO_LONG) {
                rv_core->execute_cb = instr_FTOLONG;
            } else if (rv_core->rs2 == FLT_CVT_TO_ULONG) {
                rv_core->execute_cb = instr_FTOULONG;
            }
        } else if (rv_core->func7 == FLT_CVT_FROM_INT) {
            if (rv_core->rs2 == FLT_CVT_FROM_SINT || rv_core->rs2 == FLT_CVT_FROM_LONG) {
                rv_core->execute_cb = instr_FFROMSINT;
            } else if (rv_core->rs2 == FLT_CVT_FROM_UINT || rv_core->rs2 == FLT_CVT_FROM_ULONG) {
                rv_core->execute_cb = instr_FFROMUINT; 
            }
        } else if (rv_core->func3 == FLT_MV_FUNC3) {
            if (rv_core->func7 == FLT_MV_TO_INT) {
                rv_core->execute_cb = instr_FMVTOINT;
            } else if (rv_core->func7 == FLT_MV_FROM_INT) {
                rv_core->execute_cb = instr_FMVFROMINT;
            }
        } else {
            printf("no callback found for instruction %x with func3 %d and func7 %d\n", rv_core->instruction, rv_core->func3, rv_core->func7);
            set_instr = 0;
        }
    }
}



static instruction_hook_td RV_opcode_list[MAX_INSTR_OPCODE] = {};
INIT_INSTRUCTION_LIST_DESC(RV_opcode_list);

static void init_instruction_hooks() {
    static instruction_hook_td JALR_func3_subcode_list[MAX_FUNC3_VALUE] = {};
    JALR_func3_subcode_list[FUNC3_INSTR_JALR] = {NULL, instr_JALR, NULL};
    INIT_INSTRUCTION_LIST_DESC(JALR_func3_subcode_list);

    static instruction_hook_td BEQ_BNE_BLT_BGE_BLTU_BGEU_func3_subcode_list[MAX_FUNC3_VALUE] = {};
        BEQ_BNE_BLT_BGE_BLTU_BGEU_func3_subcode_list[FUNC3_INSTR_BEQ] = {NULL, instr_BEQ, NULL};
        BEQ_BNE_BLT_BGE_BLTU_BGEU_func3_subcode_list[FUNC3_INSTR_BNE] = {NULL, instr_BNE, NULL};
        BEQ_BNE_BLT_BGE_BLTU_BGEU_func3_subcode_list[FUNC3_INSTR_BLT] = {NULL, instr_BLT, NULL};
        BEQ_BNE_BLT_BGE_BLTU_BGEU_func3_subcode_list[FUNC3_INSTR_BGE] = {NULL, instr_BGE, NULL};
        BEQ_BNE_BLT_BGE_BLTU_BGEU_func3_subcode_list[FUNC3_INSTR_BLTU] = {NULL, instr_BLTU, NULL};
        BEQ_BNE_BLT_BGE_BLTU_BGEU_func3_subcode_list[FUNC3_INSTR_BGEU] = {NULL, instr_BGEU, NULL};
    INIT_INSTRUCTION_LIST_DESC(BEQ_BNE_BLT_BGE_BLTU_BGEU_func3_subcode_list);

    static instruction_hook_td LB_LH_LW_LBU_LHU_LWU_LD_func3_subcode_list[MAX_FUNC3_VALUE] = {};
        LB_LH_LW_LBU_LHU_LWU_LD_func3_subcode_list[FUNC3_INSTR_LB] = {NULL, instr_LB, NULL};
        LB_LH_LW_LBU_LHU_LWU_LD_func3_subcode_list[FUNC3_INSTR_LH] = {NULL, instr_LH, NULL};
        LB_LH_LW_LBU_LHU_LWU_LD_func3_subcode_list[FUNC3_INSTR_LW] = {NULL, instr_LW, NULL};
        LB_LH_LW_LBU_LHU_LWU_LD_func3_subcode_list[FUNC3_INSTR_LBU] = {NULL, instr_LBU, NULL};
        LB_LH_LW_LBU_LHU_LWU_LD_func3_subcode_list[FUNC3_INSTR_LHU] = {NULL, instr_LHU, NULL};
        #ifdef RV64
            LB_LH_LW_LBU_LHU_LWU_LD_func3_subcode_list[FUNC3_INSTR_LWU] = {NULL, instr_LWU, NULL};
            LB_LH_LW_LBU_LHU_LWU_LD_func3_subcode_list[FUNC3_INSTR_LD] = {NULL, instr_LD, NULL};
        #endif
    INIT_INSTRUCTION_LIST_DESC(LB_LH_LW_LBU_LHU_LWU_LD_func3_subcode_list);

    static instruction_hook_td SB_SH_SW_SD_func3_subcode_list[MAX_FUNC3_VALUE] = {};
        SB_SH_SW_SD_func3_subcode_list[FUNC3_INSTR_SB] = {NULL, instr_SB, NULL};
        SB_SH_SW_SD_func3_subcode_list[FUNC3_INSTR_SH] = {NULL, instr_SH, NULL};
        SB_SH_SW_SD_func3_subcode_list[FUNC3_INSTR_SW] = {NULL, instr_SW, NULL};
        #ifdef RV64
            SB_SH_SW_SD_func3_subcode_list[FUNC3_INSTR_SD] = {NULL, instr_SD, NULL};
        #endif
    INIT_INSTRUCTION_LIST_DESC(SB_SH_SW_SD_func3_subcode_list);

    #ifdef RV64
        static instruction_hook_td SRLI_SRAI_func6_subcode_list[MAX_FUNC6_VALUE] = {};
            SRLI_SRAI_func6_subcode_list[FUNC6_INSTR_SRLI] = {NULL, instr_SRLI, NULL};
            SRLI_SRAI_func6_subcode_list[FUNC6_INSTR_SRAI] = {NULL, instr_SRAI, NULL};
        INIT_INSTRUCTION_LIST_DESC(SRLI_SRAI_func6_subcode_list);
    #else
        static instruction_hook_td SRLI_SRAI_func7_subcode_list[MAX_FUNC7_VALUE] = {};
            SRLI_SRAI_func7_subcode_list[FUNC7_INSTR_SRLI] = {NULL, instr_SRLI, NULL};
            SRLI_SRAI_func7_subcode_list[FUNC7_INSTR_SRAI] = {NULL, instr_SRAI, NULL};
        INIT_INSTRUCTION_LIST_DESC(SRLI_SRAI_func7_subcode_list);
    #endif

    static instruction_hook_td ADDI_SLTI_SLTIU_XORI_ORI_ANDI_SLLI_SRLI_SRAI_func3_subcode_list[MAX_FUNC3_VALUE] = {};
        ADDI_SLTI_SLTIU_XORI_ORI_ANDI_SLLI_SRLI_SRAI_func3_subcode_list[FUNC3_INSTR_ADDI] = {NULL, instr_ADDI, NULL};
        ADDI_SLTI_SLTIU_XORI_ORI_ANDI_SLLI_SRLI_SRAI_func3_subcode_list[FUNC3_INSTR_SLTI] = {NULL, instr_SLTI, NULL};
        ADDI_SLTI_SLTIU_XORI_ORI_ANDI_SLLI_SRLI_SRAI_func3_subcode_list[FUNC3_INSTR_SLTIU] = {NULL, instr_SLTIU, NULL};
        ADDI_SLTI_SLTIU_XORI_ORI_ANDI_SLLI_SRLI_SRAI_func3_subcode_list[FUNC3_INSTR_XORI] = {NULL, instr_XORI, NULL};
        ADDI_SLTI_SLTIU_XORI_ORI_ANDI_SLLI_SRLI_SRAI_func3_subcode_list[FUNC3_INSTR_ORI] = {NULL, instr_ORI, NULL};
        ADDI_SLTI_SLTIU_XORI_ORI_ANDI_SLLI_SRLI_SRAI_func3_subcode_list[FUNC3_INSTR_ANDI] = {NULL, instr_ANDI, NULL};
        ADDI_SLTI_SLTIU_XORI_ORI_ANDI_SLLI_SRLI_SRAI_func3_subcode_list[FUNC3_INSTR_SLLI] = {NULL, instr_SLLI, NULL};
        #ifdef RV64
            ADDI_SLTI_SLTIU_XORI_ORI_ANDI_SLLI_SRLI_SRAI_func3_subcode_list[FUNC3_INSTR_SRLI_SRAI] = {preparation_func6, NULL, &SRLI_SRAI_func6_subcode_list_desc};
        #else
            ADDI_SLTI_SLTIU_XORI_ORI_ANDI_SLLI_SRLI_SRAI_func3_subcode_list[FUNC3_INSTR_SRLI_SRAI] = {preparation_func7, NULL, &SRLI_SRAI_func7_subcode_list_desc};
        #endif
    INIT_INSTRUCTION_LIST_DESC(ADDI_SLTI_SLTIU_XORI_ORI_ANDI_SLLI_SRLI_SRAI_func3_subcode_list);

    static instruction_hook_td ADD_SUB_MUL_func7_subcode_list[MAX_FUNC7_VALUE] = {};
        ADD_SUB_MUL_func7_subcode_list[FUNC7_INSTR_ADD] = {NULL, instr_ADD, NULL};
        ADD_SUB_MUL_func7_subcode_list[FUNC7_INSTR_SUB] = {NULL, instr_SUB, NULL};
        #ifdef MULTIPLY_SUPPORT
            ADD_SUB_MUL_func7_subcode_list[FUNC7_INSTR_MUL] = {NULL, instr_MUL, NULL};
        #endif
    INIT_INSTRUCTION_LIST_DESC(ADD_SUB_MUL_func7_subcode_list);

    static instruction_hook_td SLL_MULH_func7_subcode_list[MAX_FUNC7_VALUE] = {};
        SLL_MULH_func7_subcode_list[FUNC7_INSTR_SLL] = {NULL, instr_SLL, NULL};
        #ifdef MULTIPLY_SUPPORT
            SLL_MULH_func7_subcode_list[FUNC7_INSTR_MUL] = {NULL, instr_MULH, NULL};
        #endif
    INIT_INSTRUCTION_LIST_DESC(SLL_MULH_func7_subcode_list);

    static instruction_hook_td SLT_MULHSU_func7_subcode_list[MAX_FUNC7_VALUE] = {};
        SLT_MULHSU_func7_subcode_list[FUNC7_INSTR_SLT] = {NULL, instr_SLT, NULL};
        #ifdef MULTIPLY_SUPPORT
            SLT_MULHSU_func7_subcode_list[FUNC7_INSTR_MULHSU] = {NULL, instr_MULHSU, NULL};
        #endif
    INIT_INSTRUCTION_LIST_DESC(SLT_MULHSU_func7_subcode_list);

    static instruction_hook_td SLTU_MULHU_func7_subcode_list[MAX_FUNC7_VALUE] = {};
        SLTU_MULHU_func7_subcode_list[FUNC7_INSTR_SLTU] = {NULL, instr_SLTU, NULL};
        #ifdef MULTIPLY_SUPPORT
            SLTU_MULHU_func7_subcode_list[FUNC7_INSTR_MULHU] = {NULL, instr_MULHU, NULL};
        #endif
    INIT_INSTRUCTION_LIST_DESC(SLTU_MULHU_func7_subcode_list);

    static instruction_hook_td XOR_DIV_func7_subcode_list[MAX_FUNC7_VALUE] = {};
        XOR_DIV_func7_subcode_list[FUNC7_INSTR_XOR] = {NULL, instr_XOR, NULL};
        #ifdef MULTIPLY_SUPPORT
            XOR_DIV_func7_subcode_list[FUNC7_INSTR_DIV] = {NULL, instr_DIV, NULL};
        #endif
    INIT_INSTRUCTION_LIST_DESC(XOR_DIV_func7_subcode_list);

    static instruction_hook_td SRL_SRA_DIVU_func7_subcode_list[MAX_FUNC7_VALUE] = {};
        SRL_SRA_DIVU_func7_subcode_list[FUNC7_INSTR_SRL] = {NULL, instr_SRL, NULL};
        SRL_SRA_DIVU_func7_subcode_list[FUNC7_INSTR_SRA] = {NULL, instr_SRA, NULL};
        #ifdef MULTIPLY_SUPPORT
            SRL_SRA_DIVU_func7_subcode_list[FUNC7_INSTR_DIVU] = {NULL, instr_DIVU, NULL};
        #endif
    INIT_INSTRUCTION_LIST_DESC(SRL_SRA_DIVU_func7_subcode_list);

    static instruction_hook_td OR_REM_func7_subcode_list[MAX_FUNC7_VALUE] = {};
        OR_REM_func7_subcode_list[FUNC7_INSTR_OR] = {NULL, instr_OR, NULL};
        #ifdef MULTIPLY_SUPPORT
            OR_REM_func7_subcode_list[FUNC7_INSTR_REM] = {NULL, instr_REM, NULL};
        #endif
    INIT_INSTRUCTION_LIST_DESC(OR_REM_func7_subcode_list);

    static instruction_hook_td AND_REMU_func7_subcode_list[MAX_FUNC7_VALUE] = {};
        AND_REMU_func7_subcode_list[FUNC7_INSTR_AND] = {NULL, instr_AND, NULL};
        #ifdef MULTIPLY_SUPPORT
            AND_REMU_func7_subcode_list[FUNC7_INSTR_REMU] = {NULL, instr_REMU, NULL};
        #endif
    INIT_INSTRUCTION_LIST_DESC(AND_REMU_func7_subcode_list);

    static instruction_hook_td ADD_SUB_SLL_SLT_SLTU_XOR_SRL_SRA_OR_AND_func3_subcode_list[MAX_FUNC3_VALUE] = {};
        ADD_SUB_SLL_SLT_SLTU_XOR_SRL_SRA_OR_AND_func3_subcode_list[FUNC3_INSTR_ADD_SUB_MUL] = {preparation_func7, NULL, &ADD_SUB_MUL_func7_subcode_list_desc};
        ADD_SUB_SLL_SLT_SLTU_XOR_SRL_SRA_OR_AND_func3_subcode_list[FUNC3_INSTR_SLL_MULH] = {preparation_func7, NULL, &SLL_MULH_func7_subcode_list_desc};
        ADD_SUB_SLL_SLT_SLTU_XOR_SRL_SRA_OR_AND_func3_subcode_list[FUNC3_INSTR_SLT_MULHSU] = {preparation_func7, NULL, &SLT_MULHSU_func7_subcode_list_desc};
        ADD_SUB_SLL_SLT_SLTU_XOR_SRL_SRA_OR_AND_func3_subcode_list[FUNC3_INSTR_SLTU_MULHU] = {preparation_func7, NULL, &SLTU_MULHU_func7_subcode_list_desc};
        ADD_SUB_SLL_SLT_SLTU_XOR_SRL_SRA_OR_AND_func3_subcode_list[FUNC3_INSTR_XOR_DIV] = {preparation_func7, NULL, &XOR_DIV_func7_subcode_list_desc};
        ADD_SUB_SLL_SLT_SLTU_XOR_SRL_SRA_OR_AND_func3_subcode_list[FUNC3_INSTR_SRL_SRA_DIVU] = {preparation_func7, NULL, &SRL_SRA_DIVU_func7_subcode_list_desc};
        ADD_SUB_SLL_SLT_SLTU_XOR_SRL_SRA_OR_AND_func3_subcode_list[FUNC3_INSTR_OR_REM] = {preparation_func7, NULL, &OR_REM_func7_subcode_list_desc};
        ADD_SUB_SLL_SLT_SLTU_XOR_SRL_SRA_OR_AND_func3_subcode_list[FUNC3_INSTR_AND_REMU] = {preparation_func7, NULL, &AND_REMU_func7_subcode_list_desc};
    INIT_INSTRUCTION_LIST_DESC(ADD_SUB_SLL_SLT_SLTU_XOR_SRL_SRA_OR_AND_func3_subcode_list);

    #ifdef RV64
        static instruction_hook_td SRLIW_SRAIW_func7_subcode_list[MAX_FUNC7_VALUE] = {};
            SRLIW_SRAIW_func7_subcode_list[FUNC7_INSTR_SRLIW] = {NULL, instr_SRLIW, NULL};
            SRLIW_SRAIW_func7_subcode_list[FUNC7_INSTR_SRAIW] = {NULL, instr_SRAIW, NULL};
        INIT_INSTRUCTION_LIST_DESC(SRLIW_SRAIW_func7_subcode_list);

        static instruction_hook_td SLLIW_SRLIW_SRAIW_ADDIW_func3_subcode_list[MAX_FUNC3_VALUE] = {};
            SLLIW_SRLIW_SRAIW_ADDIW_func3_subcode_list[FUNC3_INSTR_SLLIW] = {NULL, instr_SLLIW, NULL};
            SLLIW_SRLIW_SRAIW_ADDIW_func3_subcode_list[FUNC3_INSTR_SRLIW_SRAIW] = {preparation_func7, NULL, &SRLIW_SRAIW_func7_subcode_list_desc};
            SLLIW_SRLIW_SRAIW_ADDIW_func3_subcode_list[FUNC3_INSTR_ADDIW] = {NULL, instr_ADDIW, NULL};
        INIT_INSTRUCTION_LIST_DESC(SLLIW_SRLIW_SRAIW_ADDIW_func3_subcode_list);

        static instruction_hook_td SRLW_SRAW_DIVUW_func7_subcode_list[MAX_FUNC7_VALUE] = {};
            SRLW_SRAW_DIVUW_func7_subcode_list[FUNC7_INSTR_SRLW] = {NULL, instr_SRLW, NULL};
            SRLW_SRAW_DIVUW_func7_subcode_list[FUNC7_INSTR_SRAW] = {NULL, instr_SRAW, NULL};
            #ifdef MULTIPLY_SUPPORT
                SRLW_SRAW_DIVUW_func7_subcode_list[FUNC7_INSTR_DIVUW] = {NULL, instr_DIVUW, NULL};
            #endif
        INIT_INSTRUCTION_LIST_DESC(SRLW_SRAW_DIVUW_func7_subcode_list);

        static instruction_hook_td ADDW_SUBW_MULW_func7_subcode_list[MAX_FUNC7_VALUE] = {};
            ADDW_SUBW_MULW_func7_subcode_list[FUNC7_INSTR_ADDW] = {NULL, instr_ADDW, NULL};
            ADDW_SUBW_MULW_func7_subcode_list[FUNC7_INSTR_SUBW] = {NULL, instr_SUBW, NULL};
            #ifdef MULTIPLY_SUPPORT
                ADDW_SUBW_MULW_func7_subcode_list[FUNC7_INSTR_MULW] = {NULL, instr_MULW, NULL};
            #endif
        INIT_INSTRUCTION_LIST_DESC(ADDW_SUBW_MULW_func7_subcode_list);

        static instruction_hook_td ADDW_SUBW_SLLW_SRLW_SRAW_MULW_DIVW_DIVUW_REMW_REMUW_func3_subcode_list[MAX_FUNC3_VALUE] = {};
            ADDW_SUBW_SLLW_SRLW_SRAW_MULW_DIVW_DIVUW_REMW_REMUW_func3_subcode_list[FUNC3_INSTR_ADDW_SUBW_MULW] = {preparation_func7, NULL, &ADDW_SUBW_MULW_func7_subcode_list_desc};
            ADDW_SUBW_SLLW_SRLW_SRAW_MULW_DIVW_DIVUW_REMW_REMUW_func3_subcode_list[FUNC3_INSTR_SLLW] = {NULL, instr_SLLW, NULL};
            ADDW_SUBW_SLLW_SRLW_SRAW_MULW_DIVW_DIVUW_REMW_REMUW_func3_subcode_list[FUNC3_INSTR_SRLW_SRAW_DIVUW] = {preparation_func7, NULL, &SRLW_SRAW_DIVUW_func7_subcode_list_desc};
            #ifdef MULTIPLY_SUPPORT
                ADDW_SUBW_SLLW_SRLW_SRAW_MULW_DIVW_DIVUW_REMW_REMUW_func3_subcode_list[FUNC3_INSTR_DIVW] = {NULL, instr_DIVW, NULL};
                ADDW_SUBW_SLLW_SRLW_SRAW_MULW_DIVW_DIVUW_REMW_REMUW_func3_subcode_list[FUNC3_INSTR_REMW] = {NULL, instr_REMW, NULL};
                ADDW_SUBW_SLLW_SRLW_SRAW_MULW_DIVW_DIVUW_REMW_REMUW_func3_subcode_list[FUNC3_INSTR_REMUW] = {NULL, instr_REMUW, NULL};
            #endif
        INIT_INSTRUCTION_LIST_DESC(ADDW_SUBW_SLLW_SRLW_SRAW_MULW_DIVW_DIVUW_REMW_REMUW_func3_subcode_list);
    #endif

    #ifdef CSR_SUPPORT
        static instruction_hook_td ECALL_EBREAK_URET_func12_sub5_subcode_list[MAX_FUNC5_VALUE] = {};
            ECALL_EBREAK_URET_func12_sub5_subcode_list[FUNC5_INSTR_ECALL] = {NULL, instr_ECALL, NULL};
            ECALL_EBREAK_URET_func12_sub5_subcode_list[FUNC5_INSTR_EBREAK] = {NULL, instr_EBREAK, NULL};
            ECALL_EBREAK_URET_func12_sub5_subcode_list[FUNC5_INSTR_URET] = {NULL, instr_URET, NULL};
        INIT_INSTRUCTION_LIST_DESC(ECALL_EBREAK_URET_func12_sub5_subcode_list);

        static instruction_hook_td SRET_WFI_func12_sub5_subcode_list[MAX_FUNC5_VALUE] = {};
            SRET_WFI_func12_sub5_subcode_list[FUNC5_INSTR_SRET] = {NULL, instr_SRET, NULL};
            SRET_WFI_func12_sub5_subcode_list[FUNC5_INSTR_WFI] = {NULL, instr_NOP, NULL};
        INIT_INSTRUCTION_LIST_DESC(SRET_WFI_func12_sub5_subcode_list);

        static instruction_hook_td ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func7_subcode_list[MAX_FUNC7_VALUE] = {};
            ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func7_subcode_list[FUNC7_INSTR_ECALL_EBREAK_URET] = {preparation_func7_func12_sub5_extended, NULL, &ECALL_EBREAK_URET_func12_sub5_subcode_list_desc};
            ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func7_subcode_list[FUNC7_INSTR_SRET_WFI] = {preparation_func7_func12_sub5_extended, NULL, &SRET_WFI_func12_sub5_subcode_list_desc};
            ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func7_subcode_list[FUNC7_INSTR_MRET] = {NULL, instr_MRET, NULL};
            ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func7_subcode_list[FUNC7_INSTR_SFENCEVMA] = {NULL, instr_NOP, NULL};
        INIT_INSTRUCTION_LIST_DESC(ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func7_subcode_list);

        static instruction_hook_td CSRRW_CSRRS_CSRRC_CSRRWI_CSRRSI_CSRRCI_ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func3_subcode_list[MAX_FUNC3_VALUE] = {};
            CSRRW_CSRRS_CSRRC_CSRRWI_CSRRSI_CSRRCI_ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func3_subcode_list[FUNC3_INSTR_CSRRW] = {NULL, instr_CSRRW, NULL};
            CSRRW_CSRRS_CSRRC_CSRRWI_CSRRSI_CSRRCI_ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func3_subcode_list[FUNC3_INSTR_CSRRS] = {NULL, instr_CSRRS, NULL};
            CSRRW_CSRRS_CSRRC_CSRRWI_CSRRSI_CSRRCI_ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func3_subcode_list[FUNC3_INSTR_CSRRC] = {NULL, instr_CSRRC, NULL};
            CSRRW_CSRRS_CSRRC_CSRRWI_CSRRSI_CSRRCI_ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func3_subcode_list[FUNC3_INSTR_CSRRWI] = {NULL, instr_CSRRWI, NULL};
            CSRRW_CSRRS_CSRRC_CSRRWI_CSRRSI_CSRRCI_ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func3_subcode_list[FUNC3_INSTR_CSRRSI] = {NULL, instr_CSRRSI, NULL};
            CSRRW_CSRRS_CSRRC_CSRRWI_CSRRSI_CSRRCI_ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func3_subcode_list[FUNC3_INSTR_CSRRCI] = {NULL, instr_CSRRCI, NULL};
            CSRRW_CSRRS_CSRRC_CSRRWI_CSRRSI_CSRRCI_ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func3_subcode_list[FUNC3_INSTR_ECALL_EBREAK_MRET_SRET_URET_WFI_SFENCEVMA] = {preparation_func7, NULL, &ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func7_subcode_list_desc};
        INIT_INSTRUCTION_LIST_DESC(CSRRW_CSRRS_CSRRC_CSRRWI_CSRRSI_CSRRCI_ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func3_subcode_list);
    #endif

    #ifdef ATOMIC_SUPPORT
        static instruction_hook_td W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[MAX_FUNC5_VALUE] = {};
            W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_LR] = {NULL, instr_LR_W, NULL};
            W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_SC] = {NULL, instr_SC_W, NULL};
            W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_SWAP] = {NULL, instr_AMOSWAP_W, NULL};
            W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_ADD] = {NULL, instr_AMOADD_W, NULL};
            W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_XOR] = {NULL, instr_AMOXOR_W, NULL};
            W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_AND] = {NULL, instr_AMOAND_W, NULL};
            W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_OR] = {NULL, instr_AMOOR_W, NULL};
            W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_MIN] = {NULL, instr_AMOMIN_W, NULL};
            W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_MAX] = {NULL, instr_AMOMAX_W, NULL};
            W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_MINU] = {NULL, instr_AMOMINU_W, NULL};
            W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_MAXU] = {NULL, instr_AMOMAXU_W, NULL};
        INIT_INSTRUCTION_LIST_DESC(W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list);

        #ifdef RV64
            static instruction_hook_td D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[MAX_FUNC5_VALUE] = {};
                D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_LR] = {NULL, instr_LR_D, NULL};
                D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_SC] = {NULL, instr_SC_D, NULL};
                D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_SWAP] = {NULL, instr_AMOSWAP_D, NULL};
                D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_ADD] = {NULL, instr_AMOADD_D, NULL};
                D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_XOR] = {NULL, instr_AMOXOR_D, NULL};
                D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_AND] = {NULL, instr_AMOAND_D, NULL};
                D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_OR] = {NULL, instr_AMOOR_D, NULL};
                D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_MIN] = {NULL, instr_AMOMIN_D, NULL};
                D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_MAX] = {NULL, instr_AMOMAX_D, NULL};
                D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_MINU] = {NULL, instr_AMOMINU_D, NULL};
                D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list[FUNC5_INSTR_AMO_MAXU] = {NULL, instr_AMOMAXU_D, NULL};
            INIT_INSTRUCTION_LIST_DESC(D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list);
        #endif

        static instruction_hook_td W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func3_subcode_list[MAX_FUNC3_VALUE] = {};

            W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func3_subcode_list[FUNC3_INSTR_W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU] = {preparation_func5, NULL, &W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list_desc};
            #ifdef RV64
                W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func3_subcode_list[FUNC3_INSTR_D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU] = {preparation_func5, NULL, &D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func5_subcode_list_desc};
            #endif
        INIT_INSTRUCTION_LIST_DESC(W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func3_subcode_list);
    #endif

    static instruction_hook_td CUSTOM_logical_func7_subcode_list[MAX_FUNC7_VALUE] = {};
    CUSTOM_logical_func7_subcode_list[FUNC7_LREAD] = {NULL, instr_LREAD, NULL};
    CUSTOM_logical_func7_subcode_list[FUNC7_LWRITE] = {NULL, instr_LWRITE, NULL};
    CUSTOM_logical_func7_subcode_list[FUNC7_LTRIM] = {NULL, instr_LTRIM, NULL};
    INIT_INSTRUCTION_LIST_DESC(CUSTOM_logical_func7_subcode_list);

    static instruction_hook_td CUSTOM_physical_func7_subcode_list[MAX_FUNC7_VALUE] = {};
    CUSTOM_physical_func7_subcode_list[FUNC7_PREAD] = {NULL, instr_PREAD, NULL};
    CUSTOM_physical_func7_subcode_list[FUNC7_PWRITE] = {NULL, instr_PWRITE, NULL};
    CUSTOM_physical_func7_subcode_list[FUNC7_PERASE] = {NULL, instr_PERASE, NULL};
    INIT_INSTRUCTION_LIST_DESC(CUSTOM_physical_func7_subcode_list);

    static instruction_hook_td CUSTOM_soc_interface_func7_subcode_list[MAX_FUNC7_VALUE] = {};
    CUSTOM_soc_interface_func7_subcode_list[FUNC7_READBUFF] = {NULL, instr_READBUFF, NULL};
    CUSTOM_soc_interface_func7_subcode_list[FUNC7_STARTSIM] = {NULL, instr_STARTSIM, NULL};
    CUSTOM_soc_interface_func7_subcode_list[FUNC7_STOPSIM] = {NULL, instr_STOPSIM, NULL};
    CUSTOM_soc_interface_func7_subcode_list[FUNC7_NEXTSIMTICK] = {NULL, instr_NEXTSIMTICK, NULL};
    CUSTOM_soc_interface_func7_subcode_list[FUNC7_PUTC] = {NULL, instr_PUTC, NULL};
    CUSTOM_soc_interface_func7_subcode_list[FUNC7_WRITEBUFF] = {NULL, instr_WRITEBUFF, NULL};
    CUSTOM_soc_interface_func7_subcode_list[FUNC7_DRAM_READ] = {NULL, instr_DRAMREAD, NULL};
    CUSTOM_soc_interface_func7_subcode_list[FUNC7_DRAM_WRITE] = {NULL, instr_DRAMWRITE, NULL};
    INIT_INSTRUCTION_LIST_DESC(CUSTOM_soc_interface_func7_subcode_list);

    static instruction_hook_td CUSTOM_func3_subcode_list[MAX_FUNC3_VALUE] = {};
    CUSTOM_func3_subcode_list[FUNC3_LOGADDR] = {preparation_func7, NULL, &CUSTOM_logical_func7_subcode_list_desc};
    CUSTOM_func3_subcode_list[FUNC3_PHYSADDR] = {preparation_func7, NULL, &CUSTOM_physical_func7_subcode_list_desc};
    CUSTOM_func3_subcode_list[FUNC3_SOC_INTERFACE] = {preparation_func7, NULL, &CUSTOM_soc_interface_func7_subcode_list_desc};
    INIT_INSTRUCTION_LIST_DESC(CUSTOM_func3_subcode_list);

    RV_opcode_list[INSTR_LUI] = {U_type_preparation, instr_LUI, NULL};
    RV_opcode_list[INSTR_AUIPC] = {U_type_preparation, instr_AUIPC, NULL};
    RV_opcode_list[INSTR_JAL] = {J_type_preparation, instr_JAL, NULL};
    RV_opcode_list[INSTR_JALR] = {I_type_preparation, NULL, &JALR_func3_subcode_list_desc};
    RV_opcode_list[INSTR_BEQ_BNE_BLT_BGE_BLTU_BGEU] = {B_type_preparation, NULL, &BEQ_BNE_BLT_BGE_BLTU_BGEU_func3_subcode_list_desc};
    RV_opcode_list[INSTR_LB_LH_LW_LBU_LHU_LWU_LD] = {I_type_preparation, NULL, &LB_LH_LW_LBU_LHU_LWU_LD_func3_subcode_list_desc};
    RV_opcode_list[INSTR_SB_SH_SW_SD] = {S_type_preparation, NULL, &SB_SH_SW_SD_func3_subcode_list_desc};
    RV_opcode_list[INSTR_ADDI_SLTI_SLTIU_XORI_ORI_ANDI_SLLI_SRLI_SRAI] = {I_type_preparation, NULL, &ADDI_SLTI_SLTIU_XORI_ORI_ANDI_SLLI_SRLI_SRAI_func3_subcode_list_desc};
    RV_opcode_list[INSTR_ADD_SUB_SLL_SLT_SLTU_XOR_SRL_SRA_OR_AND_MUL_MULH_MULHSU_MULHU_DIV_DIVU_REM_REMU] = {R_type_preparation, NULL, &ADD_SUB_SLL_SLT_SLTU_XOR_SRL_SRA_OR_AND_func3_subcode_list_desc};
    RV_opcode_list[INSTR_FENCE_FENCE_I] = {NULL, instr_NOP, NULL};
    RV_opcode_list[INSTR_CSD] = {R_type_preparation, NULL, &CUSTOM_func3_subcode_list_desc};
    RV_opcode_list[INSTR_FLT_ARITH] = {R_float_type_preparation, NULL, NULL}; // a little to difficult to encode this in map format
    RV_opcode_list[INSTR_FLT_LOAD] = {I_type_preparation, instr_FLW, NULL};
    RV_opcode_list[INSTR_FLT_STORE] = {S_type_preparation, instr_FSW, NULL};
    RV_opcode_list[INSTR_FMADD] = {R4_type_preparation, instr_FMADD, NULL};
    RV_opcode_list[INSTR_FMSUB] = {R4_type_preparation, instr_FMSUB, NULL};
    RV_opcode_list[INSTR_FMNSUB] = {R4_type_preparation, instr_FMNSUB, NULL};
    RV_opcode_list[INSTR_FMNADD] = {R4_type_preparation, instr_FMNADD, NULL};
    #ifdef RV64
        RV_opcode_list[INSTR_ADDIW_SLLIW_SRLIW_SRAIW] = {I_type_preparation, NULL, &SLLIW_SRLIW_SRAIW_ADDIW_func3_subcode_list_desc};
        RV_opcode_list[INSTR_ADDW_SUBW_SLLW_SRLW_SRAW_MULW_DIVW_DIVUW_REMW_REMUW] = {R_type_preparation, NULL, &ADDW_SUBW_SLLW_SRLW_SRAW_MULW_DIVW_DIVUW_REMW_REMUW_func3_subcode_list_desc};
    #endif

    #ifdef CSR_SUPPORT
        RV_opcode_list[INSTR_ECALL_EBREAK_MRET_SRET_URET_WFI_CSRRW_CSRRS_CSRRC_CSRRWI_CSRRSI_CSRRCI_SFENCEVMA] = {I_type_preparation, NULL, &CSRRW_CSRRS_CSRRC_CSRRWI_CSRRSI_CSRRCI_ECALL_EBREAK_URET_SRET_MRET_WFI_SFENCEVMA_func3_subcode_list_desc};
    #endif

    #ifdef ATOMIC_SUPPORT
        RV_opcode_list[INSTR_AMO_W_D_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU] = {R_type_preparation, NULL, &W_LR_SC_SWAP_ADD_XOR_AND_OR_MIN_MAX_MINU_MAXU_func3_subcode_list_desc};
    #endif

}

static void rv_call_from_opcode_list(Core *rv_core, instruction_desc_td *opcode_list_desc, uint32_t opcode)
{
    int32_t next_subcode = -1;
    unsigned int list_size = opcode_list_desc->instruction_hook_list_size;
    instruction_hook_td *opcode_list = opcode_list_desc->instruction_hook_list;

    if( (opcode_list[opcode].preparation_cb == NULL) &&
        (opcode_list[opcode].execution_cb == NULL) &&
        (opcode_list[opcode].next == NULL) ) 
        {
            printf("No preparation or execution callback for opcode %d\n", opcode);
            die_msg("Unknown instruction: %lu PC: %lu Cycle: %lu\n", rv_core->instruction, rv_core->pc, rv_core->curr_cycle);
    } 
    if(opcode >= list_size) {
        die_msg("Unknown instruction: %lu PC: %lu Cycle: %lu\n", rv_core->instruction, rv_core->pc, rv_core->curr_cycle);
    }

    if(opcode_list[opcode].preparation_cb != NULL)
        opcode_list[opcode].preparation_cb(rv_core, &next_subcode);

    if(opcode_list[opcode].execution_cb != NULL)
        rv_core->execute_cb = opcode_list[opcode].execution_cb;

    if((next_subcode != -1) && (opcode_list[opcode].next != NULL))
        rv_call_from_opcode_list(rv_core, opcode_list[opcode].next, next_subcode);
}

#ifdef CSR_SUPPORT
    static inline void rv_core_update_interrupts(Core *rv_core, uint8_t mei, uint8_t mti, uint8_t msi)
    {
        trap_set_pending_bits(&rv_core->trap, mei, mti, msi);
    }

    static inline uint8_t rv_core_prepare_interrupts(Core *rv_core)
    {
        trap_cause_interrupt interrupt_cause = (trap_cause_interrupt)0;
        trap_ret trap_retval = (trap_ret)0;
        privilege_level serving_priv_level = machine_mode;

        /* Privilege Spec: "Multiple simultaneous interrupts and traps at the same privilege level are handled in the following
         * decreasing priority order: external interrupts, software interrupts, timer interrupts, then finally any
         * synchronous traps."
         *
         * NOTE: We actually don't use this priority order here. The problem is, that if an interrupt and a synchronous
         * trap is active at the same cycle the interrupt will be handled first and in the next cycle immediately the synchronous trap
         * (as they will also occur when interrupts are globally disabled) which will potentially disrupt the privilige level at which
         * the synchronous trap should actually be handled. (This issue actually happenend when a user ecall and a timer IRQ occured at the same cycle).
         * So we use another prio order to circumvent this:
         * We handle synchronous traps at the highest prio. (Interrupts will be disabled during the handling, regardless if it is a sync trap or a IRQ)
         * This ensures atomicity among sync traps and Interrupts.
         *
         * Possible solution for this: we could remember pending sync traps for each priv level separately. And handle them only if we are in the
         * appropriate priv level. Anyway for now we just keep it simple like this, as except that we don't follow the spec here 100% it works just fine.
         * Furthermore simultanious interrupts at the same cycle should be very rare anyway.
         */
        if(rv_core->sync_trap_pending)
        {
            serving_priv_level = trap_check_exception_delegation(&rv_core->trap, rv_core->curr_priv_mode, (trap_cause_exception) rv_core->sync_trap_cause);

            // printf("exception! serving priv: %d cause %d edeleg %x curr priv mode %x cycle %ld\n", serving_priv_level, rv_core->sync_trap_cause, *rv_core->trap.m.regs[trap_reg_edeleg], rv_core->curr_priv_mode, rv_core->curr_cycle);
            // printf("exception! serving: %x curr priv %x "PRINTF_FMT" "PRINTF_FMT" pc: "PRINTF_FMT"\n", serving_priv_level, rv_core->curr_priv_mode, rv_core->sync_trap_cause, *rv_core->trap.m.regs[trap_reg_status], rv_core->pc);
            rv_core->pc = trap_serve_interrupt(&rv_core->trap, serving_priv_level, rv_core->curr_priv_mode, 0, rv_core->sync_trap_cause, rv_core->pc, rv_core->sync_trap_tval);
            rv_core->curr_priv_mode = serving_priv_level;
            rv_core->sync_trap_pending = 0;
            rv_core->sync_trap_cause = 0;
            rv_core->sync_trap_tval = 0;
            return 1;
        }

        for (int interrupt_cause = trap_cause_machine_exti;
            interrupt_cause >= trap_cause_user_swi;
            --interrupt_cause)
       {
           trap_cause_interrupt cause = static_cast<trap_cause_interrupt>(interrupt_cause);
       
           trap_retval = trap_check_interrupt_pending(
               &rv_core->trap,
               rv_core->curr_priv_mode,
               cause,
               &serving_priv_level
           );
       
           if (trap_retval) {
               rv_core->pc = trap_serve_interrupt(
                   &rv_core->trap,
                   serving_priv_level,
                   rv_core->curr_priv_mode,
                   1,  // is_interrupt = 1
                   cause,
                   rv_core->pc,
                   rv_core->sync_trap_tval
               );
               rv_core->curr_priv_mode = serving_priv_level;
               return 1;
           }
       }

        return 0;
    }
#endif

static inline rv_word_t rv_core_fetch(Core *rv_core)
{
    rv_word_t addr = rv_core->pc;
    return mmu_checked_bus_access(rv_core, rv_core->curr_priv_mode, bus_instr_access, addr, &rv_core->instruction, sizeof(rv_word_t));
}

static inline rv_word_t rv_core_decode(Core *rv_core)
{
    rv_core->opcode = (rv_core->instruction & 0x7F);
    rv_core->rd = 0;
    rv_core->rs1 = 0;
    rv_core->rs2 = 0;
    rv_core->func3 = 0;
    rv_core->func7 = 0;
    rv_core->immediate = 0;
    rv_core->jump_offset = 0;
    rv_call_from_opcode_list(rv_core, &RV_opcode_list_desc, rv_core->opcode);
    return 0;
}

static uint64_t rv_core_execute(Core *rv_core)
{
    uint64_t next_cycle = rv_core->execute_cb(rv_core);

    /* clear x0 if any instruction has written into it */
    rv_core->reg_file[0] = 0;

    return next_cycle;
}

/******************* Public functions *******************************/

void Core::hardware_context_switch() {
    uint64_t temp_pc = next_pc;
    next_pc = context_switch_next_pc;
    context_switch_next_pc = next_pc ? next_pc : pc + 4;
    rv_word_t temp_regs[NR_RVI_REGS];
    for (int i = 0; i < NR_RVI_REGS; i++)
    {
        temp_regs[i] = reg_file[i];
        reg_file[i] = context_switch_regs[i];
        context_switch_regs[i] = temp_regs[i];
    }
    double temp_fregs[NR_RVF_REGS];
    for (int i = 0; i < NR_RVF_REGS; i++)
    {
        temp_fregs[i] = float_reg_file[i];
        float_reg_file[i] = context_switch_fregs[i];
        context_switch_fregs[i] = temp_fregs[i];
    }
    in_csd_mode = !in_csd_mode;
    csd_cycles = 0;
}

void Core::init_csd_job(CSDData *job_info) {
    csd_cycles = 0;
    for (int i = 0; i < NR_RVI_REGS; i++)
    {
        context_switch_regs[i] = 0;
    }
    for (int i = 0; i < NR_RVF_REGS; i++)
    {
        context_switch_fregs[i] = 0.0;
    }
    in_csd_mode = false; // gets switched on hardware context switch
    context_switch_next_pc = job_info->start_pc;
    context_switch_regs[10] = job_info->ext2;
    context_switch_regs[11] = job_info->allocator;
    context_switch_regs[2] = reg_file[2]; // save stack pointer
    csd_start_pc = job_info->start_pc;
    csd_job_loaded = true;
    csd_job_finished = false;
    hardware_context_switch();
}

void Core::clean_csd_job() {
    in_csd_mode = false;
    csd_job_loaded = false;
    context_switch_next_pc = 0;
    for (int i = 0; i < NR_RVI_REGS; i++)
    {
        context_switch_regs[i] = 0;
    }
    for (int i = 0; i < NR_RVF_REGS; i++)
    {
        context_switch_fregs[i] = 0.0;
    }
    csd_cycles = 0;
    csd_job_finished = false;
}

uint64_t Core::rv_core_run()
{
    next_pc = 0;
    uint64_t next_cycle = 1;
    if(rv_core_fetch(this))
    {
        rv_core_decode(this);
        next_cycle = rv_core_execute(this);
    }

    /* increase program counter here */
    pc = next_pc ? next_pc : pc + 4;
    
    curr_cycle += next_cycle;
    if (in_csd_mode) {
        printf("running csd mode for pc: %lx and csd cycles: %lu and start pc: %lx and instruction: %x\n", pc, csd_cycles, csd_start_pc, instruction);
        csd_cycles += next_cycle;
        if (csd_cycles >= CSD_CYCLES_LIMIT) {
            hardware_context_switch();
        }
    }
    if (reg_file[2] < stack_bottom_min) {
        die_msg("Stack overflow detected! Stack pointer: %lu Stack bottom: %lu\n",
                reg_file[2], stack_bottom_min);
    }
    /**
    csr_regs[CSR_ADDR_MCYCLE].value = curr_cycle;
    csr_regs[CSR_ADDR_MINSTRET].value = curr_cycle;
    csr_regs[CSR_ADDR_CYCLE].value = curr_cycle;
    csr_regs[CSR_ADDR_TIME].value = curr_cycle;

    #ifndef RV64
        csr_regs[CSR_ADDR_MCYCLEH].value = curr_cycle >> 32;
        csr_regs[CSR_ADDR_MINSTRETH].value = curr_cycle >> 32;
        csr_regs[CSR_ADDR_CYCLEH].value = curr_cycle >> 32;
        csr_regs[CSR_ADDR_TIMEH].value = curr_cycle >> 32;
    #endif*/
    return next_cycle;
}

void Core::rv_core_process_interrupts(uint8_t mei, uint8_t mti, uint8_t msi)
{
    #ifdef CSR_SUPPORT
        /* interrupt handling */
        rv_core_update_interrupts(this, mei, mti, msi);
        rv_core_prepare_interrupts(this);
    #else
        (void)rv_core;
        (void)mei;
        (void)msi;
        (void)mti;
    #endif
}

void Core::rv_core_reg_dump()
{

    int i = 0;

    DEBUG_PRINT("pc: " PRINTF_FMT "\n", rv_core->pc);
    DEBUG_PRINT("instr: %08x\n", rv_core->instruction);
    for(i=0;i<NR_RVI_REGS;i++)
    {
        DEBUG_PRINT("reg_file[%2d]: " PRINTF_FMT "\n", i, rv_core->reg_file[i]);
    }
}

void rv_core_reg_dump_more_regs(Core *rv_core)
{
    (void) rv_core;

    DEBUG_PRINT("internal regs after execution:\n");
    DEBUG_PRINT("instruction: %x\n", rv_core->instruction);
    DEBUG_PRINT("rd: %x rs1: %x rs2: %x imm: "PRINTF_FMT"\n", rv_core->rd, rv_core->rs1, rv_core->rs2, rv_core->immediate);
    DEBUG_PRINT("func3: %x func7: %x jump_offset "PRINTF_FMT"\n", rv_core->func3, rv_core->func7, rv_core->jump_offset);
    DEBUG_PRINT("next pc: "PRINTF_FMT"\n", rv_core->pc);
    DEBUG_PRINT("\n");
}

static void rv_core_init_csr_regs(Core *rv_core)
{
    uint16_t i = 0;
    rv_word_t xstatus_warl_bits = 0;

    #ifdef RV64
        xstatus_warl_bits = (CSR_XLEN_64_BIT << CSR_UXL_BIT_BASE) | (CSR_XLEN_64_BIT << CSR_SXL_BIT_BASE);
    #endif

    // floating point regs
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, CSR_ADDR_FFLAGS, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode) | CSR_ACCESS_RW(user_mode), 0, CSR_FFLAGS_MASK, CSR_MASK_ZERO);
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, CSR_ADDR_FRM, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode) | CSR_ACCESS_RW(user_mode), 0, CSR_FRM_MASK, CSR_MASK_ZERO);
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, CSR_ADDR_FCSR, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode) | CSR_ACCESS_RW(user_mode), 0, CSR_MASK_WR_ALL, CSR_MASK_ZERO);
   

    /* Machine Information Registers */
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, CSR_ADDR_MVENDORID, CSR_ACCESS_RO(machine_mode), 0, CSR_MASK_ZERO, CSR_MASK_ZERO);
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, CSR_ADDR_MARCHID, CSR_ACCESS_RO(machine_mode), 0, CSR_MASK_ZERO, CSR_MASK_ZERO);
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, CSR_ADDR_MIMPID, CSR_ACCESS_RO(machine_mode), 0, CSR_MASK_ZERO, CSR_MASK_ZERO);
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, CSR_ADDR_MHARTID, CSR_ACCESS_RO(machine_mode), 0, CSR_MASK_ZERO, CSR_MASK_ZERO);

    /* Machine Trap Setup */
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_MSTATUS, CSR_ACCESS_RW(machine_mode), CSR_MSTATUS_MASK, xstatus_warl_bits, &rv_core->trap, trap_m_read, trap_m_write, trap_reg_status);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_MISA, CSR_ACCESS_RO(machine_mode), CSR_MASK_WR_ALL, CSR_MASK_ZERO, &rv_core->trap, trap_m_read, trap_m_write, trap_reg_isa);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_MEDELEG, CSR_ACCESS_RW(machine_mode), CSR_MEDELEG_MASK, CSR_MASK_ZERO, &rv_core->trap, trap_m_read, trap_m_write, trap_reg_edeleg);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_MIDELEG, CSR_ACCESS_RW(machine_mode), CSR_MIDELEG_MASK, CSR_MASK_ZERO, &rv_core->trap, trap_m_read, trap_m_write, trap_reg_ideleg);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_MIE, CSR_ACCESS_RW(machine_mode), CSR_MIP_MIE_MASK, CSR_MASK_ZERO, &rv_core->trap, trap_m_read, trap_m_write, trap_reg_ie);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_MTVEC, CSR_ACCESS_RW(machine_mode), CSR_MTVEC_MASK, CSR_MASK_ZERO, &rv_core->trap, trap_m_read, trap_m_write, trap_reg_tvec);
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, CSR_ADDR_MCOUNTEREN, CSR_ACCESS_RW(machine_mode), 0, CSR_MASK_ZERO, CSR_MASK_ZERO);

    /* Machine Trap Handling */
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_MSCRATCH, CSR_ACCESS_RW(machine_mode), CSR_MASK_WR_ALL, CSR_MASK_ZERO, &rv_core->trap, trap_m_read, trap_m_write, trap_reg_scratch);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_MEPC, CSR_ACCESS_RW(machine_mode), CSR_MASK_WR_ALL, CSR_MASK_ZERO, &rv_core->trap, trap_m_read, trap_m_write, trap_reg_epc);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_MCAUSE, CSR_ACCESS_RW(machine_mode), CSR_MASK_WR_ALL, CSR_MASK_ZERO, &rv_core->trap, trap_m_read, trap_m_write, trap_reg_cause);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_MTVAL, CSR_ACCESS_RW(machine_mode), CSR_MASK_WR_ALL, CSR_MASK_ZERO, &rv_core->trap, trap_m_read, trap_m_write, trap_reg_tval);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_MIP, CSR_ACCESS_RW(machine_mode), CSR_MIP_MIE_MASK, CSR_MASK_ZERO, &rv_core->trap, trap_m_read, trap_m_write, trap_reg_ip);

    /* Set supported ISA Extension bits */
    *rv_core->trap.m.regs[trap_reg_isa] = RV_SUPPORTED_EXTENSIONS;
    #ifdef RV64
        *rv_core->trap.m.regs[trap_reg_isa] |= (CSR_XLEN_64_BIT << (XLEN-2));
    #else
        *rv_core->trap.m.regs[trap_reg_isa] |= (CSR_XLEN_32_BIT << (XLEN-2));
    #endif

    /* Machine Protection and Translation */
    for(i=0;i<PMP_NR_CFG_REGS;i++)
    {
        #ifdef PMP_SUPPORT
            INIT_CSR_REG_SPECIAL(rv_core->csr_regs, (CSR_PMPCFG0+i), CSR_ACCESS_RW(machine_mode), CSR_MASK_WR_ALL, CSR_MASK_ZERO, &rv_core->pmp, pmp_read_csr_cfg, pmp_write_csr_cfg, i);
        #else
            INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_PMPCFG0+i), CSR_ACCESS_RW(machine_mode), 0, CSR_MASK_WR_ALL, CSR_MASK_ZERO);
        #endif
    }

    /* All others are WARL */
    for(i=PMP_NR_CFG_REGS;i<PMP_NR_CFG_REGS_WARL_MAX;i++)
        INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_PMPCFG0+i), CSR_ACCESS_RO(machine_mode), 0, CSR_MASK_ZERO, CSR_MASK_ZERO);

    for(i=0;i<PMP_NR_ADDR_REGS;i++)
    {
        #ifdef PMP_SUPPORT
            INIT_CSR_REG_SPECIAL(rv_core->csr_regs, (CSR_PMPADDR0+i), CSR_ACCESS_RW(machine_mode), CSR_MASK_WR_ALL, CSR_MASK_ZERO, &rv_core->pmp, pmp_read_csr_addr, pmp_write_csr_addr, i);
        #else
            INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_PMPADDR0+i), CSR_ACCESS_RW(machine_mode), 0, CSR_MASK_WR_ALL, CSR_MASK_ZERO);
        #endif
    }

    /* All others are WARL */
    for(i=PMP_NR_ADDR_REGS;i<PMP_NR_ADDR_REGS_WARL_MAX;i++)
        INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_PMPADDR0+i), CSR_ACCESS_RO(machine_mode), 0, CSR_MASK_ZERO, CSR_MASK_ZERO);

    /* Supervisor Trap Setup */
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_SSTATUS, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode), CSR_SSTATUS_MASK, CSR_MASK_ZERO, &rv_core->trap, trap_s_read, trap_s_write, trap_reg_status);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_SEDELEG, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode), CSR_SEDELEG_MASK, CSR_MASK_ZERO, &rv_core->trap, trap_s_read, trap_s_write, trap_reg_edeleg);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_SIDELEG, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode), CSR_SIDELEG_MASK, CSR_MASK_ZERO, &rv_core->trap, trap_s_read, trap_s_write, trap_reg_ideleg);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_SIE, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode), CSR_SIP_SIE_MASK, CSR_MASK_ZERO, &rv_core->trap, trap_s_read, trap_s_write, trap_reg_ie);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_STVEC, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode), CSR_STVEC_MASK, CSR_MASK_ZERO, &rv_core->trap, trap_s_read, trap_s_write, trap_reg_tvec);
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, CSR_ADDR_SCOUNTEREN, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode), 0, CSR_MASK_ZERO, CSR_MASK_ZERO);

    /* Supervisor Trap Setup */
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_SSCRATCH, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode), CSR_MASK_WR_ALL, CSR_MASK_ZERO, &rv_core->trap, trap_s_read, trap_s_write, trap_reg_scratch);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_SEPC, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode), CSR_MASK_WR_ALL, CSR_MASK_ZERO, &rv_core->trap, trap_s_read, trap_s_write, trap_reg_epc);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_SCAUSE, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode), CSR_MASK_WR_ALL, CSR_MASK_ZERO, &rv_core->trap, trap_s_read, trap_s_write, trap_reg_cause);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_STVAL, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode), CSR_MASK_WR_ALL, CSR_MASK_ZERO, &rv_core->trap, trap_s_read, trap_s_write, trap_reg_tval);
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_SIP, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode), CSR_SIP_SIE_MASK, CSR_MASK_ZERO, &rv_core->trap, trap_s_read, trap_s_write, trap_reg_ip);

    /* Supervisor Address Translation and Protection */
    INIT_CSR_REG_SPECIAL(rv_core->csr_regs, CSR_ADDR_SATP, CSR_ACCESS_RW(machine_mode) | CSR_ACCESS_RW(supervisor_mode), CSR_SATP_MASK, CSR_MASK_ZERO, &rv_core->mmu, mmu_read_csr, mmu_write_csr, 0);

    /* Performance Counters */
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_ADDR_MCYCLE), CSR_ACCESS_RW(machine_mode), 0, CSR_MASK_WR_ALL, CSR_MASK_ZERO);
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_ADDR_MCYCLEH), CSR_ACCESS_RW(machine_mode), 0, CSR_MASK_WR_ALL, CSR_MASK_ZERO);
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_ADDR_MINSTRET), CSR_ACCESS_RW(machine_mode), 0, CSR_MASK_WR_ALL, CSR_MASK_ZERO);
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_ADDR_MINSTRETH), CSR_ACCESS_RW(machine_mode), 0, CSR_MASK_WR_ALL, CSR_MASK_ZERO);

    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_ADDR_CYCLE), CSR_ACCESS_RO(machine_mode) | CSR_ACCESS_RO(supervisor_mode) | CSR_ACCESS_RO(user_mode), 0, CSR_MASK_WR_ALL, CSR_MASK_ZERO);
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_ADDR_CYCLEH), CSR_ACCESS_RO(machine_mode) | CSR_ACCESS_RO(supervisor_mode) | CSR_ACCESS_RO(user_mode), 0, CSR_MASK_WR_ALL, CSR_MASK_ZERO);
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_ADDR_TIME), CSR_ACCESS_RO(machine_mode) | CSR_ACCESS_RO(supervisor_mode) | CSR_ACCESS_RO(user_mode), 0, CSR_MASK_WR_ALL, CSR_MASK_ZERO);
    INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_ADDR_TIMEH), CSR_ACCESS_RO(machine_mode) | CSR_ACCESS_RO(supervisor_mode) | CSR_ACCESS_RO(user_mode), 0, CSR_MASK_WR_ALL, CSR_MASK_ZERO);

    /* All others are WARL, they start at 3 */
    for(i=3;i<CSR_HPMCOUNTER_WARL_MAX;i++)
        INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_ADDR_MCYCLE+i), CSR_ACCESS_RO(machine_mode), 0, CSR_MASK_ZERO, CSR_MASK_ZERO);
        INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_ADDR_CYCLE+i), CSR_ACCESS_RO(machine_mode) | CSR_ACCESS_RO(supervisor_mode) | CSR_ACCESS_RO(user_mode), 0, CSR_MASK_ZERO, CSR_MASK_ZERO);

    for(i=3;i<CSR_HPMCOUNTER_WARL_MAX;i++)
        INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_ADDR_MCYCLEH+i), CSR_ACCESS_RW(machine_mode), 0, CSR_MASK_ZERO, CSR_MASK_ZERO);
        INIT_CSR_REG_DEFAULT(rv_core->csr_regs, (CSR_ADDR_CYCLEH+i), CSR_ACCESS_RO(machine_mode) | CSR_ACCESS_RO(supervisor_mode) | CSR_ACCESS_RO(user_mode), 0, CSR_MASK_ZERO, CSR_MASK_ZERO);
}

Core::Core(SOC *soc, bus_access_func bus_acc, uint64_t cid) : pSOC(soc), bus_access(bus_acc), core_id(cid)
{
    init_instruction_hooks();
    curr_priv_mode = machine_mode;
    pc = MROM_BASE_ADDR;
    printf("beginning pc is: %lu", pc);
    trap_init(&trap);
    mmu_init(&mmu, pmp_checked_bus_access, (void *)this);
    rv_core_init_csr_regs(this);
}

void Core::getStatList(std::vector<Stats> &list, std::string prefix) {
    Stats temp;
    temp.name = prefix + ".mutex_accquires";
    temp.desc = "Number of mutex accquires";
    list.push_back(temp);
    temp.name = prefix + ".mutex_cycles";
    temp.desc = "Cycles spent in mutex accquires";
    list.push_back(temp);
    temp.name = prefix + ".csd_cycles";
    temp.desc = "Cycles spent in CSD mode";
    list.push_back(temp);
    temp.name = prefix + ".csd_jobs";
    temp.desc = "Number of CSD jobs";
    list.push_back(temp);
}

void Core::getStatValues(std::vector<double> &values) {
    values.push_back(mutex_lock_acquires);
    values.push_back(mutex_lock_acquire_cycles);
    values.push_back(csd_cycles);
    values.push_back(csd_jobs);
}

void Core::resetStatValues() {
    mutex_lock_acquires = 0;
    mutex_lock_acquire_cycles = 0;
    csd_cycles = 0;
    csd_jobs = 0;
}

} // namespace RISCV

} // namespace CPU

} // namespace SimpleSSD 
 
