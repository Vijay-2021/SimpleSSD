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

struct CSDData {
    void *allocator;
    void *ext2;
    uint64_t start_pc;
};


class Core : StatObject{
    public: 
        privilege_level curr_priv_mode;
        uint64_t curr_cycle;

        /* Registers */
        rv_word_t reg_file[NR_RVI_REGS];
        double float_reg_file[NR_RVF_REGS];
        rv_word_t pc;
        rv_word_t next_pc;

        rv_word_t context_switch_next_pc; // the pc to switch to when context switching(we store next pc so we don't trigger hardware context switch again)
        rv_word_t context_switch_regs[NR_RVI_REGS]; // save the register file for context switching
        double context_switch_fregs[NR_RVF_REGS]; // save the float register file for context switching

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
        rv_word_t stack_bottom_min = 0;
        Core(SOC *soc, bus_access_func bus_acc, uint64_t cid);

        uint64_t rv_core_run();
        void rv_core_process_interrupts(uint8_t mei, uint8_t mti, uint8_t msi);
        void rv_core_reg_dump();
        void rv_core_reg_dump_more_regs();
        void hardware_context_switch();
        void init_csd_job(CSDData *job_info);
        void clean_csd_job(); 
        bool in_csd_mode = false;
        uint64_t csd_cycles = 0;
        bool csd_job_loaded = false;
        bool csd_job_finished = false;
        uint64_t csd_jobs = 0;
        uint64_t csd_start_pc = 0;
        uint64_t mutex_lock_acquires = 0;
        uint64_t mutex_lock_acquire_cycles = 0;
        uint64_t mutex_lock_acquire_start = 0;

        bool should_stal = false;

        void getStatList(std::vector<Stats> &stats, std::string prefix) override;
        void getStatValues(std::vector<double> &values) override;
        void resetValues() override;

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
