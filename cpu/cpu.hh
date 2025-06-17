/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __CPU_CPU__
#define __CPU_CPU__

#include <cinttypes>
#include <queue>
#include <unordered_map>
#include <vector>
#include <termios.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cpu/def.hh"
#include "lib/mcpat/mcpat.h"
#include "sim/config_reader.hh"
#include "sim/dma_interface.hh"
#include "sim/simulator.hh"
#include "sim/statistics.hh"

#include "rv_src/soc/riscv_example_soc.hh"
#include "rv_src/core/riscv_helper.hh"
#include "rv_src/peripherals/uart/simple_uart.hh"
#include "util/simplessd.hh"
#include "util/disk.hh"
#include "util/def.hh"
namespace SimpleSSD {

namespace ICL {
  class ICL;
}

namespace FTL {
  class FTL;
}

namespace PAL {
  class PAL;
}

namespace DRAM {
  class AbstractDRAM;
}

namespace CPU {

typedef enum {
    FIRMWARE_PARAMS = 0, 
    FIRMWARE_QUEUE = 1,
} DATA_REQ;


typedef struct firmware_params {
	uint64_t totalPhysicalBlocks;
  uint64_t totalLogicalBlocks;
  uint64_t pagesInBlock;
  uint32_t pageSize;
  uint32_t ioUnitInPage;
  uint32_t pageCountToMaxPerf;  
	bool bRandomTweak;
	float ftl_fill_ratio;
	float ftl_invalid_page_ratio;
	FTL::FILLING_MODE ftl_filling_mode; // or FILLING_MODE
	float ftl_gc_threshold_ratio;
	FTL::GC_MODE ftl_gc_mode;
	FTL::EVICT_POLICY ftl_evict_policy;
	uint32_t choiceParam;
	uint64_t ftl_gc_reclaim_block;
	float ftl_gc_reclaim_threshold;
	uint64_t bad_block_threshold;
} firmware_params_td;

typedef struct _InstStat {
  // Instruction count
  uint64_t branch;
  uint64_t load;
  uint64_t store;
  uint64_t arithmetic;
  uint64_t floatingPoint;
  uint64_t otherInsts;

  // Total times in ps to execute this insturcion group
  uint64_t latency;

  _InstStat();
  _InstStat(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
            uint64_t);
  _InstStat &operator+=(const _InstStat &);
  uint64_t sum();
} InstStat;

typedef struct _JobEntry {
  DMAFunction func;
  void *context;
  InstStat *inst;
  uint64_t submitAt;
  uint64_t delay;

  _JobEntry(DMAFunction &, void *, InstStat *);
} JobEntry;

class CPU : public StatObject {
 private:
  struct CoreStat {
    InstStat instStat;
    uint64_t busy;

    CoreStat();
  };
  Event csdCycleEvent; // cycle for computational storage device
  class Core {
   private:
    bool busy;

    Event jobEvent;
    std::queue<JobEntry> jobs;

    CoreStat stat;

    void handleJob();
    void jobDone();

   public:
    Core();
    ~Core();

    void submitJob(JobEntry, uint64_t = 0);

    void addStat(InstStat &);

    bool isBusy();
    uint64_t getJobListSize();
    CoreStat &getStat();
  };
  
  ConfigReader &conf;
  
  ICL::ICL *pICL;
  FTL::FTL *pFTL;
  PAL::PAL *pPAL;
  DRAM::AbstractDRAM *pDRAM;
  Disk *pDisk;

  uint64_t lastResetStat;

  uint64_t clockSpeed;
  uint64_t clockPeriod;

  // Cores
  std::vector<Core> hilCore;
  std::vector<Core> iclCore;
  std::vector<Core> ftlCore;
  RISCV::SOC *csd;
  // CPIs
  std::unordered_map<uint16_t, std::unordered_map<uint16_t, InstStat>> cpi;

  uint32_t leastBusyCPU(std::vector<Core> &);
  void calculatePower(Power &);
  void csdCycle();
  bool csd_in_progress;
  uint32_t page_size = 16834; // Default page size for SimpleSSD
  uint32_t lba_size = 512;
  firmware_params_td fw_params;
  std::vector<FTL::Request> req_buffer;
  public:
    CPU(ConfigReader &, ICL::ICL *, FTL::FTL *, PAL::PAL *pal, DRAM::AbstractDRAM *dram);
    ~CPU();

    void execute(NAMESPACE, FUNCTION, DMAFunction &, void * = nullptr,
                uint64_t = 0);
    uint64_t applyLatency(NAMESPACE, FUNCTION);

    void getStatList(std::vector<Stats> &, std::string) override;
    void getStatValues(std::vector<double> &) override;
    uint64_t getClockPeriod();
    void resetStatValues() override;
    void startCSD();
    bool socIsPaused();
    void stopCSD();
    void initCSD();
    void printLastStat();
    uint64_t read_flash_icl(uint8_t* buffer, uint64_t offset , uint64_t len); // for the flash functions
    uint64_t write_flash_icl(uint8_t* buffer, uint64_t offset , uint64_t len);
    uint64_t trim_flash_icl(uint8_t* buffer, uint64_t offset , uint64_t len);
    uint64_t read_flash_pal(uint8_t* buffer, uint64_t lpn , uint64_t ppn);
    uint64_t write_flash_pal(uint8_t* buffer, uint64_t lpn , uint64_t ppn);
    uint64_t erase_flash_pal(uint8_t* buffer, uint64_t lpn , uint64_t ppn);
    uint64_t read_buffer(uint8_t *buffer, uint64_t req_type);
    void setDisk(Disk *disk);
    void closeDisk();
    void addCSDTask(char* input_cmd);
    uint64_t submitReadRequest(FTL::Request &req);
    uint64_t submitWriteRequest(FTL::Request &req);
    uint64_t submitTrimRequest(FTL::Request &req);
    uint64_t submitFormatRequest(FTL::Request &range);
};

}  // namespace CPU

}  // namespace SimpleSSD


#endif
