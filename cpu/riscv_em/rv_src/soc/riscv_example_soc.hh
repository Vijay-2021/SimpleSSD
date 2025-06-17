#ifndef RISCV_EXAMPLE_SOC_H
#define RISCV_EXAMPLE_SOC_H

#include "rv_src/core/riscv_types.hh"
#include "rv_src/core/core.hh"
#include "rv_src/peripherals/plic/plic.hh"
#include "rv_src/peripherals/uart/simple_uart.hh"

#include <vector>

namespace SimpleSSD {

namespace DRAM {
    class AbstractDRAM; 
}

namespace CPU {

class CPU;

namespace RISCV {

class SOC {
    public: 
        SOC(char *fw_file_name, char *dtb_file_name, char *initrd_file_name, CPU *cpu, uint32_t num_cores, DRAM::AbstractDRAM *dram);
        ~SOC();
        void rv_soc_dump_mem();
        void rv_soc_run(rv_word_t success_pc, uint64_t num_cycles); // this depends on the stored number of cycles in the core
        void rv_soc_tick(rv_word_t success_pc, uint64_t num_cycles); // this just runs for the given number of cycles
        void rv_soc_init_mem_access_cbs();
        void rv_soc_fs_init(); 
        void rv_soc_add_task(char *input_cmd);
        void rv_soc_schedule();
        void rv_soc_deschedule();
        void rv_soc_schedule_next();
        uint64_t get_period(); 
        uint8_t* get_ram();
        uint64_t pread(uint8_t* buffer, uint64_t offset , uint64_t len);
        uint64_t pwrite(uint8_t *buffer, uint64_t offset, uint64_t len);
        uint64_t perase(uint8_t *buffer, uint64_t offset, uint64_t len);
        uint64_t lread(uint8_t *buffer, uint64_t offset, uint64_t len);
        uint64_t lwrite(uint8_t *buffer, uint64_t offset, uint64_t len);
        uint64_t ltrim(uint8_t *buffer, uint64_t offset, uint64_t len);
        uint64_t read_buffer(uint8_t *buffer, uint64_t req_type);
        void start_simulation();
        void stop_simulation();
        void next_simulation_tick(uint64_t next_tick);
        typedef struct rv_soc_mem_access_cb_struct
        {
            bus_access_func bus_access;
            void *priv;
            rv_word_t addr_start;
            rv_word_t mem_size;

        } rv_soc_mem_access_cb_td;
        uint64_t clock_period;
        rv_soc_mem_access_cb_td mem_access_cbs[6];
        DRAM::AbstractDRAM *pDRAM;
    private: 
        CPU *pCPU; 
        std::vector<Core> rv_cores;
        uint8_t *mrom; /* Contains reset vector and device-tree? */
        uint8_t *ram;
        uint8_t *from; /* Contains filesystem */
        //superblock_td *fs_superblock; /* Contains superblock of the filesystem */
        clint_td clint;
        plic_td plic;
        simple_uart_td uart;
        void stop();
        void start();
        void init_mem_access_struct(int entry, bus_access_func bus_access, void* _priv, rv_word_t addr_start, rv_word_t mem_size);
};


} // namespace RISCV

} // namespace CPU

} // namespace SimpleSSD

#endif /* RISCV_EXAMPLE_SOC_H */
