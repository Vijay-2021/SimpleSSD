#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits>

#include "rv_src/core/riscv_helper.hh"
#include "riscv_example_soc.hh"
#include "rv_src/helpers/file_helper.hh"

#include "cpu/cpu.hh"
#include "dram/abstract_dram.hh"

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
    uint64_t ret_time = getTick() + rv_soc->clock_period;
    for(i=0;i<(sizeof(rv_soc->mem_access_cbs)/sizeof(rv_soc->mem_access_cbs[0]));i++)
    {
        if(ADDR_WITHIN_LEN(address, len, rv_soc->mem_access_cbs[i].addr_start, rv_soc->mem_access_cbs[i].mem_size))
        {
            tmp_addr = address - rv_soc->mem_access_cbs[i].addr_start;
            /**if (rv_soc->mem_access_cbs[i].addr_start == RAM_BASE_ADDR) {
                if (access_type == bus_write_access) {
                    rv_soc->pDRAM->write((void*)tmp_addr, len, ret_time);
                } else {
                    rv_soc->pDRAM->read((void*)tmp_addr, len, ret_time);
                }
            } else {
                ret_time += rv_soc->clock_period; // assume 1 cycle access time, for now
            }*/
            rv_soc->mem_access_cbs[i].bus_access(rv_soc->mem_access_cbs[i].priv, priv_level, access_type, tmp_addr, value, len);
            return ret_time;
        }
    }
    printf("address is: %p\n", (void*)address);
    printf("pc is: %p\n", (void*)rv_soc->rv_cores[0].pc);
    printf("instruction is: %x\n", rv_soc->rv_cores[0].instruction);
    die_msg("Invalid Addresses, or no valid write pointer found, write not executed!");
    return ret_time + rv_soc->clock_period;
}

SOC::SOC(char *fw_file_name, char *dtb_file_name, char *initrd_file_name, CPU *cpu, uint32_t num_cores, DRAM::AbstractDRAM *dram) : pDRAM(dram), pCPU(cpu)
{
    #define RESET_VEC_SIZE 10
    #define MiB 0x100000

    uint64_t i;
    uint64_t start_addr = RAM_BASE_ADDR;
    uint64_t ram_addr_end = (RAM_BASE_ADDR + RAM_SIZE_BYTES);
    uint64_t fdt_addr = 0;
    uint64_t fdt_size = 0;
    uint64_t tmp = 0;

    clock_period = pCPU->getClockPeriod(); 
    /* Init everything to zero */
    // memset(rv_soc, 0, sizeof(rv_soc_td));
    from = (uint8_t*) calloc(FROM_SIZE_BYTES, sizeof(uint8_t));
    mrom = (uint8_t*) calloc(MROM_SIZE_BYTES, sizeof(uint8_t));
    ram = (uint8_t*) calloc(RAM_SIZE_BYTES, sizeof(uint8_t));

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
    uint64_t cycle = 0;
    while(soc_run_mode_ != PAUSED_MODE && soc_run_mode_ != FAILED_MODE) 
    {
        if (cycle < 1000000 && cycle % 10000 == 0) {
            printf("Running cycle: %lu\n", cycle);
        }
        if (cycle % 10000000 == 0) {
            printf("Running cycle: %lu\n", cycle);
        }
        for (uint32_t core_id = 0; core_id < rv_cores.size(); core_id++) {
            rv_cores[core_id].rv_core_run();
        }
        /**
        uart_irq_pending = simple_uart_update(&uart);
        plic_update_pending(&plic, 10, uart_irq_pending);
        mei = plic_update(&plic);
        clint_update(&clint, &msi, &mti);

        for (uint32_t core_id = 0; core_id < rv_cores.size(); core_id++) {
            rv_cores[core_id].rv_core_process_interrupts(mei, mti, msi);

            rv_cores[core_id].rv_core_reg_dump();
        }*/
        cycle++;
    }
    
}


uint64_t SOC::rv_soc_tick(uint64_t num_cycles)
{
    uint8_t mei = 0, msi = 0, mti = 0;
    uint8_t uart_irq_pending = 0;
    uint64_t next_tick = getTick() + clock_period; // default next tick is the next cycle
    // rv_core_reg_dump(&rv_core0);
    for (uint64_t current_cycle = 0; current_cycle < num_cycles && soc_run_mode_ != PAUSED_MODE && soc_run_mode_ != FAILED_MODE; current_cycle++) {
        next_tick = std::numeric_limits<uint64_t>::max(); // reset next tick to max value for each cycle
        for (uint32_t core_id = 0; core_id < rv_cores.size(); core_id++) {
            uint64_t requested_tick = rv_cores[core_id].rv_core_run();
            next_tick = std::min(next_tick, requested_tick);
        }

        uart_irq_pending = simple_uart_update(&uart);

        /* update interrupt controllers */
        plic_update_pending(&plic, 10, uart_irq_pending);
        mei = plic_update(&plic);

        /* Feed clint and update internall states */    
        clint_update(&clint, &msi, &mti);

        /* update CSRs for actual interrupt processing */
        for (uint32_t core_id = 0; core_id < rv_cores.size(); core_id++) {
            rv_cores[core_id].rv_core_process_interrupts(mei, mti, msi);
        }
        
    }
    return next_tick;
}

void SOC::rv_soc_add_task(char *input_cmd)
{
    // This function is not implemented yet, but it should add a task to the CSD
    // For now, we just print the input command
    printf("Adding CSD task from soc: %s\n", input_cmd);
    strtok(input_cmd, " "); // Tokenize the input command if needed
    // You can implement the actual task addition logic here
}


uint64_t SOC::lread(uint8_t* buffer, uint64_t offset , uint64_t len) {
    return pCPU->read_flash_icl(buffer, offset, len);
}
uint64_t SOC::lwrite(uint8_t *buffer, uint64_t offset, uint64_t len) {
    return pCPU->write_flash_icl(buffer, offset, len);
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
    pCPU->read_buffer(buffer, req_info, req_type);
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
    pCPU->startCSD();
}

void SOC::stop_simulation() {
    pCPU->stopCSD();
}

void SOC::next_simulation_tick(uint64_t next_tick) {
    // To-do
}

} // namespace RISCV

} // namespace CPU

} // namespace SimpleSSD