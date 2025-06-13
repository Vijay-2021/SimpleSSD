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

#ifndef __RISCV_FTL_FW__
#define __RISCV_FTL_FW__

#include "map.hh"
#include "vector.hh"
#include "list.hh"
#include "pair.hh"
#include "cs_instructions.h"
#include "Block.hh"
#include "Bitset.hh"
#include "Stats.hh"
#include "Config.hh"
#include "AbstractDram.hh"

class Firmware {

private:

  map<uint64_t, vector<pair<uint32_t, uint32_t>>>
      table;
  map<uint32_t, Block> blocks;
  std::list<Block> freeBlocks;
  uint32_t nFreeBlocks;  // For some libraries which std::list::size() is O(n)
  std::vector<uint32_t> lastFreeBlock;
  Bitset lastFreeBlockIOMap;
  uint32_t lastFreeBlockIndex;

  bool bReclaimMore;
  bool bRandomTweak;
  uint32_t bitsetSize;

  struct {
    uint64_t gcCount;
    uint64_t reclaimedBlocks;
    uint64_t validSuperPageCopies;
    uint64_t validPageCopies;
  } stat;

  typedef struct {
    uint64_t totalPhysicalBlocks;  //!< (PAL::Parameter::superBlock)
    uint64_t totalLogicalBlocks;
    uint64_t pagesInBlock;  //!< (PAL::Parameter::page)
    uint32_t pageSize;      //!< Mapping unit (PAL::Parameter::superPageSize)
    uint32_t ioUnitInPage;  //!< # smallest I/O unit in one page
    uint32_t pageCountToMaxPerf;  //!< # pages to fully utilize internal parallism
    bool random_tweak;
  } Parameter;

  Parameter ssd_params;

  float freeBlockRatio();
  uint32_t convertBlockIdx(uint32_t);
  uint32_t getFreeBlock(uint32_t);
  uint32_t getLastFreeBlock(Bitset &);
  void calculateVictimWeight(vector<pair<uint32_t, float>> &,
                            const EVICT_POLICY, uint64_t);
  void selectVictimBlock(vector<uint32_t> &, uint64_t &);
  void doGarbageCollection(vector<uint32_t> &, uint64_t &);

  float calculateWearLeveling();
  void calculateTotalPages(uint64_t &, uint64_t &);

  void readInternal(Request &, uint64_t &);
  void writeInternal(Request &, uint64_t &, bool = true);
  void trimInternal(Request &, uint64_t &);
  void eraseInternal(PAL::Request &, uint64_t &);

public:
  Firmware(ConfigReader &, Parameter &, PAL::PAL *, DRAM::AbstractDRAM *);
  ~Firmware();

  bool initialize() override;

  void read(Request &, uint64_t &) override;
  void write(Request &, uint64_t &) override;
  void trim(Request &, uint64_t &) override;

  void format(LPNRange &, uint64_t &) override;

  Status *getStatus(uint64_t, uint64_t) override;

  void getStatList(std::vector<Stats> &, char*) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;
};


#endif