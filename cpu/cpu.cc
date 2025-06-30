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

#include "cpu/cpu.hh"

#include <limits>
#include <stddef.h>
#include "icl/icl.hh"
#include "ftl/ftl.hh"
#include "pal/pal.hh"

#include "sim/trace.hh"
#include <stdlib.h>

static uint64_t global_req_id = 0;

namespace SimpleSSD {

namespace CPU {

InstStat::_InstStat()
    : branch(0),
      load(0),
      store(0),
      arithmetic(0),
      floatingPoint(0),
      otherInsts(0),
      latency(0) {}

InstStat::_InstStat(uint64_t b, uint64_t l, uint64_t s, uint64_t a, uint64_t f,
                    uint64_t o, uint64_t c)
    : branch(b),
      load(l),
      store(s),
      arithmetic(a),
      floatingPoint(f),
      otherInsts(o) {
  latency = sum();
  latency *= c;
}

InstStat &InstStat::operator+=(const InstStat &rhs) {
  this->branch += rhs.branch;
  this->load += rhs.load;
  this->store += rhs.store;
  this->arithmetic += rhs.arithmetic;
  this->floatingPoint += rhs.floatingPoint;
  this->otherInsts += rhs.otherInsts;
  // Do not add up totalcycles

  return *this;
}

uint64_t InstStat::sum() {
  return branch + load + store + arithmetic + floatingPoint + otherInsts;
}

JobEntry::_JobEntry(DMAFunction &f, void *c, InstStat *i)
    : func(f), context(c), inst(i) {}

CPU::CoreStat::CoreStat() : busy(0) {}

CPU::Core::Core() : busy(false) {
  jobEvent = allocate([this](uint64_t) { jobDone(); });
}

CPU::Core::~Core() {}

bool CPU::CPU::Core::isBusy() {
  return busy;
}

uint64_t CPU::CPU::Core::getJobListSize() {
  return jobs.size();
}

CPU::CoreStat &CPU::Core::getStat() {
  return stat;
}

void CPU::Core::submitJob(JobEntry job, uint64_t delay) {
  job.delay = delay;
  job.submitAt = getTick();
  jobs.push(job);

  if (!busy) {
    handleJob();
  }
}

void CPU::Core::handleJob() {
  auto &iter = jobs.front();
  uint64_t now = getTick();
  uint64_t diff = now - iter.submitAt;
  uint64_t finishedAt;

  if (diff >= iter.delay) {
    finishedAt = now + iter.inst->latency;
  }
  else {
    finishedAt = now + iter.inst->latency + iter.delay - diff;
  }

  busy = true;

  schedule(jobEvent, finishedAt);
}

void CPU::Core::jobDone() {
  auto &iter = jobs.front();

  iter.func(getTick(), iter.context);
  stat.busy += iter.inst->latency;
  stat.instStat += *iter.inst;

  jobs.pop();
  busy = false;

  if (jobs.size() > 0) {
    handleJob();
  }
}

void CPU::Core::addStat(InstStat &inst) {
  stat.busy += inst.latency;
  stat.instStat += inst;
}

void CPU::RISCVCycle() {
  uint64_t next_tick = 0;
  uint32_t burst_cycles = conf.readUint(CONFIG_CPU, BURST_CYCLES);
  switch(RISCV::soc_run_mode_) {
    case RISCV::FAST_FORWARD_MODE:
      riscv_soc->rv_soc_run(); // don't schedule the next cycle, just run until the next stop signal
      break;
    case RISCV::CYCLE_MODE:
      riscv_soc->rv_soc_tick(1);
      schedule(RISCVCycleEvent, getTick() + clockPeriod);
      break;
    case RISCV::PAUSED_MODE:
      // Do nothing
      break;
    case RISCV::FAILED_MODE:
      debugprint(LOG_CPU, "RISCV core is in FAILED mode, stopping further execution");
      break;
    case RISCV::TIMING_MODE:
      next_tick = riscv_soc->rv_soc_tick(1);
      schedule(RISCVCycleEvent, next_tick);
      break;
    case RISCV::BURST_MODE:
      riscv_soc->rv_soc_tick(burst_cycles);
      schedule(RISCVCycleEvent, getTick() + burst_cycles * clockPeriod);
      break;
    default:
      panic("Invalid RISCV mode");
  }
}

CPU::CPU(ConfigReader &c, ICL::ICL *icl, FTL::FTL *ftl, PAL::PAL *pal, DRAM::AbstractDRAM *dram) : conf(c), pICL(icl), pFTL(ftl), pPAL(pal), pDRAM(dram), lastResetStat(0) {
  clockSpeed = conf.readUint(CONFIG_CPU, CPU_CLOCK);
  clockPeriod = 1000000000000. / clockSpeed;  // in pico-seconds
  hilCore.resize(conf.readUint(CONFIG_CPU, CPU_CORE_HIL));
  iclCore.resize(conf.readUint(CONFIG_CPU, CPU_CORE_ICL));
  ftlCore.resize(conf.readUint(CONFIG_CPU, CPU_CORE_FTL));
  uint64_t riscv_cores = conf.readUint(CONFIG_CPU, CPU_CORE_RISCV);
  riscv_soc = new RISCV::SOC(const_cast<char*>(conf.readString(CONFIG_CPU, CPU_FW_PATH).c_str()), nullptr, nullptr, this, riscv_cores, pDRAM); // TO-DO: make this less hacky
  RISCVCycleEvent = allocate([this](uint64_t) {
    RISCVCycle();
  });
  callbacks.resize(riscv_cores);
  page_size = pPAL->getInfo()->pageSize;
  pDisk = new Disk();
  
  fparams.totalPhysicalBlocks = pFTL->getInfo()->totalPhysicalBlocks;
  fparams.totalLogicalBlocks = pFTL->getInfo()->totalLogicalBlocks;
  fparams.pagesInBlock = pFTL->getInfo()->pagesInBlock;
  fparams.pageSize = pFTL->getInfo()->pageSize;
  fparams.ioUnitInPage = pFTL->getInfo()->ioUnitInPage;
  fparams.pageCountToMaxPerf = pFTL->getInfo()->pageCountToMaxPerf;
  fparams.bRandomTweak = conf.readBoolean(CONFIG_FTL, SimpleSSD::FTL::FTL_USE_RANDOM_IO_TWEAK);
  fparams.ftl_fill_ratio = conf.readFloat(CONFIG_FTL, SimpleSSD::FTL::FTL_FILL_RATIO);
  fparams.ftl_invalid_page_ratio = conf.readFloat(CONFIG_FTL, SimpleSSD::FTL::FTL_INVALID_PAGE_RATIO);
  fparams.ftl_filling_mode = (FTL::FILLING_MODE)conf.readInt(CONFIG_FTL, SimpleSSD::FTL::FTL_FILLING_MODE);
  fparams.ftl_gc_threshold_ratio = conf.readFloat(CONFIG_FTL, SimpleSSD::FTL::FTL_GC_THRESHOLD_RATIO);
  fparams.ftl_gc_mode = (FTL::GC_MODE)conf.readInt(CONFIG_FTL, SimpleSSD::FTL::FTL_GC_MODE);
  fparams.ftl_evict_policy = (FTL::EVICT_POLICY)conf.readInt(CONFIG_FTL, SimpleSSD::FTL::FTL_GC_EVICT_POLICY);
  fparams.choiceParam = conf.readInt(CONFIG_FTL, SimpleSSD::FTL::FTL_GC_D_CHOICE_PARAM);
  fparams.ftl_gc_reclaim_block = conf.readInt(CONFIG_FTL, SimpleSSD::FTL::FTL_GC_RECLAIM_BLOCK);
  fparams.ftl_gc_reclaim_threshold = conf.readFloat(CONFIG_FTL, SimpleSSD::FTL::FTL_GC_RECLAIM_BLOCK);
  fparams.bad_block_threshold = conf.readInt(CONFIG_FTL, SimpleSSD::FTL::FTL_BAD_BLOCK_THRESHOLD);
  
  iparams.pageSize = fparams.pageSize;
  iparams.pageCountToMaxPerf = fparams.pageCountToMaxPerf;
  iparams.ioUnitInPage = fparams.ioUnitInPage;
  iparams.waySize = conf.readUint(CONFIG_ICL, ICL::ICL_WAY_SIZE);
  iparams.prefetchCount = conf.readUint(CONFIG_ICL, ICL::ICL_PREFETCH_COUNT);
  iparams.prefetchRatio = conf.readFloat(CONFIG_ICL, ICL::ICL_PREFETCH_RATIO);
  iparams.useReadCaching = conf.readBoolean(CONFIG_ICL, ICL::ICL_USE_READ_CACHE);
  iparams.useWriteCaching = conf.readBoolean(CONFIG_ICL, ICL::ICL_USE_WRITE_CACHE);
  iparams.useReadPrefetch = conf.readBoolean(CONFIG_ICL, ICL::ICL_USE_READ_PREFETCH);
  iparams.useRandomIOTweak = fparams.bRandomTweak;
  iparams.cacheSize = conf.readUint(CONFIG_ICL, ICL::ICL_CACHE_SIZE);
  iparams.iclEvictGranularity = (ICL::EVICT_MODE)conf.readInt(CONFIG_ICL, ICL::ICL_EVICT_GRANULARITY);
  iparams.iclPrefetchGranularity = (ICL::PREFETCH_MODE)conf.readInt(CONFIG_ICL, ICL::ICL_PREFETCH_GRANULARITY);
  debugprint(LOG_CPU, "page size is %u, page count to max perf is %u, io unit in page is %u and waySize is %u and prefetch count is %u and prefetch ratio is %f and useReadCaching is %u, useWriteCaching is %u, useReadPrefetch is %u, useRandomIOTweak is %u, cache size is %" PRIu64 " and iclEvictGranularity is %d and iclPrefetchGranularity is %d",
             iparams.pageSize, iparams.pageCountToMaxPerf, iparams.ioUnitInPage, iparams.waySize, iparams.prefetchCount, iparams.prefetchRatio,
             (uint32_t)iparams.useReadCaching, (uint32_t)iparams.useWriteCaching, (uint32_t)iparams.useReadPrefetch, (uint32_t)iparams.useRandomIOTweak,
             iparams.cacheSize, (int)iparams.iclEvictGranularity, (int)iparams.iclPrefetchGranularity);
  // unsigned char buffer[256];
  // read_flash(buffer, 4096, 4096);
  // schedule(RISCVCycleEvent, getTick()); // start the riscv core(s) as soon as possible
  // Initialize CPU table
  cpi.insert({FTL, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({FTL__PAGE_MAPPING, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({ICL, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({ICL__GENERIC_CACHE, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({HIL, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({NVME__CONTROLLER, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({NVME__PRPLIST, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({NVME__SGL, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({NVME__SUBSYSTEM, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({NVME__NAMESPACE, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({NVME__OCSSD, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({UFS__DEVICE, std::unordered_map<uint16_t, InstStat>()});
  cpi.insert({SATA__DEVICE, std::unordered_map<uint16_t, InstStat>()});

  // Insert item (Use cpu/generator/generate.py to generate this code)
  cpi.find(0)->second.insert({0, InstStat(5, 32, 6, 13, 0, 1, clockPeriod)});
  cpi.find(0)->second.insert({1, InstStat(5, 32, 6, 13, 0, 1, clockPeriod)});
  cpi.find(0)->second.insert({3, InstStat(5, 32, 6, 13, 0, 1, clockPeriod)});
  cpi.find(0)->second.insert({4, InstStat(4, 24, 4, 6, 0, 0, clockPeriod)});
  cpi.find(1)->second.insert({0, InstStat(8, 28, 7, 18, 0, 1, clockPeriod)});
  cpi.find(1)->second.insert({1, InstStat(8, 28, 7, 19, 0, 0, clockPeriod)});
  cpi.find(1)->second.insert({3, InstStat(4, 28, 6, 11, 0, 0, clockPeriod)});
  cpi.find(1)->second.insert(
      {4, InstStat(63, 180, 21, 147, 0, 2, clockPeriod)});
  cpi.find(1)->second.insert(
      {9, InstStat(177, 504, 113, 415, 118, 19, clockPeriod)});
  cpi.find(1)->second.insert(
      {10, InstStat(157, 616, 102, 338, 0, 2, clockPeriod)});
  cpi.find(1)->second.insert(
      {5, InstStat(45, 180, 15, 155, 0, 0, clockPeriod)});
  cpi.find(1)->second.insert(
      {6, InstStat(133, 452, 54, 377, 91, 1, clockPeriod)});
  cpi.find(1)->second.insert(
      {8, InstStat(34, 140, 10, 146, 0, 0, clockPeriod)});
  cpi.find(1)->second.insert(
      {7, InstStat(120, 236, 86, 260, 0, 1, clockPeriod)});
  cpi.find(2)->second.insert({0, InstStat(8, 88, 17, 27, 0, 1, clockPeriod)});
  cpi.find(2)->second.insert({1, InstStat(8, 88, 17, 27, 0, 1, clockPeriod)});
  cpi.find(2)->second.insert({2, InstStat(5, 40, 6, 12, 0, 0, clockPeriod)});
  cpi.find(2)->second.insert({3, InstStat(5, 40, 6, 12, 0, 0, clockPeriod)});
  cpi.find(2)->second.insert({4, InstStat(5, 40, 6, 12, 0, 0, clockPeriod)});
  cpi.find(3)->second.insert(
      {0, InstStat(90, 532, 64, 284, 0, 1, clockPeriod)});
  cpi.find(3)->second.insert(
      {1, InstStat(82, 496, 53, 312, 0, 5, clockPeriod)});
  cpi.find(3)->second.insert({2, InstStat(22, 120, 20, 59, 0, 2, clockPeriod)});
  cpi.find(3)->second.insert({3, InstStat(22, 120, 20, 61, 0, 2, clockPeriod)});
  cpi.find(3)->second.insert({4, InstStat(9, 72, 12, 86, 0, 1, clockPeriod)});
  cpi.find(4)->second.insert(
      {0, InstStat(61, 312, 102, 120, 0, 2, clockPeriod)});
  cpi.find(4)->second.insert(
      {1, InstStat(61, 312, 102, 120, 0, 2, clockPeriod)});
  cpi.find(4)->second.insert({2, InstStat(27, 100, 27, 49, 0, 1, clockPeriod)});
  cpi.find(5)->second.insert(
      {14, InstStat(44, 164, 32, 68, 0, 2, clockPeriod)});
  cpi.find(5)->second.insert({13, InstStat(0, 0, 0, 0, 0, 0, clockPeriod)});
  cpi.find(5)->second.insert(
      {16, InstStat(136, 360, 65, 230, 0, 3, clockPeriod)});
  cpi.find(5)->second.insert(
      {15, InstStat(54, 140, 36, 91, 0, 8, clockPeriod)});
  cpi.find(5)->second.insert({11, InstStat(0, 0, 0, 0, 0, 0, clockPeriod)});
  cpi.find(5)->second.insert({12, InstStat(0, 0, 0, 0, 0, 0, clockPeriod)});
  cpi.find(6)->second.insert(
      {17, InstStat(41, 168, 42, 75, 0, 1, clockPeriod)});
  cpi.find(6)->second.insert(
      {0, InstStat(99, 456, 94, 177, 0, 6, clockPeriod)});
  cpi.find(6)->second.insert(
      {1, InstStat(99, 456, 94, 177, 0, 6, clockPeriod)});
  cpi.find(7)->second.insert(
      {18, InstStat(44, 152, 35, 78, 0, 2, clockPeriod)});
  cpi.find(7)->second.insert(
      {0, InstStat(99, 456, 94, 177, 0, 6, clockPeriod)});
  cpi.find(7)->second.insert(
      {1, InstStat(99, 456, 94, 177, 0, 6, clockPeriod)});
  cpi.find(8)->second.insert(
      {19, InstStat(119, 220, 45, 160, 0, 6, clockPeriod)});
  cpi.find(8)->second.insert({20, InstStat(4, 40, 14, 110, 0, 1, clockPeriod)});
  cpi.find(8)->second.insert(
      {21, InstStat(70, 200, 42, 161, 0, 1, clockPeriod)});
  cpi.find(9)->second.insert({19, InstStat(27, 44, 5, 37, 0, 0, clockPeriod)});
  cpi.find(9)->second.insert(
      {0, InstStat(82, 292, 42, 128, 0, 4, clockPeriod)});
  cpi.find(9)->second.insert(
      {1, InstStat(86, 304, 47, 141, 0, 3, clockPeriod)});
  cpi.find(9)->second.insert({2, InstStat(51, 124, 28, 78, 0, 3, clockPeriod)});
  cpi.find(9)->second.insert(
      {22, InstStat(131, 364, 71, 200, 0, 7, clockPeriod)});
  cpi.find(10)->second.insert(
      {19, InstStat(155, 100, 12, 208, 0, 4, clockPeriod)});
  cpi.find(10)->second.insert(
      {0, InstStat(93, 284, 60, 146, 0, 5, clockPeriod)});
  cpi.find(10)->second.insert(
      {1, InstStat(95, 276, 60, 150, 0, 4, clockPeriod)});
  cpi.find(10)->second.insert(
      {22, InstStat(119, 328, 76, 186, 0, 4, clockPeriod)});
  cpi.find(10)->second.insert(
      {5, InstStat(54, 172, 69, 89, 0, 1, clockPeriod)});
  cpi.find(10)->second.insert(
      {6, InstStat(72, 236, 77, 141, 0, 3, clockPeriod)});
  cpi.find(10)->second.insert(
      {7, InstStat(68, 204, 77, 116, 0, 1, clockPeriod)});
  cpi.find(10)->second.insert(
      {20, InstStat(65, 388, 63, 303, 0, 1, clockPeriod)});
  cpi.find(10)->second.insert(
      {23, InstStat(128, 368, 76, 204, 0, 4, clockPeriod)});
  cpi.find(10)->second.insert(
      {24, InstStat(128, 384, 81, 209, 0, 6, clockPeriod)});
  cpi.find(10)->second.insert(
      {25, InstStat(69, 184, 43, 112, 0, 4, clockPeriod)});
  cpi.find(10)->second.insert(
      {26, InstStat(206, 692, 157, 315, 0, 5, clockPeriod)});
  cpi.find(10)->second.insert(
      {27, InstStat(183, 620, 154, 284, 0, 6, clockPeriod)});
  cpi.find(10)->second.insert(
      {28, InstStat(162, 460, 78, 227, 0, 4, clockPeriod)});
  cpi.find(11)->second.insert(
      {29, InstStat(51, 132, 40, 97, 0, 0, clockPeriod)});
  cpi.find(11)->second.insert(
      {30, InstStat(212, 460, 117, 491, 0, 9, clockPeriod)});
  cpi.find(11)->second.insert(
      {31, InstStat(42, 172, 43, 74, 0, 2, clockPeriod)});
  cpi.find(11)->second.insert(
      {32, InstStat(42, 172, 43, 74, 0, 2, clockPeriod)});
  cpi.find(11)->second.insert({0, InstStat(29, 76, 17, 51, 0, 2, clockPeriod)});
  cpi.find(11)->second.insert({1, InstStat(29, 76, 17, 51, 0, 2, clockPeriod)});
  cpi.find(11)->second.insert({2, InstStat(25, 64, 18, 44, 0, 1, clockPeriod)});
  cpi.find(12)->second.insert(
      {19, InstStat(157, 352, 69, 178, 0, 1, clockPeriod)});
  cpi.find(12)->second.insert(
      {31, InstStat(42, 172, 43, 73, 0, 3, clockPeriod)});
  cpi.find(12)->second.insert(
      {32, InstStat(42, 172, 43, 73, 0, 3, clockPeriod)});
  cpi.find(12)->second.insert(
      {0, InstStat(28, 84, 23, 119, 0, 0, clockPeriod)});
  cpi.find(12)->second.insert(
      {1, InstStat(28, 84, 23, 120, 0, 1, clockPeriod)});
  cpi.find(12)->second.insert({2, InstStat(25, 64, 18, 44, 0, 1, clockPeriod)});
  cpi.find(12)->second.insert(
      {33, InstStat(57, 212, 36, 128, 0, 3, clockPeriod)});
  cpi.find(12)->second.insert(
      {34, InstStat(34, 116, 29, 72, 0, 3, clockPeriod)});
  cpi.find(12)->second.insert({35, InstStat(16, 64, 9, 38, 0, 1, clockPeriod)});
  cpi.find(12)->second.insert(
      {36, InstStat(28, 72, 15, 48, 0, 2, clockPeriod)});
  cpi.find(12)->second.insert(
      {37, InstStat(57, 212, 36, 127, 0, 3, clockPeriod)});
  cpi.find(12)->second.insert(
      {38, InstStat(34, 128, 31, 68, 0, 2, clockPeriod)});
  cpi.find(12)->second.insert(
      {39, InstStat(18, 56, 10, 37, 0, 1, clockPeriod)});
  cpi.find(12)->second.insert(
      {40, InstStat(33, 100, 17, 61, 0, 3, clockPeriod)});
}

CPU::~CPU() {
  delete riscv_soc;
}

void CPU::initRISCV() {
  RISCV::soc_run_mode_ = RISCV::SOC_RUN_MODE::FAST_FORWARD_MODE;
  schedule(RISCVCycleEvent, getTick());  
}

void CPU::startRISCV() {
  debugprint(LOG_CPU, "start riscv called\n");
  RISCV::soc_run_mode_ = RISCV::SOC_RUN_MODE::TIMING_MODE;
  schedule(RISCVCycleEvent, getTick());
}

void CPU::stopRISCV() {
  debugprint(LOG_CPU, "stop riscv called\n");
  uint64_t RISCVCycleTick = 0;
  scheduled(RISCVCycleEvent, &RISCVCycleTick);
  if (RISCVCycleTick >= getTick()) {
    deschedule(RISCVCycleEvent);
  }
  RISCV::soc_run_mode_ = RISCV::SOC_RUN_MODE::PAUSED_MODE;
}

void CPU::calculatePower(Power &power) {
  // Print stats before die
  ParseXML param;
  uint64_t simCycle = (getTick() - lastResetStat) / clockPeriod;
  uint32_t totalCore = hilCore.size() + iclCore.size() + ftlCore.size();
  uint32_t coreIdx = 0;

  param.initialize();

  // system
  {
    param.sys.number_of_L1Directories = 0;
    param.sys.number_of_L2Directories = 0;
    param.sys.number_of_L2s = 1;
    param.sys.Private_L2 = 0;
    param.sys.number_of_L3s = 0;
    param.sys.number_of_NoCs = 0;
    param.sys.homogeneous_cores = 0;
    param.sys.homogeneous_L2s = 1;
    param.sys.homogeneous_L1Directories = 1;
    param.sys.homogeneous_L2Directories = 1;
    param.sys.homogeneous_L3s = 1;
    param.sys.homogeneous_ccs = 1;
    param.sys.homogeneous_NoCs = 1;
    param.sys.core_tech_node = 40;
    param.sys.target_core_clockrate = clockSpeed / 1000000;
    param.sys.temperature = 340;
    param.sys.number_cache_levels = 2;
    param.sys.interconnect_projection_type = 1;
    param.sys.device_type = 0;
    param.sys.longer_channel_device = 1;
    param.sys.Embedded = 1;
    param.sys.opt_clockrate = 1;
    param.sys.machine_bits = 64;
    param.sys.virtual_address_width = 48;
    param.sys.physical_address_width = 48;
    param.sys.virtual_memory_page_size = 4096;
    param.sys.total_cycles = simCycle;
    param.sys.number_of_cores = totalCore;
  }

  // Now, we are using heterogeneous cores
  for (coreIdx = 0; coreIdx < totalCore; coreIdx++) {
    // system.core
    {
      param.sys.core[coreIdx].clock_rate = param.sys.target_core_clockrate;
      param.sys.core[coreIdx].opt_local = 0;
      param.sys.core[coreIdx].instruction_length = 32;
      param.sys.core[coreIdx].opcode_width = 7;
      param.sys.core[coreIdx].x86 = 0;
      param.sys.core[coreIdx].micro_opcode_width = 8;
      param.sys.core[coreIdx].machine_type = 0;
      param.sys.core[coreIdx].number_hardware_threads = 1;
      param.sys.core[coreIdx].fetch_width = 2;
      param.sys.core[coreIdx].number_instruction_fetch_ports = 1;
      param.sys.core[coreIdx].decode_width = 2;
      param.sys.core[coreIdx].issue_width = 4;
      param.sys.core[coreIdx].peak_issue_width = 7;
      param.sys.core[coreIdx].commit_width = 4;
      param.sys.core[coreIdx].fp_issue_width = 1;
      param.sys.core[coreIdx].prediction_width = 0;
      param.sys.core[coreIdx].pipelines_per_core[0] = 1;
      param.sys.core[coreIdx].pipelines_per_core[1] = 1;
      param.sys.core[coreIdx].pipeline_depth[0] = 8;
      param.sys.core[coreIdx].pipeline_depth[1] = 8;
      param.sys.core[coreIdx].ALU_per_core = 3;
      param.sys.core[coreIdx].MUL_per_core = 1;
      param.sys.core[coreIdx].FPU_per_core = 1;
      param.sys.core[coreIdx].instruction_buffer_size = 32;
      param.sys.core[coreIdx].decoded_stream_buffer_size = 16;
      param.sys.core[coreIdx].instruction_window_scheme = 0;
      param.sys.core[coreIdx].instruction_window_size = 20;
      param.sys.core[coreIdx].fp_instruction_window_size = 15;
      param.sys.core[coreIdx].ROB_size = 0;
      param.sys.core[coreIdx].archi_Regs_IRF_size = 32;
      param.sys.core[coreIdx].archi_Regs_FRF_size = 32;
      param.sys.core[coreIdx].phy_Regs_IRF_size = 64;
      param.sys.core[coreIdx].phy_Regs_FRF_size = 64;
      param.sys.core[coreIdx].rename_scheme = 0;
      param.sys.core[coreIdx].checkpoint_depth = 1;
      param.sys.core[coreIdx].register_windows_size = 0;
      strcpy(param.sys.core[coreIdx].LSU_order, "inorder");
      param.sys.core[coreIdx].store_buffer_size = 4;
      param.sys.core[coreIdx].load_buffer_size = 0;
      param.sys.core[coreIdx].memory_ports = 1;
      param.sys.core[coreIdx].RAS_size = 0;
      param.sys.core[coreIdx].number_of_BPT = 2;
      param.sys.core[coreIdx].number_of_BTB = 2;
    }

    // system.core.itlb
    param.sys.core[coreIdx].itlb.number_entries = 64;

    // system.core.icache
    {
      param.sys.core[coreIdx].icache.icache_config[0] = 32768;
      param.sys.core[coreIdx].icache.icache_config[1] = 8;
      param.sys.core[coreIdx].icache.icache_config[2] = 4;
      param.sys.core[coreIdx].icache.icache_config[3] = 1;
      param.sys.core[coreIdx].icache.icache_config[4] = 10;
      param.sys.core[coreIdx].icache.icache_config[5] = 10;
      param.sys.core[coreIdx].icache.icache_config[6] = 32;
      param.sys.core[coreIdx].icache.icache_config[7] = 0;
      param.sys.core[coreIdx].icache.buffer_sizes[0] = 4;
      param.sys.core[coreIdx].icache.buffer_sizes[1] = 4;
      param.sys.core[coreIdx].icache.buffer_sizes[2] = 4;
      param.sys.core[coreIdx].icache.buffer_sizes[3] = 0;
    }

    // system.core dtlb
    param.sys.core[coreIdx].dtlb.number_entries = 64;

    // system.core.dcache
    {
      param.sys.core[coreIdx].dcache.dcache_config[0] = 32768;
      param.sys.core[coreIdx].dcache.dcache_config[1] = 8;
      param.sys.core[coreIdx].dcache.dcache_config[2] = 4;
      param.sys.core[coreIdx].dcache.dcache_config[3] = 1;
      param.sys.core[coreIdx].dcache.dcache_config[4] = 10;
      param.sys.core[coreIdx].dcache.dcache_config[5] = 10;
      param.sys.core[coreIdx].dcache.dcache_config[6] = 32;
      param.sys.core[coreIdx].dcache.dcache_config[7] = 0;
      param.sys.core[coreIdx].dcache.buffer_sizes[0] = 4;
      param.sys.core[coreIdx].dcache.buffer_sizes[1] = 4;
      param.sys.core[coreIdx].dcache.buffer_sizes[2] = 4;
      param.sys.core[coreIdx].dcache.buffer_sizes[3] = 4;
    }

    // system.core.BTB
    param.sys.core[coreIdx].BTB.BTB_config[0] = 4096;
    param.sys.core[coreIdx].BTB.BTB_config[1] = 4;
    param.sys.core[coreIdx].BTB.BTB_config[2] = 2;
    param.sys.core[coreIdx].BTB.BTB_config[3] = 2;
    param.sys.core[coreIdx].BTB.BTB_config[4] = 1;
    param.sys.core[coreIdx].BTB.BTB_config[5] = 1;
  }

  // system.L2
  {
    param.sys.L2[0].L2_config[0] = 1048576;
    param.sys.L2[0].L2_config[1] = 32;
    param.sys.L2[0].L2_config[2] = 8;
    param.sys.L2[0].L2_config[3] = 8;
    param.sys.L2[0].L2_config[4] = 8;
    param.sys.L2[0].L2_config[5] = 23;
    param.sys.L2[0].L2_config[6] = 32;
    param.sys.L2[0].L2_config[7] = 1;
    param.sys.L2[0].buffer_sizes[0] = 16;
    param.sys.L2[0].buffer_sizes[1] = 16;
    param.sys.L2[0].buffer_sizes[2] = 16;
    param.sys.L2[0].buffer_sizes[3] = 16;
    param.sys.L2[0].clockrate = param.sys.target_core_clockrate;
    param.sys.L2[0].ports[0] = 1;
    param.sys.L2[0].ports[1] = 1;
    param.sys.L2[0].ports[2] = 1;
    param.sys.L2[0].device_type = 0;
  }

  // Etcetera
  param.sys.mc.req_window_size_per_channel = 32;

  // Apply stat values

  // Core stat
  coreIdx = 0;

  for (auto &core : hilCore) {
    CoreStat &stat = core.getStat();

    param.sys.core[coreIdx].total_instructions = stat.instStat.sum();
    param.sys.core[coreIdx].int_instructions = stat.instStat.arithmetic;
    param.sys.core[coreIdx].fp_instructions = stat.instStat.floatingPoint;
    param.sys.core[coreIdx].branch_instructions = stat.instStat.branch;
    param.sys.core[coreIdx].load_instructions = stat.instStat.load;
    param.sys.core[coreIdx].store_instructions = stat.instStat.store;
    param.sys.core[coreIdx].busy_cycles = stat.busy / clockPeriod;

    coreIdx++;
  }

  for (auto &core : iclCore) {
    CoreStat &stat = core.getStat();

    param.sys.core[coreIdx].total_instructions = stat.instStat.sum();
    param.sys.core[coreIdx].int_instructions = stat.instStat.arithmetic;
    param.sys.core[coreIdx].fp_instructions = stat.instStat.floatingPoint;
    param.sys.core[coreIdx].branch_instructions = stat.instStat.branch;
    param.sys.core[coreIdx].load_instructions = stat.instStat.load;
    param.sys.core[coreIdx].store_instructions = stat.instStat.store;
    param.sys.core[coreIdx].busy_cycles = stat.busy / clockPeriod;

    coreIdx++;
  }

  for (auto &core : ftlCore) {
    CoreStat &stat = core.getStat();

    param.sys.core[coreIdx].total_instructions = stat.instStat.sum();
    param.sys.core[coreIdx].int_instructions = stat.instStat.arithmetic;
    param.sys.core[coreIdx].fp_instructions = stat.instStat.floatingPoint;
    param.sys.core[coreIdx].branch_instructions = stat.instStat.branch;
    param.sys.core[coreIdx].load_instructions = stat.instStat.load;
    param.sys.core[coreIdx].store_instructions = stat.instStat.store;
    param.sys.core[coreIdx].busy_cycles = stat.busy / clockPeriod;

    coreIdx++;
  };

  for (coreIdx = 0; coreIdx < totalCore; coreIdx++) {
    param.sys.core[coreIdx].total_cycles = simCycle;
    param.sys.core[coreIdx].idle_cycles =
        simCycle - param.sys.core[coreIdx].busy_cycles;
    param.sys.core[coreIdx].committed_instructions =
        param.sys.core[coreIdx].total_instructions;
    param.sys.core[coreIdx].committed_int_instructions =
        param.sys.core[coreIdx].int_instructions;
    param.sys.core[coreIdx].committed_fp_instructions =
        param.sys.core[coreIdx].fp_instructions;
    param.sys.core[coreIdx].pipeline_duty_cycle = 1;
    param.sys.core[coreIdx].IFU_duty_cycle = 0.9;
    param.sys.core[coreIdx].BR_duty_cycle = 0.72;
    param.sys.core[coreIdx].LSU_duty_cycle = 0.71;
    param.sys.core[coreIdx].MemManU_I_duty_cycle = 0.9;
    param.sys.core[coreIdx].MemManU_D_duty_cycle = 0.71;
    param.sys.core[coreIdx].ALU_duty_cycle = 0.76;
    param.sys.core[coreIdx].MUL_duty_cycle = 0.82;
    param.sys.core[coreIdx].FPU_duty_cycle = 0.0;
    param.sys.core[coreIdx].ALU_cdb_duty_cycle = 0.76;
    param.sys.core[coreIdx].MUL_cdb_duty_cycle = 0.82;
    param.sys.core[coreIdx].FPU_cdb_duty_cycle = 0.0;
    param.sys.core[coreIdx].ialu_accesses = param.sys.core[0].int_instructions;
    param.sys.core[coreIdx].fpu_accesses = param.sys.core[0].fp_instructions;
    param.sys.core[coreIdx].mul_accesses =
        param.sys.core[0].int_instructions * .5;
    param.sys.core[coreIdx].int_regfile_reads =
        param.sys.core[0].load_instructions;
    param.sys.core[coreIdx].float_regfile_reads =
        param.sys.core[coreIdx].fp_instructions * .4;
    param.sys.core[coreIdx].int_regfile_writes =
        param.sys.core[0].store_instructions;
    param.sys.core[coreIdx].float_regfile_writes =
        param.sys.core[coreIdx].fp_instructions * .4;

    // L1i and L1d
    {
      auto &core = param.sys.core[coreIdx];

      core.icache.total_accesses = core.load_instructions * .3;
      core.icache.total_hits = core.icache.total_accesses * .7;
      core.icache.total_misses = core.icache.total_accesses * .3;
      core.icache.read_accesses = core.icache.total_accesses;
      core.icache.read_hits = core.icache.total_hits;
      core.icache.read_misses = core.icache.total_misses;
      core.itlb.total_accesses = core.load_instructions * .2;
      core.itlb.total_hits = core.itlb.total_accesses * .8;
      core.itlb.total_misses = core.itlb.total_accesses * .2;

      core.dcache.total_accesses = core.load_instructions * .4;
      core.dcache.total_hits = core.dcache.total_accesses * .4;
      core.dcache.total_misses = core.dcache.total_accesses * .6;
      core.dcache.read_accesses = core.dcache.total_accesses * .6;
      core.dcache.read_hits = core.dcache.total_hits * .6;
      core.dcache.read_misses = core.dcache.total_misses * .6;
      core.dcache.write_accesses = core.dcache.total_accesses * .4;
      core.dcache.write_hits = core.dcache.total_hits * .4;
      core.dcache.write_misses = core.dcache.total_misses * .4;
      core.dcache.write_backs = core.dcache.total_misses * .4;
      core.dtlb.total_accesses = core.load_instructions * .2;
      core.dtlb.total_hits = core.itlb.total_accesses * .8;
      core.dtlb.total_misses = core.itlb.total_accesses * .2;
    }
  }

  // L2
  param.sys.L2[0].duty_cycle = 1.;

  if (param.sys.number_of_L2s > 0) {
    auto &L2 = param.sys.L2[0];

    L2.total_accesses = param.sys.core[0].dcache.write_backs;
    L2.total_hits = L2.total_accesses * .4;
    L2.total_misses = L2.total_accesses * .6;
    L2.read_accesses = L2.total_accesses * .5;
    L2.read_hits = L2.total_hits * .5;
    L2.read_misses = L2.total_misses * .5;
    L2.write_accesses = L2.total_accesses * .5;
    L2.write_hits = L2.total_hits * .5;
    L2.write_misses = L2.total_misses * .5;
    L2.write_backs = L2.total_misses * .4;
  }
  else {
    param.sys.L2[0].total_accesses = 0;
    param.sys.L2[0].total_hits = 0;
    param.sys.L2[0].total_misses = 0;
    param.sys.L2[0].read_accesses = 0;
    param.sys.L2[0].read_hits = 0;
    param.sys.L2[0].read_misses = 0;
    param.sys.L2[0].write_accesses = 0;
    param.sys.L2[0].write_hits = 0;
    param.sys.L2[0].write_misses = 0;
    param.sys.L2[0].write_backs = 0;
  }

  // L3
  param.sys.L3[0].duty_cycle = 1.;

  if (param.sys.number_of_L3s > 0) {
    auto &L3 = param.sys.L3[0];

    L3.total_accesses = param.sys.L2[0].write_backs;
    L3.total_hits = L3.total_accesses * .4;
    L3.total_misses = L3.total_accesses * .6;
    L3.read_accesses = L3.total_accesses * .5;
    L3.read_hits = L3.total_hits * .5;
    L3.read_misses = L3.total_misses * .5;
    L3.write_accesses = L3.total_accesses * .5;
    L3.write_hits = L3.total_hits * .5;
    L3.write_misses = L3.total_misses * .5;
    L3.write_backs = L3.total_misses * .4;
  }
  else {
    param.sys.L3[0].total_accesses = 0;
    param.sys.L3[0].total_hits = 0;
    param.sys.L3[0].total_misses = 0;
    param.sys.L3[0].read_accesses = 0;
    param.sys.L3[0].read_hits = 0;
    param.sys.L3[0].read_misses = 0;
    param.sys.L3[0].write_accesses = 0;
    param.sys.L3[0].write_hits = 0;
    param.sys.L3[0].write_misses = 0;
    param.sys.L3[0].write_backs = 0;
  }

  McPAT mcpat(&param);

  mcpat.getPower(power);
}

uint32_t CPU::leastBusyCPU(std::vector<Core> &list) {
  uint32_t idx = list.size();
  uint64_t busymin = std::numeric_limits<uint64_t>::max();

  for (uint32_t i = 0; i < list.size(); i++) {
    auto &core = list.at(i);
    auto busy = core.getStat().busy;

    if (!core.isBusy() && busy < busymin) {
      busymin = busy;
      idx = i;
    }
  }

  if (idx == list.size()) {
    uint64_t jobmin = std::numeric_limits<uint64_t>::max();

    for (uint32_t i = 0; i < list.size(); i++) {
      auto jobs = list.at(i).getJobListSize();

      if (jobs <= jobmin) {
        jobmin = jobs;
        idx = i;
      }
    }
  }

  return idx;
}

void CPU::execute(NAMESPACE ns, FUNCTION fct, DMAFunction &func, void *context,
                  uint64_t delay) {
  Core *pCore = nullptr;

  // Find dedicated core
  switch (ns) {
    case FTL:
    case FTL__PAGE_MAPPING:
      if (ftlCore.size() > 0) {
        pCore = &ftlCore.at(leastBusyCPU(ftlCore));
      }

      break;
    case ICL:
    case ICL__GENERIC_CACHE:
      if (iclCore.size() > 0) {
        pCore = &iclCore.at(leastBusyCPU(iclCore));
      }

      break;
    case HIL:
    case NVME__CONTROLLER:
    case NVME__PRPLIST:
    case NVME__SGL:
    case NVME__SUBSYSTEM:
    case NVME__NAMESPACE:
    case NVME__OCSSD:
    case UFS__DEVICE:
    case SATA__DEVICE:
      if (hilCore.size() > 0) {
        pCore = &hilCore.at(leastBusyCPU(hilCore));
      }

      break;
    default:
      panic("Undefined function namespace %u", ns);

      break;
  }

  if (pCore) {
    // Get CPI table
    auto table = cpi.find(ns);

    if (table == cpi.end()) {
      panic("Namespace %u not defined in CPI table", ns);
    }

    // Get CPI
    auto inst = table->second.find(fct);

    if (inst == table->second.end()) {
      panic("Namespace %u does not have function %u", ns, fct);
    }

    pCore->submitJob(JobEntry(func, context, &inst->second), delay);
  }
  else {
    func(getTick(), context);
  }
  
}

uint64_t CPU::applyLatency(NAMESPACE ns, FUNCTION fct) {
  Core *pCore = nullptr;

  // Find dedicated core
  switch (ns) {
    case FTL:
    case FTL__PAGE_MAPPING:
      if (ftlCore.size() > 0) {
        pCore = &ftlCore.at(leastBusyCPU(ftlCore));
      }

      break;
    case ICL:
    case ICL__GENERIC_CACHE:
      if (iclCore.size() > 0) {
        pCore = &iclCore.at(leastBusyCPU(iclCore));
      }

      break;
    case HIL:
    case NVME__CONTROLLER:
    case NVME__PRPLIST:
    case NVME__SGL:
    case NVME__SUBSYSTEM:
    case NVME__NAMESPACE:
    case NVME__OCSSD:
    case UFS__DEVICE:
    case SATA__DEVICE:
      if (hilCore.size() > 0) {
        pCore = &hilCore.at(leastBusyCPU(hilCore));
      }

      break;
    default:
      panic("Undefined function namespace %u", ns);

      break;
  }

  if (pCore) {
    // Get CPI table
    auto table = cpi.find(ns);

    if (table == cpi.end()) {
      panic("Namespace %u not defined in CPI table", ns);
    }

    // Get CPI
    auto inst = table->second.find(fct);

    if (inst == table->second.end()) {
      panic("Namespace %u does not have function %u", ns, fct);
    }

    pCore->addStat(inst->second);

    return inst->second.latency;
  }

  return 0;
}

void CPU::getStatList(std::vector<Stats> &list, std::string prefix) {
  Stats temp;
  std::string number;

  for (uint32_t i = 0; i < hilCore.size(); i++) {
    number = std::to_string(i);

    temp.name = prefix + ".hil" + number + ".busy";
    temp.desc = "CPU for HIL core " + number + " busy ticks";
    list.push_back(temp);

    temp.name = prefix + ".hil" + number + ".insts.branch";
    temp.desc = "CPU for HIL core " + number + " executed branch instructions";
    list.push_back(temp);

    temp.name = prefix + ".hil" + number + ".insts.load";
    temp.desc = "CPU for HIL core " + number + " executed load instructions";
    list.push_back(temp);

    temp.name = prefix + ".hil" + number + ".insts.store";
    temp.desc = "CPU for HIL core " + number + " executed store instructions";
    list.push_back(temp);

    temp.name = prefix + ".hil" + number + ".insts.arithmetic";
    temp.desc =
        "CPU for HIL core " + number + " executed arithmetic instructions";
    list.push_back(temp);

    temp.name = prefix + ".hil" + number + ".insts.fp";
    temp.desc =
        "CPU for HIL core " + number + " executed floating point instructions";
    list.push_back(temp);

    temp.name = prefix + ".hil" + number + ".insts.others";
    temp.desc = "CPU for HIL core " + number + " executed other instructions";
    list.push_back(temp);
  }

  for (uint32_t i = 0; i < iclCore.size(); i++) {
    number = std::to_string(i);

    temp.name = prefix + ".icl" + number + ".busy";
    temp.desc = "CPU for ICL core " + number + " busy ticks";
    list.push_back(temp);

    temp.name = prefix + ".icl" + number + ".insts.branch";
    temp.desc = "CPU for ICL core " + number + " executed branch instructions";
    list.push_back(temp);

    temp.name = prefix + ".icl" + number + ".insts.load";
    temp.desc = "CPU for ICL core " + number + " executed load instructions";
    list.push_back(temp);

    temp.name = prefix + ".icl" + number + ".insts.store";
    temp.desc = "CPU for ICL core " + number + " executed store instructions";
    list.push_back(temp);

    temp.name = prefix + ".icl" + number + ".insts.arithmetic";
    temp.desc =
        "CPU for ICL core " + number + " executed arithmetic instructions";
    list.push_back(temp);

    temp.name = prefix + ".icl" + number + ".insts.fp";
    temp.desc =
        "CPU for ICL core " + number + " executed floating point instructions";
    list.push_back(temp);

    temp.name = prefix + ".icl" + number + ".insts.others";
    temp.desc = "CPU for ICL core " + number + " executed other instructions";
    list.push_back(temp);
  }

  for (uint32_t i = 0; i < ftlCore.size(); i++) {
    number = std::to_string(i);

    temp.name = prefix + ".ftl" + number + ".busy";
    temp.desc = "CPU for FTL core " + number + " busy ticks";
    list.push_back(temp);

    temp.name = prefix + ".ftl" + number + ".insts.branch";
    temp.desc = "CPU for FTL core " + number + " executed branch instructions";
    list.push_back(temp);

    temp.name = prefix + ".ftl" + number + ".insts.load";
    temp.desc = "CPU for FTL core " + number + " executed store instructions";
    list.push_back(temp);

    temp.name = prefix + ".ftl" + number + ".insts.store";
    temp.desc = "CPU for FTL core " + number + " executed load instructions";
    list.push_back(temp);

    temp.name = prefix + ".ftl" + number + ".insts.arithmetic";
    temp.desc =
        "CPU for FTL core " + number + " executed arithmetic instructions";
    list.push_back(temp);

    temp.name = prefix + ".ftl" + number + ".insts.fp";
    temp.desc =
        "CPU for FTL core " + number + " executed floating point instructions";
    list.push_back(temp);

    temp.name = prefix + ".ftl" + number + ".insts.others";
    temp.desc = "CPU for FTL core " + number + " executed other instructions";
    list.push_back(temp);
  }
  // for (uint32_t i = 0; i < riscv_soc.rv_cores.size(); i++) {
  //   number = std::to_string(i);
    
  //   temp.name = prefix + ".riscv" + number + ".busy";
  //   temp.desc = "CPU for RISCV core " + number + " busy ticks";
  //   list.push_back(temp);

  //   temp.name = prefix + ".riscv" + number + ".insts.branch";
  //   temp.desc = "CPU for RISCV core " + number + " executed branch instructions";
  //   list.push_back(temp);

  //   temp.name = prefix + ".riscv" + number + ".insts.load";
  //   temp.desc = "CPU for RISCV core " + number + " executed load instructions";
  //   list.push_back(temp);

  //   temp.name = prefix + ".riscv" + number + ".insts.store";
  //   temp.desc = "CPU for RISCV core " + number + " executed store instructions";
  //   list.push_back(temp);

  //   temp.name = prefix + ".riscv" + number + ".insts.arithmetic";
  //   temp.desc =
  //       "CPU for RISCV core " + number + " executed arithmetic instructions";
  //   list.push_back(temp);

  //   temp.name = prefix + ".riscv" + number + ".insts.fp";
  //   temp.desc =
  //       "CPU for RISCV core " + number + " executed floating point instructions";
  //   list.push_back(temp);

  //   temp.name = prefix + ".riscv" + number + ".insts.others";
  //   temp.desc = "CPU for RISCV core " + number +
  //               " executed other instructions (e.g. system calls)";
  //   list.push_back(temp);
  // }

  temp.name = prefix + ".riscv" + ".icl_read_requests";
  temp.desc = "Total ICL read requests";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".icl_write_requests";
  temp.desc = "Total ICL write requests";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".icl_trim_requests";
  temp.desc = "Total ICL trim requests";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".icl_format_requests";
  temp.desc = "Total ICL format requests";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".icl_flush_requests";
  temp.desc = "Total ICL flush requests";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".read_cache_hits";
  temp.desc = "Total read cache hits";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".read_cache_misses";
  temp.desc = "Total read cache misses";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".write_cache_hits";
  temp.desc = "Total write cache hits";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".write_cache_misses";
  temp.desc = "Total write cache misses";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".read_cache_evictions";
  temp.desc = "Total read cache evictions";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".write_cache_evictions";
  temp.desc = "Total write cache evictions";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".icl_read_cycles";
  temp.desc = "Total ICL read cycles";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".icl_write_cycles";
  temp.desc = "Total ICL write cycles";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".icl_trim_cycles";
  temp.desc = "Total ICL trim cycles";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".icl_format_cycles";
  temp.desc = "Total ICL format cycles";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".icl_flush_cycles";
  temp.desc = "Total ICL flush cycles";
  list.push_back(temp);

  temp.name = prefix + ".riscv" + ".ftl_read_requests";
  temp.desc = "Total FTL read requests";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".ftl_write_requests";
  temp.desc = "Total FTL write requests";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".ftl_trim_requests";
  temp.desc = "Total FTL trim requests";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".ftl_format_requests";
  temp.desc = "Total FTL format requests";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".ftl_garbage_collection_requests";
  temp.desc = "Total FTL garbage collection requests";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".ftl_read_cycles";
  temp.desc = "Total FTL read cycles";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".ftl_write_cycles";
  temp.desc = "Total FTL write cycles";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".ftl_trim_cycles";
  temp.desc = "Total FTL trim cycles";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".ftl_format_cycles";
  temp.desc = "Total FTL format cycles";
  list.push_back(temp);
  temp.name = prefix + ".riscv" + ".ftl_garbage_collection_cycles";
  temp.desc = "Total FTL garbage collection cycles";
  list.push_back(temp);  


}

void CPU::getStatValues(std::vector<double> &values) {
  for (auto &core : hilCore) {
    auto &stat = core.getStat();

    values.push_back(stat.busy);
    values.push_back(stat.instStat.branch);
    values.push_back(stat.instStat.load);
    values.push_back(stat.instStat.store);
    values.push_back(stat.instStat.arithmetic);
    values.push_back(stat.instStat.floatingPoint);
    values.push_back(stat.instStat.otherInsts);
  }

  for (auto &core : iclCore) {
    auto &stat = core.getStat();

    values.push_back(stat.busy);
    values.push_back(stat.instStat.branch);
    values.push_back(stat.instStat.load);
    values.push_back(stat.instStat.store);
    values.push_back(stat.instStat.arithmetic);
    values.push_back(stat.instStat.floatingPoint);
    values.push_back(stat.instStat.otherInsts);
  }

  for (auto &core : ftlCore) {
    auto &stat = core.getStat();

    values.push_back(stat.busy);
    values.push_back(stat.instStat.branch);
    values.push_back(stat.instStat.load);
    values.push_back(stat.instStat.store);
    values.push_back(stat.instStat.arithmetic);
    values.push_back(stat.instStat.floatingPoint);
    values.push_back(stat.instStat.otherInsts);
  }

  auto *icl_stats = riscv_soc->getICLStats();
  values.push_back(icl_stats->read_requests);
  values.push_back(icl_stats->write_requests);
  values.push_back(icl_stats->trim_requests);
  values.push_back(icl_stats->format_requests);
  values.push_back(icl_stats->flush_requests);
  values.push_back(icl_stats->read_cache_hits);
  values.push_back(icl_stats->read_cache_misses);
  values.push_back(icl_stats->write_cache_hits);
  values.push_back(icl_stats->write_cache_misses);
  values.push_back(icl_stats->read_cache_evictions);
  values.push_back(icl_stats->write_cache_evictions);
  values.push_back(icl_stats->read_req_cycles);
  values.push_back(icl_stats->write_req_cycles);
  values.push_back(icl_stats->trim_req_cycles);
  values.push_back(icl_stats->format_req_cycles);
  values.push_back(icl_stats->flush_req_cycles);

  auto *ftl_stats = riscv_soc->getFTLStats();
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
}

void CPU::resetStatValues() {
  lastResetStat = getTick();

  for (auto &core : hilCore) {
    auto &stat = core.getStat();

    stat.busy = 0;
    stat.instStat = InstStat();
  }

  for (auto &core : iclCore) {
    auto &stat = core.getStat();

    stat.busy = 0;
    stat.instStat = InstStat();
  }

  for (auto &core : ftlCore) {
    auto &stat = core.getStat();

    stat.busy = 0;
    stat.instStat = InstStat();
  }
  riscv_soc->resetStatValues();
}

void CPU::printLastStat() {
  Power power;

  debugprint(LOG_CPU, "Begin CPU power calculation");

  calculatePower(power);

  debugprint(LOG_CPU, "Core:");
  debugprint(LOG_CPU, "  Area: %lf mm^2", power.core.area);
  debugprint(LOG_CPU, "  Peak Dynamic: %lf W", power.core.peakDynamic);
  debugprint(LOG_CPU, "  Subthreshold Leakage: %lf W",
             power.core.subthresholdLeakage);
  debugprint(LOG_CPU, "  Gate Leakage: %lf W", power.core.gateLeakage);
  debugprint(LOG_CPU, "  Runtime Dynamic: %lf W", power.core.runtimeDynamic);

  if (power.level2.area > 0.0) {
    debugprint(LOG_CPU, "L2:");
    debugprint(LOG_CPU, "  Area: %lf mm^2", power.level2.area);
    debugprint(LOG_CPU, "  Peak Dynamic: %lf W", power.level2.peakDynamic);
    debugprint(LOG_CPU, "  Subthreshold Leakage: %lf W",
               power.level2.subthresholdLeakage);
    debugprint(LOG_CPU, "  Gate Leakage: %lf W", power.level2.gateLeakage);
    debugprint(LOG_CPU, "  Runtime Dynamic: %lf W",
               power.level2.runtimeDynamic);
  }

  if (power.level3.area > 0.0) {
    debugprint(LOG_CPU, "L3:");
    debugprint(LOG_CPU, "  Area: %lf mm^2", power.level3.area);
    debugprint(LOG_CPU, "  Peak Dynamic: %lf W", power.level3.peakDynamic);
    debugprint(LOG_CPU, "  Subthreshold Leakage: %lf W",
               power.level3.subthresholdLeakage);
    debugprint(LOG_CPU, "  Gate Leakage: %lf W", power.level3.gateLeakage);
    debugprint(LOG_CPU, "  Runtime Dynamic: %lf W",
               power.level3.runtimeDynamic);
  }
}

uint64_t CPU::read_flash_icl(uint8_t* buffer, uint64_t offset , uint64_t len) {
  debugprint(LOG_CPU, "Read flash ICL from RISCV Core at offset %u, length %u",
             offset, len);
  ICL::Request req;
  LPNRange lpnRange;
  lpnRange.slpn = offset / (page_size / lba_size);
  lpnRange.nlp = (len / (page_size / lba_size)) + 1;
  req.range = lpnRange;
  req.offset = 0;
  req.length = len;
  req.reqID = global_req_id++;
  req.reqSubID = 0;
  uint64_t reqTick = getTick();
  debugprint(LOG_CPU, "Request ID %u at tick %llu", req.reqID, reqTick);
  pICL->read(req, reqTick);
  debugprint(LOG_CPU, "Request ID %u finished at tick %llu",
             req.reqID, reqTick);
  uint64_t slba = offset;
  uint32_t nlblk = len;
  pDisk->read(slba, nlblk, buffer);
  return reqTick;
}

uint64_t CPU::write_flash_icl(uint8_t* buffer, uint64_t offset , uint64_t len) {
  debugprint(SimpleSSD::LOG_CPU, "Write flash ICL from RISCV Core at offset %u, length %u",
      offset, len);
  ICL::Request req;
  LPNRange lpnRange;
  uint32_t page_per_lba = page_size / lba_size;
  lpnRange.slpn = offset / page_per_lba;
  lpnRange.nlp = (len + page_per_lba - 1) / page_per_lba;
  req.range = lpnRange;
  req.offset = 0;
  req.length = len;
  req.reqID = global_req_id++;
  req.reqSubID = 0;
  uint64_t reqTick = getTick();
  debugprint(LOG_CPU, "Request ID %u at tick %llu", req.reqID, reqTick);
  pICL->write(req, reqTick);
  debugprint(LOG_CPU, "Request ID %u completed at tick %llu",
              req.reqID, reqTick);
  uint64_t slba = offset;
  uint32_t nlblk = len;
  pDisk->write(slba, nlblk, buffer);
  return reqTick;
}

uint64_t CPU::trim_flash_icl(uint8_t* buffer, uint64_t offset , uint64_t len) {
  debugprint(SimpleSSD::LOG_CPU, "Trim flash ICL from RISCV Core at offset %u, length %u",
    offset, len);
  LPNRange lpnRange;
  uint32_t page_per_lba = page_size / lba_size;
  lpnRange.slpn = offset / page_per_lba;
  lpnRange.nlp = (len + page_per_lba - 1) / page_per_lba;
  uint64_t reqTick = getTick();
  pICL->trim(lpnRange, reqTick);
  return reqTick;
}

// TO-DO: implement!
void CPU::read_flash_pal(uint8_t* request, uint64_t* tick) {
  debugprint(LOG_CPU, "Read flash PAL from RISCV Core at tick %llu", *tick);
  PAL::Request* req = (PAL::Request*)request;
  uint64_t reqTick = *tick;
  pPAL->read(*req, reqTick);
  *tick = reqTick;
}

void CPU::write_flash_pal(uint8_t* request, uint64_t* tick) {
  debugprint(LOG_CPU, "Write flash PAL from RISCV Core at tick %llu", *tick);
  PAL::Request* req = (PAL::Request*)request;
  uint64_t reqTick = *tick;
  pPAL->write(*req, reqTick);
  *tick = reqTick;
}

// TO-DO: implement!
void CPU::erase_flash_pal(uint8_t* request, uint64_t* tick) {
  debugprint(LOG_CPU, "Erase flash PAL from RISCV Core at tick %llu", *tick);
  PAL::Request* req = (PAL::Request*)request;
  uint64_t reqTick = *tick;
  pPAL->erase(*req, reqTick);
  *tick = reqTick;
}


void CPU::setDisk(Disk *disk) {
  pDisk = disk;
}

void CPU::closeDisk() {
  if (pDisk) {
    pDisk->close();
    pDisk = nullptr;
  }
}

void CPU::addRISCVTask(char* input_command) {
  riscv_soc->rv_soc_add_task(input_command);
}

void CPU::read_buffer(uint8_t* buffer, uint64_t req_info, uint64_t req_type) {
  if (req_type == RISCV::FIRMWARE_FTL_PARAMS) {
    memcpy(buffer, &fparams, sizeof(ftl_params));
  } else if (req_type == RISCV::FIRMWARE_ICL_PARAMS) {
    memcpy(buffer, &iparams, sizeof(icl_params));
  } else if (req_type == RISCV::FIRMWARE_QUEUE_TOP) {
    RISCVJob req_job = req_queue.front();
    memcpy(buffer, &req_job.request, sizeof(ICL::Request));
    uint64_t core_id = req_info;
    if (core_id < callbacks.size()) {
      callbacks[core_id].first = req_job.func;
      callbacks[core_id].second = req_job.context;
    } else {
      debugprint(LOG_CPU, "Core ID %llu is out of range for callbacks",
                 core_id);
    }
    req_queue.pop();
  } else if (req_type == RISCV::FIRMWARE_TICK) {
    uint64_t tick = getTick();
    memcpy(buffer, &tick, sizeof(uint64_t));
  } else if (req_type == RISCV::FIRMWARE_QUEUE_SIZE) {
    uint64_t size = req_queue.size();
    memcpy(buffer, &size, sizeof(uint64_t));
  } else {
    debugprint(LOG_CPU, "Unknown request type %llu", req_type);
  }
}

void CPU::write_buffer(uint8_t* buffer, uint64_t req_info, uint64_t req_type) {
  if (req_type == RISCV::FIRMWARE_REQ_DONE) {
    uint64_t finished_at;
    memcpy(&finished_at, buffer, sizeof(uint64_t));
    runDMA(finished_at, req_info);
  } else if (req_type == RISCV::FIRMWARE_REQ_FAILED) {
    debugprint(LOG_CPU, "Request failed!!\n");
    // runDMA(req_info);
  }
}

void CPU::runDMA(uint64_t finished_at, uint64_t core_id) {
  // for now there isn't a core id
  DMAFunction func = callbacks[core_id].first;
  void* context = callbacks[core_id].second;
  debugprint(LOG_CPU, "Running DMA function at tick %lu for core ID %lu",
             finished_at, core_id);
  func(finished_at, context);
}

uint64_t CPU::getClockPeriod() {
  return clockPeriod;
}

bool CPU::socIsPaused() {
  return (RISCV::soc_run_mode_ == RISCV::PAUSED_MODE);
}

uint64_t CPU::submitRead(HIL::Request *req, DMAFunction &callback) {
  debugprint(LOG_CPU, "Submitting read request with ID %u", req->reqID);
  ICL::Request request(*req);
  request.reqType = ICL_REQ_READ;
  req_queue.push({request, callback, (void*)req});
  if (socIsPaused()) {
    startRISCV();
  }
  return getTick() + clockPeriod;
}

uint64_t CPU::submitWrite(HIL::Request *req, DMAFunction &callback) {
  debugprint(LOG_CPU, "Submitting write request with ID %u", req->reqID);
  ICL::Request request(*req);
  request.reqType = ICL_REQ_WRITE;
  req_queue.push({request, callback, (void*)req});
  if (socIsPaused()) {
    startRISCV();
  }
  return getTick() + clockPeriod;
}

uint64_t CPU::submitTrim(HIL::Request *req, DMAFunction &callback) {
  debugprint(LOG_CPU, "Submitting trim request with ID %u", req->reqID);
  ICL::Request request(*req);
  request.reqType = ICL_REQ_TRIM;
  req_queue.push({request, callback, (void*)req});
  if (socIsPaused()) {
    startRISCV();
  }
  return getTick() + clockPeriod;
}  

uint64_t CPU::submitFormat(HIL::Request *req, DMAFunction &callback) {
  debugprint(LOG_CPU, "Submitting format request with ID %u", req->reqID);
  ICL::Request request(*req);
  request.reqType = ICL_REQ_FORMAT;
  req_queue.push({request, callback, (void*)req});
  if (socIsPaused()) {
    startRISCV();
  }
  return getTick() + clockPeriod;
}

uint64_t CPU::submitFlush(HIL::Request *req, DMAFunction &callback) {
  debugprint(LOG_CPU, "Submitting flush request with ID %u", req->reqID);
  ICL::Request request(*req);
  request.reqType = ICL_REQ_FLUSH;
  req_queue.push({request, callback, (void*)req});
  if (socIsPaused()) {
    startRISCV();
  }
  return getTick() + clockPeriod;

}

} // namespace CPU

} // namespace SimpleSSD
