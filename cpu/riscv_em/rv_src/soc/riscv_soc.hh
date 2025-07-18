#ifndef RISCV_EXAMPLE_SOC_H
#define RISCV_EXAMPLE_SOC_H

#include "rv_src/core/riscv_types.hh"
#include "rv_src/core/core.hh"
#include "rv_src/peripherals/plic/plic.hh"
#include "rv_src/peripherals/uart/simple_uart.hh"
#include "sim/statistics.hh"

#include <vector>
#include <queue>

namespace SimpleSSD {

namespace Memory {
    class SimpleDRAM;
    class SRAM; 
}

namespace CPU {

class CPU;

namespace RISCV {


class SOC : public StatObject {
    public: 
        SOC(char *fw_file_name, char *dtb_file_name, char *initrd_file_name, CPU *cpu, uint32_t num_cores, Memory::SimpleDRAM *dram);
        ~SOC();
        void rv_soc_dump_mem();
        void rv_soc_run(); // run the SOC until either success is reached or a stop signal is sent
        uint64_t rv_soc_tick(uint64_t num_cycles); // run the soc for a given number of cycles, returns the next tick time
        void rv_soc_init_mem_access_cbs();
        void rv_soc_fs_init(); 
        void rv_soc_add_task(char *input_cmd);
        void rv_soc_schedule();
        void rv_soc_deschedule();
        void rv_soc_schedule_next();
        uint64_t get_period(); 
        uint8_t* get_ram();
        void pread(uint8_t *request, uint64_t* tick);
        void pwrite(uint8_t *request, uint64_t* tick);
        void perase(uint8_t *request, uint64_t* tick);
        uint64_t lread(uint8_t *buffer, uint64_t offset, uint64_t len);
        uint64_t lwrite(uint8_t *buffer, uint64_t offset, uint64_t len);
        uint64_t ltrim(uint8_t *buffer, uint64_t offset, uint64_t len);
        void read_buffer(uint8_t *buffer, uint64_t req_info, uint64_t req_type);
        void write_buffer(uint8_t *buffer, uint64_t req_info, uint64_t req_type);
        void start_simulation();
        void stop_simulation();
        void next_simulation_tick(uint64_t next_tick);
        void coreSetup(uint64_t function_addr, uint64_t function_arg_ptr);

        typedef struct rv_soc_mem_access_cb_struct
        {
            bus_access_func bus_access;
            void *priv;
            rv_word_t addr_start;
            rv_word_t mem_size;

        } rv_soc_mem_access_cb_td;
        uint64_t clock_period;
        rv_soc_mem_access_cb_td mem_access_cbs[6];
        Memory::SimpleDRAM *pDRAM;
        Memory::SRAM *pCache; // SRAM for RISCV
        std::vector<Core> rv_cores;
        std::vector<uint64_t> next_core_ticks;
        uint64_t ICL_HIGH;
        uint64_t ICL_LOW;
        FTLStats* getFTLStats();
        ICLStats* getICLStats();
        void setFTLStats(FTLStats *ftl_stat_fw);
        void setICLStats(ICLStats *icl_stat_fw);
        void setICLLow(uint64_t *icl_low);
        void setICLHigh(uint64_t *icl_high);
        uint64_t read(uint64_t addr, uint64_t len);
        uint64_t write(uint64_t addr, uint64_t len);
        bool instruction_access(rv_word_t address, void *value, uint8_t len);
        bool getTestMode();
        bool test_mode = false; 
        bool cores_setup = false;
        uint64_t current_stack_bottom = 0;
        uint32_t current_core = 0;
        std::queue<char*> csd_queue; // CSD jobs queue
        uint64_t current_job_id = 0;
        
        void getStatList(std::vector<Stats> &list, std::string prefix) override;
        void getStatValues(std::vector<double> &values) override;
        void resetStatValues() override;
        uint64_t total_cycles = 0;
        uint64_t total_fast_forward_cycles = 0;
        FTLStats *ftl_stats;
        ICLStats *icl_stats;

    private: 
        CPU *pCPU; 
        uint8_t *mrom; /* Contains reset vector and device-tree? */
        uint8_t *ram;
        uint8_t *from; /* Contains filesystem */
        
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
