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

#include "cs_instructions.h"
#include "vector.hh"
#include "list.hh"
#include "pair.hh"


class Firmware {
  
  private:
    Map<uint64_t, Vector<std::pair<uint32_t, uint32_t>>>
        table;
    Map<uint32_t, Block> blocks;
    List<Block> freeBlocks;
    uint32_t nFreeBlocks;  // For some libraries which List::size() is O(n)
    Vector<uint32_t> lastFreeBlock;
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
    
    firmware_params param;
    float freeBlockRatio();
    uint32_t convertBlockIdx(uint32_t);
    uint32_t getFreeBlock(uint32_t);
    uint32_t getLastFreeBlock(Bitset &);
    void calculateVictimWeight(Vector<std::pair<uint32_t, float>> &,
                              const EVICT_POLICY, uint64_t);
    void selectVictimBlock(Vector<uint32_t> &, uint64_t &);
    void doGarbageCollection(Vector<uint32_t> &, uint64_t &);

    float calculateWearLeveling();
    void calculateTotalPages(uint64_t &, uint64_t &);

    void readInternal(Request &, uint64_t &);
    void writeInternal(Request &, uint64_t &, bool = true);
    void trimInternal(Request &, uint64_t &);
    void eraseInternal(PAL::Request &, uint64_t &);

 public:
    Firmware(firmware_params& ssd_params);
    ~Firmware();

    bool initialize() override;

    void read(Request &, uint64_t &) override;
    void write(Request &, uint64_t &) override;
    void trim(Request &, uint64_t &) override;
};


#endif