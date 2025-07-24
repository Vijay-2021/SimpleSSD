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
#include <utility>

#include "cpu/def.hh"
#include "lib/mcpat/mcpat.h"
#include "sim/config_reader.hh"
#include "sim/dma_interface.hh"
#include "sim/simulator.hh"
#include "sim/statistics.hh"

#include "rv_src/soc/riscv_soc.hh"
#include "rv_src/core/riscv_helper.hh"
#include "rv_src/peripherals/uart/simple_uart.hh"
#include "rv_src/core/riscv_types.hh"

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

namespace Memory {
  class SimpleDRAM;
}

namespace CPU {

typedef struct _RISCVJob {
  ICL::Request request;
  DMAFunction func;
  void *context;
  _RISCVJob(ICL::Request &req, DMAFunction &f, void *c) 
    : request(req), func(f), context(c) {}
} RISCVJob;


struct __attribute__((packed, aligned(4))) ftl_params {
  uint64_t totalPhysicalBlocks;
  uint64_t totalLogicalBlocks;
  uint64_t pagesInBlock;
  uint32_t pageSize;
  uint32_t ioUnitInPage;
  uint32_t pageCountToMaxPerf;  
  uint32_t bRandomTweak;
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
};

struct __attribute__((packed, aligned(4))) icl_params {
    uint32_t pageSize;
    uint32_t pageCountToMaxPerf;
    uint32_t ioUnitInPage;
    uint32_t waySize;
    uint32_t prefetchCount;
    float prefetchRatio;
    uint32_t useReadCaching;
    uint32_t useWriteCaching;
    uint32_t useReadPrefetch;
    uint32_t useRandomIOTweak;
    uint64_t cacheSize;
    ICL::EVICT_MODE iclEvictGranularity;
    ICL::PREFETCH_MODE iclPrefetchGranularity;
    ICL::ICL_CACHE_TYPE cacheType; 
};

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
    Event RISCVCycleEvent; // cycle for computational storage device
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
    Memory::SimpleDRAM *pDRAM;
    Disk *pDisk;

    uint64_t lastResetStat;

    uint64_t clockSpeed;
    uint64_t clockPeriod;

    // Cores
    std::vector<Core> hilCore;
    std::vector<Core> iclCore;
    std::vector<Core> ftlCore;
    RISCV::SOC *riscv_soc;
    // CPIs
    std::unordered_map<uint16_t, std::unordered_map<uint16_t, InstStat>> cpi;

    uint32_t leastBusyCPU(std::vector<Core> &);
    void calculatePower(Power &);
    void RISCVCycle(); 
    uint32_t page_size = 16834; // Default page size for SimpleSSD
    uint32_t lba_size = 512;
    ftl_params fparams;
    icl_params iparams;
    std::queue<RISCVJob> req_queue;
    std::vector<std::pair<DMAFunction, void *>> callbacks;
    
    bool test_mode;
    TEST_TYPE test_type;
    uint32_t test_count;
    uint32_t test_size_min;
    uint32_t test_size_max;
    uint64_t last_start_tick = 0;
    uint64_t active_periods = 0;
    uint64_t active_duration = 0;
    uint64_t last_test_tick = 0;
  public:
    CPU(ConfigReader &, ICL::ICL *, FTL::FTL *, PAL::PAL *pal, Memory::SimpleDRAM *dram);
    ~CPU();

    void execute(NAMESPACE, FUNCTION, DMAFunction &, void * = nullptr,
                uint64_t = 0);
    uint64_t applyLatency(NAMESPACE, FUNCTION);

    void getStatList(std::vector<Stats> &, std::string) override;
    void getStatValues(std::vector<double> &) override;
    uint64_t getClockPeriod();
    void resetStatValues() override;
    void startRISCV();

    bool socIsPaused();
    void stopRISCV();
    void initRISCV();
    void initTests();
    void printLastStat();
    uint64_t read_flash_icl(uint8_t* buffer, uint64_t offset , uint64_t len); // for the flash functions
    uint64_t write_flash_icl(uint8_t* buffer, uint64_t offset , uint64_t len);
    uint64_t trim_flash_icl(uint8_t* buffer, uint64_t offset , uint64_t len);
    void read_flash_pal(uint8_t* request, uint64_t* tick);
    void write_flash_pal(uint8_t* request, uint64_t* tick);
    void erase_flash_pal(uint8_t* request, uint64_t* tick);
    void read_buffer(uint8_t *buffer, uint64_t req_info, uint64_t req_type);
    void write_buffer(uint8_t *buffer, uint64_t req_info, uint64_t req_type);
    void runDMA(uint64_t finished_at, uint64_t core_id);
    void setDisk(Disk *disk);
    void closeDisk();
    void addRISCVTask(char* input_cmd);
    uint64_t submitRead(HIL::Request *req, DMAFunction &func);
    uint64_t submitWrite(HIL::Request *req, DMAFunction &func);
    uint64_t submitTrim(HIL::Request *req, DMAFunction &func);
    uint64_t submitFormat(HIL::Request *req, DMAFunction &func);
    uint64_t submitFlush(HIL::Request *req, DMAFunction &func);

    void generateSequentialRead(uint64_t count, uint64_t min_size, uint64_t max_size);
    void generateSequentialWrite(uint64_t count, uint64_t min_size, uint64_t max_size);
    void generateSequentialIO(uint64_t count, uint64_t min_size, uint64_t max_size);
    void generateRandomRead(uint64_t count, uint64_t min_size, uint64_t max_size);
    void generateRandomWrite(uint64_t count, uint64_t min_size, uint64_t max_size);
    void generateRandomIO(uint64_t count, uint64_t min_size, uint64_t max_size);
    void generateSequentialTrim(uint64_t count, uint64_t min_size, uint64_t max_size);
    void generateSequentialFormat(uint64_t count, uint64_t min_size, uint64_t max_size);
    void generateSequentialFlush(uint64_t count, uint64_t min_size, uint64_t max_size);
    
};  

}  // namespace CPU

}  // namespace SimpleSSD


#endif
