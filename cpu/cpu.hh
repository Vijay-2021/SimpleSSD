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

#include "cpu/riscv_em/src/soc/riscv_example_soc.h"
#include "cpu/riscv_em/src/core/riscv_helper.h"
#include "cpu/riscv_em/src/peripherals/uart/simple_uart.h"
#include "util/simplessd.hh"
#include "util/disk.hh"

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

namespace CPU {

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
  Disk *pDisk;

  uint64_t lastResetStat;

  uint64_t clockSpeed;
  uint64_t clockPeriod;

  // Cores
  std::vector<Core> hilCore;
  std::vector<Core> iclCore;
  std::vector<Core> ftlCore;
  rv_soc_td csd;
  // CPIs
  std::unordered_map<uint16_t, std::unordered_map<uint16_t, InstStat>> cpi;

  uint32_t leastBusyCPU(std::vector<Core> &);
  void calculatePower(Power &);
  void csdCycle();
  bool csd_in_progress;
  uint32_t page_size = 16834; // Default page size for SimpleSSD
  uint32_t lba_size = 512;
  public:
    CPU(ConfigReader &, ICL::ICL *, FTL::FTL *, PAL::PAL *pal);
    ~CPU();

    void execute(NAMESPACE, FUNCTION, DMAFunction &, void * = nullptr,
                uint64_t = 0);
    uint64_t applyLatency(NAMESPACE, FUNCTION);

    void getStatList(std::vector<Stats> &, std::string) override;
    void getStatValues(std::vector<double> &) override;
    void resetStatValues() override;
    void startCSD();
    void stopCSD();
    void initCSD();
    void initFS();
    void printLastStat();
    uint64_t read_flash_icl(uint8_t* buffer, uint64_t offset , uint64_t len); // for the flash functions
    uint64_t write_flash_icl(uint8_t* buffer, uint64_t offset , uint64_t len);
    uint64_t read_flash_pal(uint8_t* buffer, uint64_t lpn , uint64_t ppn);
    uint64_t write_flash_pal(uint8_t* buffer, uint64_t lpn , uint64_t ppn);
    void setDisk(Disk *disk);
    void closeDisk();
    void addCSDTask(char* input_cmd);
};

}  // namespace CPU

}  // namespace SimpleSSD


#endif
