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
    typedef enum {
      /* Common FTL configuration */
      FTL_MAPPING_MODE          = 0,
      FTL_OVERPROVISION_RATIO   = 1,
      FTL_GC_THRESHOLD_RATIO    = 2,
      FTL_BAD_BLOCK_THRESHOLD   = 3,
      FTL_FILLING_MODE          = 4,
      FTL_FILL_RATIO            = 5,
      FTL_INVALID_PAGE_RATIO    = 6,
      FTL_GC_MODE               = 7,
      FTL_GC_RECLAIM_BLOCK      = 8,
      FTL_GC_RECLAIM_THRESHOLD  = 9,
      FTL_GC_EVICT_POLICY       = 10,
      FTL_GC_D_CHOICE_PARAM     = 11,
      FTL_USE_RANDOM_IO_TWEAK   = 12,

      /* N+K Mapping configuration*/
      FTL_NKMAP_N               = 13,
      FTL_NKMAP_K               = 14,
    } FTL_CONFIG;

    typedef enum {
      PAGE_MAPPING = 0,
    } MAPPING;

    typedef enum {
      GC_MODE_0 = 0,  // Reclaim fixed number of blocks
      GC_MODE_1 = 1,  // Reclaim blocks until threshold
    } GC_MODE;

    typedef enum {
      FILLING_MODE_0 = 0,
      FILLING_MODE_1 = 1,
      FILLING_MODE_2 = 2,
    } FILLING_MODE;

    typedef enum {
      POLICY_GREEDY        = 0,  // Select the block with the least valid pages
      POLICY_COST_BENEFIT  = 1,
      POLICY_RANDOM        = 2,  // Select the block randomly
      POLICY_DCHOICE       = 3,
    } EVICT_POLICY;

    typedef enum {
        FIRMWARE_REQ = 0
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
      FILLING_MODE ftl_filling_mode; // or FILLING_MODE
      float ftl_gc_threshold_ratio;
      GC_MODE ftl_gc_mode;
      EVICT_POLICY ftl_evict_policy;
      uint32_t choiceParam;
      uint64_t ftl_gc_reclaim_block;
      float ftl_gc_reclaim_threshold;
      uint64_t bad_block_threshold;
    } firmware_params_td;

  
  
  public:
    Firmware();
    ~Firmware();
    void print_status();
    uint64_t read();
    uint64_t write();
    uint64_t trim();
    
  private:
    firmware_params_td ssd_params;
    Vector<int> lastFreeBlock;
    List<Pair<int, int>> test;
};


#endif