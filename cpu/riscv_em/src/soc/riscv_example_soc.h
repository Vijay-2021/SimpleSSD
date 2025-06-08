#ifndef RISCV_EXAMPLE_SOC_H
#define RISCV_EXAMPLE_SOC_H

#include "src/core/riscv_types.h"
#include "src/core/core.h"

#include "src/peripherals/plic/plic.h"
#include "src/peripherals/uart/uart_8250.h"
#include "src/peripherals/uart/simple_uart.h"
//#include "src/core/filesystem/ext2.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rv_soc_mem_access_cb_struct
{
    bus_access_func bus_access;
    void *priv;
    rv_uint_xlen addr_start;
    rv_uint_xlen mem_size;

} rv_soc_mem_access_cb_td;

typedef struct rv_soc_struct
{
    /* For now we have 1 single core */
    rv_core_td* rv_cores;
    uint32_t num_cores;
    uint8_t *mrom; /* Contains reset vector and device-tree? */
    uint8_t *ram;
    uint8_t *from; /* Contains filesystem */
    //superblock_td *fs_superblock; /* Contains superblock of the filesystem */
    clint_td clint;
    plic_td plic;
    simple_uart_td uart;
    rv_soc_mem_access_cb_td mem_access_cbs[6];
    void* ctx;
    uint8_t (*read)(void *ctx, uint8_t* buffer, uint32_t offset , uint32_t len);
	uint8_t (*write)(void *ctx, uint8_t *buffer, uint32_t offset, uint32_t len);
    void (*stop) (void *ctx);
} rv_soc_td;

void rv_soc_dump_mem(rv_soc_td *rv_soc);
void rv_soc_init(rv_soc_td *rv_soc, char *fw_file_name, char *dtb_file_name, char *initrd_file_name);
void rv_soc_run(rv_soc_td *rv_soc, rv_uint_xlen success_pc, uint64_t num_cycles); // this depends on the stored number of cycles in the core
void rv_soc_tick(rv_soc_td *rv_soc, rv_uint_xlen success_pc, uint64_t num_cycles); // this just runs for the given number of cycles
void rv_soc_fs_init(rv_soc_td *rv_soc); 
#ifdef __cplusplus
}
#endif

#endif /* RISCV_EXAMPLE_SOC_H */
