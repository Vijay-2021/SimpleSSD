#ifndef RISCV_CORE_H
#define RISCV_CORE_H

#include "riscv_types.hh"


#include "rv_src/core/pmp/pmp.hh"
#include "rv_src/core/trap/trap.hh"
#include "rv_src/peripherals/clint/clint.hh"
#include "rv_src/core/csr/csr.hh"
#include "rv_src/core/mmu/mmu.hh"

#define NR_RVI_REGS 32
#define NR_RVF_REGS 32

#define INIT_INSTRUCTION_LIST_DESC(_instruction_list) \
    static instruction_desc_td  _instruction_list##_desc = \
    { sizeof(_instruction_list)/sizeof(_instruction_list[0]), _instruction_list }

namespace SimpleSSD {

namespace CPU {

namespace RISCV {

class SOC;

struct ProcessStat {
    uint64_t branch;
    uint64_t load;
    uint64_t store;
    uint64_t arithmetic;
    uint64_t floatingPoint;
    uint64_t otherInsts;
    uint64_t cycles_run;
    char* process_name;
};


class Core {
    public: 
        privilege_level curr_priv_mode;
        uint64_t curr_cycle;

        /* Registers */
        rv_word_t reg_file[NR_RVI_REGS];
        double float_reg_file[NR_RVF_REGS];
        rv_word_t pc;
        rv_word_t next_pc;

        uint32_t instruction;
        uint8_t opcode;
        uint8_t rd;
        uint8_t rs1;
        uint8_t rs2;
        uint8_t rs3; // for fmadd, fmsub, etc
        uint8_t func3;
        uint8_t func7;
        uint8_t func6;
        uint8_t func5;
        uint16_t func12;
        uint8_t rm; // store the rounding mode for fp opps
        rv_word_t immediate;
        rv_word_t jump_offset;

        uint8_t sync_trap_pending;
        rv_word_t sync_trap_cause;
        rv_word_t sync_trap_tval;

        /* points to the next instruction */
        uint64_t (*execute_cb)(Core *rv_core);

        /* externally hooked */
        SOC *pSOC;
        bus_access_func bus_access;
        // bus_read_mem read_mem;
        // bus_write_mem write_mem;

        csr_reg_td csr_regs[CSR_ADDR_MAX];
        pmp_td pmp;
        trap_td trap;
        mmu_td mmu;

        int lr_valid;
        rv_word_t lr_address;
        uint64_t core_id;

        Core(SOC *soc, bus_access_func bus_acc, uint64_t cid);

        uint64_t rv_core_run();
        void rv_core_process_interrupts(uint8_t mei, uint8_t mti, uint8_t msi);
        void rv_core_reg_dump();
        void rv_core_reg_dump_more_regs();

};

typedef struct instruction_hook_struct
{
    void (*preparation_cb)(Core *rv_core, int32_t *next_subcode);
    uint64_t (*execution_cb)(Core *rv_core_data);
    struct instruction_desc_struct *next;

} instruction_hook_td;

typedef struct instruction_desc_struct
{
    unsigned int instruction_hook_list_size;
    instruction_hook_td *instruction_hook_list;

} instruction_desc_td;

} // namespace RISCV

} // namespace CPU

} // namespace SimpleSSD

#endif /* RISCV_CORE_H */
