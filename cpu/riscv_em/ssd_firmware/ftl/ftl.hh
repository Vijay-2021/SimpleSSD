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
#include "map.hh"
#include "pair.hh"
#include "def.hh"
#include "block.hh"
#include "limits.hh"

namespace FTL {

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
  FILLING_MODE ftl_filling_mode; // or FILLING_MODE
  float ftl_gc_threshold_ratio;
  GC_MODE ftl_gc_mode;
  EVICT_POLICY ftl_evict_policy;
  uint32_t choiceParam;
  uint64_t ftl_gc_reclaim_block;
  float ftl_gc_reclaim_threshold;
  uint64_t bad_block_threshold;
};

class FTL {
  
  private:
    Vector<Vector<Pair<uint32_t, uint32_t>>>
        table; // maps lba to physical block/page(for each io unit in page)
    Vector<Block> blocks;
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

    
    ftl_params params;
    float freeBlockRatio();
    uint32_t convertBlockIdx(uint32_t);
    uint32_t getFreeBlock(uint32_t);
    uint32_t getLastFreeBlock(Bitset &);
    void calculateVictimWeight(Vector<Pair<uint32_t, float>> &,
                              const EVICT_POLICY);
    void selectVictimBlock(Vector<uint32_t> &);
    uint64_t doGarbageCollection(Vector<uint32_t> &);

    float calculateWearLeveling();
    void calculateTotalPages(uint64_t &, uint64_t &);

    uint64_t readInternal(Request &);
    uint64_t writeInternal(Request &, bool = true);
    uint64_t trimInternal(Request &);
    void eraseInternal(PAL::Request &, uint64_t &tick);

 public:
    FTL(ftl_params& fparams);
    ~FTL();

    bool initialize();

    uint64_t read(Request &);
    uint64_t write(Request &);
    uint64_t trim(Request &);
    uint64_t format(LPNRange &);
    
    FTLStats ftl_stats;
};

}

#endif