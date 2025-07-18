#include "riscv_soc.hh"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits>
#include <inttypes.h>
#include "rv_src/core/riscv_helper.hh"
#include "rv_src/helpers/file_helper.hh"
#include "rv_src/core/instruction_timings.hh"
#include "cpu/cpu.hh"
#include "memory/simple.hh"
#include "memory/sram.hh"

namespace SimpleSSD {

namespace CPU {

namespace RISCV {

void SOC::init_mem_access_struct(int entry, bus_access_func bus_access, void* priv, rv_word_t addr_start, rv_word_t mem_size) 
{
    mem_access_cbs[entry].bus_access = bus_access;
    mem_access_cbs[entry].priv = priv;
    mem_access_cbs[entry].addr_start = addr_start;
    mem_access_cbs[entry].mem_size = mem_size;
}

static rv_ret memory_bus_access(void *priv, privilege_level priv_level, bus_access_type access_type, rv_word_t address, void *value, uint8_t len)
{
    (void) priv_level;
    uint8_t *mem_ptr = (uint8_t*)priv;

    if(access_type == bus_write_access) {
        memcpy(&mem_ptr[address], value, len);
    }
    else {
        memcpy(value, &mem_ptr[address], len);
    }
    return rv_ok;
}


void SOC::rv_soc_init_mem_access_cbs()
{
    int count = 0;
    init_mem_access_struct(count++, memory_bus_access, ram, RAM_BASE_ADDR, RAM_SIZE_BYTES);
    init_mem_access_struct(count++, clint_bus_access, &clint, CLINT_BASE_ADDR, CLINT_SIZE_BYTES);
    init_mem_access_struct(count++, plic_bus_access, &plic, PLIC_BASE_ADDR, PLIC_SIZE_BYTES);
    init_mem_access_struct(count++, simple_uart_bus_access, &uart, SIMPLE_UART_TX_REG_ADDR, SIMPLE_UART_SIZE_BYTES);
    init_mem_access_struct(count++, memory_bus_access, mrom, MROM_BASE_ADDR, MROM_SIZE_BYTES);
    init_mem_access_struct(count++, memory_bus_access, from, FROM_BASE_ADDR, FROM_SIZE_BYTES);
}

static uint64_t rv_soc_bus_access(void *priv, privilege_level priv_level, bus_access_type access_type, rv_word_t address, void *value, uint8_t len)
{
    SOC *rv_soc = (SOC *)priv;
    rv_word_t tmp_addr = 0;
    size_t i = 0;
    uint64_t ret_cycles = 1;
    for(i=0;i<(sizeof(rv_soc->mem_access_cbs)/sizeof(rv_soc->mem_access_cbs[0]));i++)
    {
        if(ADDR_WITHIN_LEN(address, len, rv_soc->mem_access_cbs[i].addr_start, rv_soc->mem_access_cbs[i].mem_size))
        {
            tmp_addr = address - rv_soc->mem_access_cbs[i].addr_start;
            if (rv_soc->mem_access_cbs[i].addr_start == RAM_BASE_ADDR) {
                if (access_type == bus_write_access) {
                    ret_cycles = rv_soc->write((uint64_t)tmp_addr, (uint64_t)len);
                } else if (access_type == bus_read_access) {
                    ret_cycles = rv_soc->read((uint64_t)tmp_addr, (uint64_t)len);
                }
            } 
            rv_soc->mem_access_cbs[i].bus_access(rv_soc->mem_access_cbs[i].priv, priv_level, access_type, tmp_addr, value, len);
            return ret_cycles;
        }
    }
    printf("address is: %p\n", (void*)address);
    printf("pc is: %p\n", (void*)rv_soc->rv_cores[rv_soc->current_core].pc);
    printf("instruction is: %x\n", rv_soc->rv_cores[rv_soc->current_core].instruction);
    die_msg("Invalid Addresses, or no valid write pointer found, write not executed!");
    return 0; // 0 to indicate error
}

SOC::SOC(char *fw_file_name, char *dtb_file_name, char *initrd_file_name, CPU *cpu, uint32_t num_cores, Memory::SimpleDRAM *dram) : pDRAM(dram), pCPU(cpu)
{
    #define RESET_VEC_SIZE 10
    #define MiB 0x100000

    uint64_t i;
    uint64_t start_addr = RAM_BASE_ADDR;
    uint64_t ram_addr_end = (RAM_BASE_ADDR + RAM_SIZE_BYTES);
    uint64_t fdt_addr = 0;
    uint64_t fdt_size = 0;
    uint64_t tmp = 0;

    ftl_stats = nullptr; // set to null until firmware overwrites it
    icl_stats = nullptr;

    clock_period = pCPU->getClockPeriod(); 
    /* Init everything to zero */
    // memset(rv_soc, 0, sizeof(rv_soc_td));
    from = (uint8_t*) calloc(FROM_SIZE_BYTES, sizeof(uint8_t));
    mrom = (uint8_t*) calloc(MROM_SIZE_BYTES, sizeof(uint8_t));
    ram = (uint8_t*) calloc(RAM_SIZE_BYTES, sizeof(uint8_t));
    pCache = new Memory::SRAM(SRAM_SIZE, SRAM_BLOCK_SIZE, SRAM_NUM_WAYS);
    ICL_HIGH = 0;
    ICL_LOW = 0;
    /* Copy dtb and firmware */
    if(dtb_file_name != NULL)
    {
        fdt_size = get_file_size(dtb_file_name);

        /*
         * This is a little annoying: qemu keeps changing this stuff 
         * from time to time and I need to do it the same, otherwise the 
         * tests would fail as they won't match with qemu's results anymore
         * Be Aware: 16 * MiB was taken from qemu 6.2.0 but it could be
         * that they changed it already in later versions
         */
        fdt_addr = ADDR_ALIGN_DOWN(ram_addr_end - fdt_size, 16 * MiB);
        tmp = fdt_addr - RAM_BASE_ADDR;
        write_mem_from_file(dtb_file_name, &ram[tmp], RAM_SIZE_BYTES-tmp);
    }

    write_mem_from_file(fw_file_name, ram, RAM_SIZE_BYTES);
    uint32_t* ram_instrs = (uint32_t*) ram;
    if (initrd_file_name != NULL) {
        write_mem_from_file(initrd_file_name, from, FROM_SIZE_BYTES);
    }
    
    /* this is the reset vector, taken from qemu v5.2 */
    uint32_t reset_vec[RESET_VEC_SIZE] = {
        0x00000297,                  /* 1:  auipc  t0, %pcrel_hi(fw_dyn) */
        0x02828613,                  /*     addi   a2, t0, %pcrel_lo(1b) */
        0xf1402573,                  /*     csrr   a0, mhartid  */
        #ifdef RV64
            0x0202b583,              /*     ld     a1, 32(t0) */
            0x0182b283,              /*     ld     t0, 24(t0) */
        #else
            0x0202a583,              /*     lw     a1, 32(t0) */
            0x0182a283,              /*     lw     t0, 24(t0) */
        #endif
        0x00028067,                  /*     jr     t0 */
        start_addr,                  /* start: .dword */
        0x00000000,
        fdt_addr,                    /* fdt_laddr: .dword */
        0x00000000,
                                     /* fw_dyn: */
    };

    uint32_t *tmp_ptr = (uint32_t *)mrom;
    for(i=0;i<(sizeof(reset_vec)/sizeof(reset_vec[0]));i++)
    {
        tmp_ptr[i] = reset_vec[i];
    }
    for (uint32_t core_id = 0; core_id < num_cores; core_id++) {
        rv_cores.push_back(Core(this, rv_soc_bus_access, core_id));
        next_core_ticks.push_back(0);
    }
    /* initialize one core with a csr table */
    simple_uart_init(&uart);

    /* initialize ram and peripheral read write access pointers */
    rv_soc_init_mem_access_cbs();
}

SOC::~SOC() {
    free(mrom);
    free(from);
    free(ram);
    delete pCache;
}

void SOC::rv_soc_dump_mem()
{
    uint32_t i = 0;
    printf("rv RAM contents\n");
    for(i=0;i<RAM_SIZE_BYTES/(sizeof(rv_word_t));i++)
    {
        printf("%x\n", ram[i]);
    }
}

void SOC::rv_soc_run()
{
    uint8_t mei = 0, msi = 0, mti = 0;
    uint8_t uart_irq_pending = 0;
    for (uint32_t core_id = 0; core_id < rv_cores.size(); core_id++) {
        rv_cores[core_id].rv_core_reg_dump();
    }
    uint64_t time = 0;
    while(soc_run_mode_ == FAST_FORWARD_MODE) 
    {
        if (cores_setup) {
            for (uint32_t core_id = 0; core_id < rv_cores.size(); core_id++) {
                current_core = core_id; // set the current core for the callbacks
                rv_cores[core_id].rv_core_run();
            }
            
            //uart_irq_pending = simple_uart_update(&uart);
            //plic_update_pending(&plic, 10, uart_irq_pending);
            //mei = plic_update(&plic);
            //clint_update(&clint, &msi, &mti);

            for (uint32_t core_id = 0; core_id < rv_cores.size(); core_id++) {
                // rv_cores[core_id].rv_core_process_interrupts(mei, mti, msi);
                // rv_cores[core_id].rv_core_reg_dump();
            }
        } else {
            rv_cores[0].rv_core_run(); // run only the first core until cores are set up
        }
        total_cycles++;
        total_fast_forward_cycles++;
    }
    
}


uint64_t SOC::rv_soc_tick(uint64_t num_cycles)
{
    uint8_t mei = 0, msi = 0, mti = 0;
    uint8_t uart_irq_pending = 0;
    uint64_t next_tick = getTick() + clock_period; // default next tick is the next cycle
    // rv_core_reg_dump(&rv_core0);
    uint64_t next_cycle = std::numeric_limits<uint64_t>::max(); // reset next cycle to max value for each cycle
    for (uint64_t current_cycle = 0; current_cycle < num_cycles && soc_run_mode_ != PAUSED_MODE && soc_run_mode_ != FAILED_MODE; current_cycle++) {
        if (cores_setup) {
            next_tick = std::numeric_limits<uint64_t>::max(); // reset next tick to max value for each cycle
            for (uint32_t core_id = 0; core_id < rv_cores.size(); core_id++) {
                if (getTick() >= next_core_ticks[core_id]) {
                    current_core = core_id; // set the current core for the callbacks
                    uint64_t curr_cycle = rv_cores[core_id].rv_core_run();
                    next_core_ticks[core_id] = getTick() + clock_period*curr_cycle;
                    next_cycle = std::min(next_cycle, curr_cycle); // find the next cycle across all cores
                }
                total_cycles+= next_cycle;
                next_tick = std::min(next_tick, next_core_ticks[core_id]); // for scheduling purposes, we need to find the next tick across all cores
            }
        } else {
            if (getTick() >= next_core_ticks[0]) {
                next_cycle = rv_cores[0].rv_core_run();
                next_core_ticks[0] = getTick() + clock_period*next_cycle;
            }
            total_cycles += next_cycle;
            next_tick = next_core_ticks[0]; // for the first core, just use its next tick
        }
        // uart_irq_pending = simple_uart_update(&uart);

        // /* update interrupt controllers */
        // plic_update_pending(&plic, 10, uart_irq_pending);
        // mei = plic_update(&plic);

        // /* Feed clint and update internall states */    
        // clint_update(&clint, &msi, &mti);

        // /* update CSRs for actual interrupt processing */
        // for (uint32_t core_id = 0; core_id < rv_cores.size(); core_id++) {
        //     rv_cores[core_id].rv_core_process_interrupts(mei, mti, msi);
        // }
    }
    return next_tick;
}

void SOC::rv_soc_add_task(char *input_cmd)
{
    csd_queue.push(input_cmd);
}

uint64_t SOC::lread(uint8_t* buffer, uint64_t offset , uint64_t len) {
    return pCPU->read_flash_icl(buffer, offset, len); // add the read request to the queue
}

uint64_t SOC::lwrite(uint8_t *buffer, uint64_t offset, uint64_t len) {
    return pCPU->write_flash_icl(buffer, offset, len); // add the write request to the queue
}

uint64_t SOC::ltrim(uint8_t *buffer, uint64_t offset, uint64_t len) {
    return pCPU->trim_flash_icl(buffer, offset, len);
}

void SOC::pread(uint8_t* request, uint64_t* tick) {
    pCPU->read_flash_pal(request, tick);
}
void SOC::pwrite(uint8_t* request, uint64_t* tick) {
    pCPU->write_flash_pal(request, tick);
}
void SOC::perase(uint8_t* request, uint64_t* tick) {
    pCPU->erase_flash_pal(request, tick);
}

void SOC::read_buffer(uint8_t *buffer, uint64_t req_info, uint64_t req_type) {
    if (req_type == FIRMWARE_CSD_QUEUE_TOP) {
        char* csd_job = csd_queue.front();
        printf("Calling CSD queue top with job: %s\n", csd_job);
        memcpy(buffer, csd_queue.front(), strlen(csd_job) + 1); // copy the job to the buffer
        csd_queue.pop(); // remove the job from the queue after reading

    } else if (req_type == FIRMWARE_CSD_QUEUE_SIZE) {
        uint64_t size = csd_queue.size();
        memcpy(buffer, &size, sizeof(uint64_t));
    } else {
        pCPU->read_buffer(buffer, req_info, req_type);
    }
}

void SOC::coreSetup(uint64_t function_addr, uint64_t function_arg_ptr) {
    if (cores_setup) {
        printf("Core setup already completed, skipping\n");
        return;
    }
    for (size_t core_id =  1; core_id < rv_cores.size(); core_id++) {
        rv_cores[core_id].curr_priv_mode = rv_cores[0].curr_priv_mode;
        
        for (size_t i = 0; i < NR_RVI_REGS; i++) {
            rv_cores[core_id].reg_file[i] = 0;
        }
        for (size_t i = 0; i < NR_RVF_REGS; i++) {
            rv_cores[core_id].float_reg_file[i] = 0;
        }
        rv_cores[core_id].reg_file[10] = function_arg_ptr; // a0
        if (current_stack_bottom == 0) {
            current_stack_bottom = rv_cores[0].reg_file[2] - STACK_SIZE;
            rv_cores[core_id].stack_bottom_min = current_stack_bottom; // set the minimum stack bottom for the core
        }
        rv_cores[core_id].reg_file[2] = current_stack_bottom;
        current_stack_bottom -= STACK_SIZE; // allocate a new stack for the core
        rv_cores[core_id].stack_bottom_min = current_stack_bottom; // set the minimum stack bottom for the core
        rv_cores[core_id].pc = function_addr;
        rv_cores[core_id].next_pc = 0;
        rv_cores[core_id].instruction = 0;
        rv_cores[core_id].opcode = 0;
        rv_cores[core_id].rd = 0;
        rv_cores[core_id].rs1 = 0;
        rv_cores[core_id].rs2 = 0;
        rv_cores[core_id].rs3 = 0;
        rv_cores[core_id].func3 = 0;
        rv_cores[core_id].func7 = 0;
        rv_cores[core_id].func6 = 0;
        rv_cores[core_id].func5 = 0;
        rv_cores[core_id].func12 = 0;
        rv_cores[core_id].rm = 0;
        rv_cores[core_id].immediate = 0;
        rv_cores[core_id].jump_offset = 0;
        rv_cores[core_id].sync_trap_pending = 0;
        rv_cores[core_id].sync_trap_cause = 0;
        rv_cores[core_id].sync_trap_tval = 0;
        rv_cores[core_id].execute_cb = nullptr;

        for (size_t i = 0; i < CSR_ADDR_MAX; i++) {
            rv_cores[core_id].csr_regs[i].internal_reg = 0;
        }

        rv_cores[core_id].lr_valid = 0;
        rv_cores[core_id].lr_address = 0;
        rv_cores[core_id].curr_cycle = 0;
    }
    cores_setup = true; // signal that the cores are setup
}

void SOC::write_buffer(uint8_t* buffer, uint64_t req_info, uint64_t req_type) {
    pCPU->write_buffer(buffer, req_info, req_type);
}
uint64_t SOC::get_period() {
    return clock_period;
} 

uint8_t* SOC::get_ram() {
    return ram;
}

void SOC::start_simulation() {
    pCPU->startRISCV();
}

void SOC::stop_simulation() {
    pCPU->stopRISCV();
}

void SOC::next_simulation_tick(uint64_t next_tick) {
    // To-do
}

void SOC::resetStatValues() {
    for (auto &core : rv_cores) {
        core.resetStatValues();
    }
    if (ftl_stats) {
        memset(ftl_stats, 0, sizeof(FTLStats));
    } 
    if (icl_stats) {
        memset(icl_stats, 0, sizeof(ICLStats));
    }
    total_cycles = 0; 
    total_fast_forward_cycles = 0;
    pDRAM->resetStatValues();
    pCache->resetStatValues();
}

FTLStats* SOC::getFTLStats() {
    return ftl_stats;
}
ICLStats* SOC::getICLStats() {
    return icl_stats;
}
void SOC::setFTLStats(FTLStats *ftl_stats) {
    ftl_stats = ftl_stats;
}
void SOC::setICLStats(ICLStats *icl_stats) {
    icl_stats = icl_stats;
}

void SOC::setICLLow(uint64_t *icl_low) {
    ICL_LOW = *icl_low - RAM_BASE_ADDR; // store the low address relative to RAM base address
}

void SOC::setICLHigh(uint64_t *icl_high) {
    ICL_HIGH = *icl_high - RAM_BASE_ADDR;
}

uint64_t SOC::read(uint64_t addr, uint64_t len) {
    if (((ICL_HIGH == 0 && ICL_LOW == 0) || (addr > ICL_HIGH || addr < ICL_LOW)) && pCache->read(addr)) {
        return LOAD_CACHE_CYCLE_COUNT;
    } else {
        uint64_t dramReadTime = pDRAM->read_dram((void*)addr, len);
        double cyclesD = std::ceil(dramReadTime / clock_period);
        uint64_t cycles = static_cast<uint64_t>(cyclesD);
        return cycles;
    }
}

uint64_t SOC::write(uint64_t addr, uint64_t len) {
    if (((ICL_HIGH == 0 && ICL_LOW == 0) || (addr > ICL_HIGH || addr < ICL_LOW)) && pCache->write(addr)) {
        return WRITE_CACHE_CYCLE_COUNT;
    } else {
        uint64_t dramWriteTime = pDRAM->write_dram((void*)addr, len);
        double cyclesD = std::ceil(dramWriteTime / clock_period);
        uint64_t cycles = static_cast<uint64_t>(cyclesD);
        return cycles;
    }
}

bool SOC::getTestMode() {
    return test_mode;
}

void SOC::getStatList(std::vector<Stats> &list, std::string prefix) {
    Stats temp;
    
    for (auto & core : rv_cores) {
        core.getStatList(list, prefix + ".core" + std::to_string(core.core_id));
    }

    temp.name = prefix + ".icl_read_requests";
    temp.desc = "Total ICL read requests";
    list.push_back(temp);
    temp.name = prefix + ".icl_write_requests";
    temp.desc = "Total ICL write requests";
    list.push_back(temp);
    temp.name = prefix + ".icl_trim_requests";
    temp.desc = "Total ICL trim requests";
    list.push_back(temp);
    temp.name = prefix + ".icl_format_requests";
    temp.desc = "Total ICL format requests";
    list.push_back(temp);
    temp.name = prefix + ".icl_flush_requests";
    temp.desc = "Total ICL flush requests";
    list.push_back(temp);
    temp.name = prefix + ".read_cache_hits";
    temp.desc = "Total read cache hits";
    list.push_back(temp);
    temp.name = prefix + ".read_cache_misses";
    temp.desc = "Total read cache misses";
    list.push_back(temp);
    temp.name = prefix + ".write_cache_hits";
    temp.desc = "Total write cache hits";
    list.push_back(temp);
    temp.name = prefix + ".write_cache_misses";
    temp.desc = "Total write cache misses";
    list.push_back(temp);
    temp.name = prefix + ".icl_evictions";
    temp.desc = "Total ICL cache evictions";
    list.push_back(temp);
    temp.name = prefix + ".icl_read_cycles";
    temp.desc = "Total ICL read cycles";
    list.push_back(temp);
    temp.name = prefix + ".icl_write_cycles";
    temp.desc = "Total ICL write cycles";
    list.push_back(temp);
    temp.name = prefix + ".icl_trim_cycles";
    temp.desc = "Total ICL trim cycles";
    list.push_back(temp);
    temp.name = prefix + ".icl_format_cycles";
    temp.desc = "Total ICL format cycles";
    list.push_back(temp);
    temp.name = prefix + ".icl_flush_cycles";
    temp.desc = "Total ICL flush cycles";
    list.push_back(temp);
    temp.name = prefix + ".icl_read_bytes_transferred";
    temp.desc = "Total ICL read bytes transferred";
    list.push_back(temp);
    temp.name = prefix + ".icl_write_bytes_transferred";
    temp.desc = "Total ICL write bytes transferred";
    list.push_back(temp);
    temp.name = prefix + ".icl_trim_bytes_transferred";   
    temp.desc = "Total ICL trim bytes transferred";
    list.push_back(temp);
    temp.name = prefix + ".icl_format_bytes_transferred";
    temp.desc = "Total ICL format bytes transferred";
    list.push_back(temp);
    temp.name = prefix + ".icl_flush_bytes_transferred";
    temp.desc = "Total ICL flush bytes transferred";
    list.push_back(temp);

    temp.name = prefix + ".heap_top";
    temp.desc = "Heap top address";
    list.push_back(temp);

    temp.name = prefix + ".ftl_read_requests";
    temp.desc = "Total FTL read requests";
    list.push_back(temp);
    temp.name = prefix + ".ftl_write_requests";
    temp.desc = "Total FTL write requests";
    list.push_back(temp);
    temp.name = prefix + ".ftl_trim_requests";
    temp.desc = "Total FTL trim requests";
    list.push_back(temp);
    temp.name = prefix + ".ftl_format_requests";
    temp.desc = "Total FTL format requests";
    list.push_back(temp);
    temp.name = prefix + ".ftl_garbage_collection_requests";
    temp.desc = "Total FTL garbage collection requests";
    list.push_back(temp);
    temp.name = prefix + ".ftl_read_cycles";
    temp.desc = "Total FTL read cycles";
    list.push_back(temp);
    temp.name = prefix + ".ftl_write_cycles";
    temp.desc = "Total FTL write cycles";
    list.push_back(temp);
    temp.name = prefix + ".ftl_trim_cycles";
    temp.desc = "Total FTL trim cycles";
    list.push_back(temp);
    temp.name = prefix + ".ftl_format_cycles";
    temp.desc = "Total FTL format cycles";
    list.push_back(temp);
    temp.name = prefix + ".ftl_garbage_collection_cycles";
    temp.desc = "Total FTL garbage collection cycles";
    list.push_back(temp);  

    temp.name = prefix + ".total_simulated_cycles";
    temp.desc = "Total simulated cycles";
    list.push_back(temp);
    temp.name = prefix + ".total_fast_forward_cycles";
    temp.desc = "Total fast forward cycles";
    list.push_back(temp);

    pDRAM->getStatList(list, prefix + ".dram");
    pCache->getStatList(list, prefix + ".cache");
    // temp.name = prefix + ".total_dram_read_accesses";
    // temp.desc = "Total DRAM read accesses";
    // list.push_back(temp);
    // temp.name = prefix + ".total_dram_read_stalled_cycles";
    // temp.desc = "Total DRAM Read latency";
    // list.push_back(temp);
    // temp.name = prefix + ".total_dram_write_accesses";
    // temp.desc = "Total DRAM write accesses";
    // list.push_back(temp);
    // temp.name = prefix + ".total_dram_write_stalled_cycles";
    // temp.desc = "Total DRAM Write latency";
    // list.push_back(temp);
    // temp.name = prefix + ".total_dram_read_bytes_transferred"; // ICL requests are modelled similar to OpenSSD by requesting a large amount of data from DRAM
    // temp.desc = "Total DRAM read bytes transferred";
    // list.push_back(temp);
    // temp.name = prefix + ".total_dram_write_bytes_transferred";
    // temp.desc = "Total DRAM write bytes transferred";
    // list.push_back(temp);

 
    // temp.name = prefix + ".total_cache_read_accesses";
    // temp.desc = "Total Cache read accesses";
    // list.push_back(temp);
    // temp.name = prefix + ".total_cache_read_hits";
    // temp.desc = "Total Cache read hits";
    // list.push_back(temp);
    // temp.name = prefix + ".total_cache_read_misses";
    // temp.desc = "Total Cache read misses";
    // list.push_back(temp);
    // temp.name = prefix + ".total_cache_write_accesses";
    // temp.desc = "Total Cache write accesses";
    // list.push_back(temp);
    // temp.name = prefix + ".total_cache_write_hits";
    // temp.desc = "Total Cache write hits";
    // list.push_back(temp);
    // temp.name = prefix + ".total_cache_write_misses";
    // temp.desc = "Total Cache write misses";
    // list.push_back(temp);
    // temp.name = prefix + ".total_cache_read_evictions";
    // temp.desc = "Total Cache read evictions";
    // list.push_back(temp);
    // temp.name = prefix + ".total_cache_write_evictions";
    // temp.desc = "Total Cache write evictions";
    // list.push_back(temp);


}

void SOC::getStatValues(std::vector<double> &values) {
    for (auto & core : rv_cores) {
        core.getStatValues(values);
    }
    auto *icl_stats = getICLStats();
    if (icl_stats != nullptr) {
        values.push_back(icl_stats->read_requests);
        values.push_back(icl_stats->write_requests);
        values.push_back(icl_stats->trim_requests);
        values.push_back(icl_stats->format_requests);
        values.push_back(icl_stats->flush_requests);
        values.push_back(icl_stats->read_cache_hits);
        values.push_back(icl_stats->read_cache_misses);
        values.push_back(icl_stats->write_cache_hits);
        values.push_back(icl_stats->write_cache_misses);
        values.push_back(icl_stats->cache_evictions);
        values.push_back(icl_stats->read_req_cycles);
        values.push_back(icl_stats->write_req_cycles);
        values.push_back(icl_stats->trim_req_cycles);
        values.push_back(icl_stats->format_req_cycles);
        values.push_back(icl_stats->flush_req_cycles);
        values.push_back(icl_stats->read_bytes);
        values.push_back(icl_stats->write_bytes);
        values.push_back(icl_stats->trim_bytes);
        values.push_back(icl_stats->format_bytes);
        values.push_back(icl_stats->flush_bytes);
        values.push_back(icl_stats->heap_top);
    } else {
        // If ICL stats are not available, push zeros
        for (size_t i = 0; i < 21; i++) {
        values.push_back(0);
        }
    }
    auto *ftl_stats = getFTLStats();
    if (ftl_stats != nullptr) {
        values.push_back(ftl_stats->read_requests);
        values.push_back(ftl_stats->write_requests);
        values.push_back(ftl_stats->trim_requests);
        values.push_back(ftl_stats->format_requests);
        values.push_back(ftl_stats->garbage_collection_requests);
        values.push_back(ftl_stats->read_req_cycles);
        values.push_back(ftl_stats->write_req_cycles);
        values.push_back(ftl_stats->trim_req_cycles);   
        values.push_back(ftl_stats->format_req_cycles);
        values.push_back(ftl_stats->gc_req_cycles);
    } else {
        // If FTL stats are not available, push zeros
        for (size_t i = 0; i < 10; i++) {
            values.push_back(0);
        }
    }
    values.push_back(total_cycles);
    values.push_back(total_fast_forward_cycles);
    pDRAM->getStatValues(values);
    pCache->getStatValues(values);   
}

} // namespace RISCV

} // namespace CPU

} // namespace SimpleSSD