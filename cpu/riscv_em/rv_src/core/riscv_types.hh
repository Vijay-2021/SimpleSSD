#ifndef RISCV_TYPES_H
#define RISCV_TYPES_H

#include <stdint.h>

#include "riscv_config.hh"

#define __PACKED __attribute__((__packed__)) 

#ifdef RV64
    #define XLEN 64
    typedef uint64_t rv_word_t;
    typedef int64_t rv_sword_t;
#else
    #define XLEN 32
    typedef uint32_t rv_word_t;
    typedef int32_t rv_sword_t;
#endif

typedef enum
{
    rv_ok,
    rv_err

} rv_ret;

typedef enum  
{
    priv_level_unknown = -1, /* This just ensures that the enum is signed, which might be needed in down counting for loops */
    user_mode = 0,
    supervisor_mode = 1,
    reserved_mode = 2, /* Hypervisor ?? */
    machine_mode = 3,
    priv_level_max = 4

} privilege_level;

typedef enum
{
    bus_read_access = 0,
    bus_write_access,
    bus_instr_access,

    bus_access_type_max

} bus_access_type;

typedef uint64_t (*bus_access_func)(void *priv, privilege_level priv_level, bus_access_type access_type, rv_word_t addr, void *value, uint8_t len);

namespace SimpleSSD {

namespace CPU {

namespace RISCV {

typedef enum {
  FAST_FORWARD_MODE = 0, // soc runs until it sends a stop signal to cpu
  TIMING_MODE = 1, // schedule soc at requested intervals
  CYCLE_MODE = 2, // soc runs at every tick
  BURST_MODE = 3, // run soc for a set burst of cycles
  PAUSED_MODE = 4, // soc is paused, no execution
  FAILED_MODE = 5, // soc has failed, no execution
} SOC_RUN_MODE;

typedef enum {
    FIRMWARE_FTL_PARAMS = 0, 
    FIRMWARE_ICL_PARAMS = 1,
    FIRMWARE_QUEUE_TOP = 2,
    FIRMWARE_BITSET_BUFFER = 3,
    FIRMWARE_TICK = 4,
    FIRMWARE_CYCLE = 5,
    FIRMWARE_CORE_ID = 6,
} DATA_REQ;

typedef enum {
  FIRMWARE_REQ_DONE = 0,
  FIRMWARE_REQ_FAILED = 1,
} DATA_RESP; 


}

}

} // namespace SimpleSSD::CPU::RISCV

#define FLOAT_FMT 0x00
#define DOUBLE_FMT 0x01

#define RM_NEAREST 0x00
#define RM_TZERO 0x01 
#define RM_DOWN 0x02
#define RM_UP 0x03
#define RM_NEAREST_MAX 0x04
#define RM_DYNAMIC 0x07

#define RD_SOC_CONFIG 0x00
#define RD_SOC_LOG 0x01
// add more

#endif /* RISCV_TYPES_H */
