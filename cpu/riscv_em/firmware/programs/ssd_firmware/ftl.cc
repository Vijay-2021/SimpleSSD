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

#include "ftl.hh"
#include "utils.h"
#include "firmware_utils.h"
#include "move.hh"
#include "random.h"
#include "sort.hh"
#include "memory.h"
#include "unique.hh"

namespace FTL {

FTL::FTL(ftl_params &fparams) : params(fparams), lastFreeBlock(fparams.pageCountToMaxPerf),
      lastFreeBlockIOMap(fparams.ioUnitInPage), bReclaimMore(false), blocks(fparams.totalPhysicalBlocks), 
      table(fparams.totalLogicalBlocks * fparams.pagesInBlock) {
  printf("constructor called with total physical blocks: %u\n", params.totalPhysicalBlocks);
  printf("total page count to max perf: %u\n", params.pageCountToMaxPerf);
  for (uint32_t i = 0; i < 16; i++) {
    freeBlocks.emplace_back(move(Block(i, params.pagesInBlock, params.ioUnitInPage)));
  }

  nFreeBlocks = params.totalPhysicalBlocks;
  printf("free blocks is fine\n");
  // status.totalLogicalPages = params.totalLogicalBlocks * params.pagesInBlock;

  // Allocate free blocks
  for (uint32_t i = 0; i < params.pageCountToMaxPerf; i++) {
    lastFreeBlock.at(i) = getFreeBlock(i);
  }
  printf("Completed map allocate\n");
  lastFreeBlockIndex = 0;

  memset(&stat, 0, sizeof(stat));

  bRandomTweak = params.bRandomTweak;
  bitsetSize = bRandomTweak ? params.ioUnitInPage : 1;
  printf("finished setup, starting initialization\n");
  initialize();
}

FTL::~FTL() {}
 
bool FTL::initialize() {
  uint64_t nPagesToWarmup;
  uint64_t nPagesToInvalidate;
  uint64_t nTotalLogicalPages;
  uint64_t maxPagesBeforeGC;
  uint64_t tick;
  uint64_t valid;
  uint64_t invalid;
  FILLING_MODE mode;

  Request req(params.ioUnitInPage);

  print("Initialization started\n");

  nTotalLogicalPages = params.totalLogicalBlocks * params.pagesInBlock;
  nPagesToWarmup =
      nTotalLogicalPages * params.ftl_fill_ratio;
  nPagesToInvalidate =
      nTotalLogicalPages * params.ftl_invalid_page_ratio;
  mode = params.ftl_filling_mode;
  maxPagesBeforeGC =
      params.pagesInBlock *
      (params.totalPhysicalBlocks *
           (1 - params.ftl_gc_threshold_ratio) -
       params.pageCountToMaxPerf);  // # free blocks to maintain
  if (nPagesToWarmup + nPagesToInvalidate > maxPagesBeforeGC) {
    nPagesToInvalidate = maxPagesBeforeGC - nPagesToWarmup;
  }
  req.ioFlag.set();
  // Step 1. Filling
  if (mode == FILLING_MODE_0 || mode == FILLING_MODE_1) {
    // Sequential
    for (uint64_t i = 0; i < nPagesToWarmup; i++) {
      req.lpn = i;
      writeInternal(req, false);
    }
  }
  else {
    // Random
    for (uint64_t i = 0; i < nPagesToWarmup; i++) {
      tick = 0;
      req.lpn = rand64_range(0, nTotalLogicalPages - 1);
      writeInternal(req, false);
    }
  }

  // Step 2. Invalidating
  if (mode == FILLING_MODE_0) {
    // Sequential
    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      printf("okay...\n");
      req.lpn = i;
      writeInternal(req, false);
    }
  }
  else if (mode == FILLING_MODE_1) {
    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      req.lpn = rand64_range(0, nPagesToWarmup - 1);
      writeInternal(req, false);
    }
  }
  else {
    // Random
    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      req.lpn = rand64_range(0, nTotalLogicalPages - 1);
      writeInternal(req, false);
    }
  }
  
  calculateTotalPages(valid, invalid);

  return true;
}

uint64_t FTL::read(Request &req) {
  if (req.ioFlag.count() > 0) {
    return readInternal(req);
  }
  else {
    print("FTL got empty request\n");
    return getTick();
  }

}

uint64_t FTL::write(Request &req) {
  if (req.ioFlag.count() > 0) {
    return writeInternal(req);
  }
  else {
    print("FTL got empty request\n");
    return getTick();
  }

}

uint64_t FTL::trim(Request &req) {
  return trimInternal(req);
}


uint64_t FTL::format(LPNRange &range) {
  PAL::Request req(params.ioUnitInPage);
  Vector<uint32_t> list;

  req.ioFlag.set();

  for (auto iter = table.begin(); iter != table.end();) {
    if (iter->first >= range.slpn && iter->first < range.slpn + range.nlp) {
      auto &mappingList = iter->second;

      // Do trim
      for (uint32_t idx = 0; idx < bitsetSize; idx++) {
        auto &mapping = mappingList.at(idx);
        auto block = blocks.find(mapping.first);

        if (block == blocks.end()) {
          panic("Block is not in use");
        }

        block->second.invalidate(mapping.second, idx);

        // Collect block indices
        list.push_back(mapping.first);
      }

      iter = table.erase(iter);
    }
    else {
      ++iter;
    }
  }

  // Get blocks to erase
  introsort(list.begin(), list.end(), [](const uint32_t &a, const uint32_t &b) {
    return a < b;
  });
  auto last = Unique(list.begin(), list.end());
  while (last != list.end()) {
    list.pop_back();
  }

  // Do GC only in specified blocks
  return doGarbageCollection(list);

} 

float FTL::freeBlockRatio() {
  return (float)nFreeBlocks / params.totalPhysicalBlocks;
}

uint32_t FTL::convertBlockIdx(uint32_t blockIdx) {
  return blockIdx % params.pageCountToMaxPerf;
}

uint32_t FTL::getFreeBlock(uint32_t idx) {
  uint32_t blockIndex = 0;

  if (idx >= params.pageCountToMaxPerf) {
    panic("Index out of range");
  }

  if (nFreeBlocks > 0) {
    // Search block which is blockIdx % params.pageCountToMaxPerf == idx
    auto iter = freeBlocks.begin();

    for (; iter != freeBlocks.end(); ++iter) {
      blockIndex = iter->getBlockIndex();

      if (blockIndex % params.pageCountToMaxPerf == idx) {
        break;
      }
    }

    // Sanity check
    if (iter == freeBlocks.end()) {
      // Just use first one
      iter = freeBlocks.begin();
      blockIndex = iter->getBlockIndex();
    }
    // Insert found block to block list
    if (blocks.find(blockIndex) != blocks.end()) {
      panic("Corrupted");
    }
    blocks.emplace(blockIndex, move(*iter));
    // Remove found block from free block list
    freeBlocks.erase(iter);
    nFreeBlocks--;
  }
  else {
    panic("No free block left");
  }

  return blockIndex;
}

uint32_t FTL::getLastFreeBlock(Bitset &iomap) {
  if (!bRandomTweak || (lastFreeBlockIOMap & iomap).any()) {
    // Update lastFreeBlockIndex
    lastFreeBlockIndex++;

    if (lastFreeBlockIndex == params.pageCountToMaxPerf) {
      lastFreeBlockIndex = 0;
    }

    lastFreeBlockIOMap = iomap;
  }
  else {
    lastFreeBlockIOMap |= iomap;
  }

  auto freeBlock = blocks.find(lastFreeBlock.at(lastFreeBlockIndex));

  // Sanity check
  if (freeBlock == blocks.end()) {
    panic("Corrupted");
  }

  // If current free block is full, get next block
  if (freeBlock->second.getNextWritePageIndex() == params.pagesInBlock) {
    lastFreeBlock.at(lastFreeBlockIndex) = getFreeBlock(lastFreeBlockIndex);

    bReclaimMore = true;
  }

  return lastFreeBlock.at(lastFreeBlockIndex);
}

// calculate weight of each block regarding victim selection policy
void FTL::calculateVictimWeight(
    Vector<Pair<uint32_t, float>> &weight, const EVICT_POLICY policy) {
  float temp;

  weight.reserve(blocks.size());

  switch (policy) {
    case POLICY_GREEDY:
    case POLICY_RANDOM:
    case POLICY_DCHOICE:
      for (auto iter : blocks) {
        if (iter.second.getNextWritePageIndex() != params.pagesInBlock) {
          continue;
        }

        weight.push_back({iter.first, iter.second.getValidPageCountRaw()});
      }

      break;
    case POLICY_COST_BENEFIT:
      for (auto iter : blocks) {
        if (iter.second.getNextWritePageIndex() != params.pagesInBlock) {
          continue;
        }

        temp = (float)(iter.second.getValidPageCountRaw()) / params.pagesInBlock;

        weight.push_back(
            {iter.first,
             temp / ((1 - temp) * (getTick() - iter.second.getLastAccessedTime()))});
      }

      break;
    default:
      panic("Invalid evict policy");
  }
}

void FTL::selectVictimBlock(Vector<uint32_t> &list) {
  static const GC_MODE mode = params.ftl_gc_mode;
  static const EVICT_POLICY policy = params.ftl_evict_policy;
  static uint32_t dChoiceParam = params.choiceParam;
  uint64_t nBlocks = params.ftl_gc_reclaim_block;
  Vector<Pair<uint32_t, float>> weight;

  list.clear();

  // Calculate number of blocks to reclaim
  if (mode == GC_MODE_0) {
    // DO NOTHING
  }
  else if (mode == GC_MODE_1) {
    static const float t = params.ftl_gc_reclaim_threshold;

    nBlocks = params.totalPhysicalBlocks * t - nFreeBlocks;
  }
  else {
    panic("Invalid GC mode");
  }

  // reclaim one more if last free block fully used
  if (bReclaimMore) {
    nBlocks += params.pageCountToMaxPerf;

    bReclaimMore = false;
  }

  // Calculate weights of all blocks
  calculateVictimWeight(weight, policy);

  if (policy == POLICY_RANDOM || policy == POLICY_DCHOICE) {
    uint64_t randomRange =
        policy == POLICY_RANDOM ? nBlocks : dChoiceParam * nBlocks;
    Vector<Pair<uint32_t, float>> selected;

    while (selected.size() < randomRange) {
      uint64_t idx = rand64_range(0, weight.size() - 1);

      if (weight.at(idx).first < 0xFFFFFFFF) {
        selected.push_back(weight.at(idx));
        weight.at(idx).first = 0xFFFFFFFF;  // Mark as selected
      }
    }

    weight = move(selected);
  }

  // Sort weights
  introsort(
      weight.begin(), weight.end(),
      [](Pair<uint32_t, float> a, Pair<uint32_t, float> b) -> bool {
        return a.second < b.second;
      });

  // Select victims from the blocks with the lowest weight
  nBlocks = MIN(nBlocks, weight.size());

  for (uint64_t i = 0; i < nBlocks; i++) {
    list.push_back(weight.at(i).first);
  }

}

uint64_t FTL::doGarbageCollection(Vector<uint32_t> &blocksToReclaim) {
  PAL::Request req(params.ioUnitInPage);
  Vector<PAL::Request> readRequests;
  Vector<PAL::Request> writeRequests;
  Vector<PAL::Request> eraseRequests;
  Vector<uint64_t> lpns;
  Bitset bit(params.ioUnitInPage);

  if (blocksToReclaim.size() == 0) {
    return getTick();
  }

  // For all blocks to reclaim, collecting request structure only
  for (auto &iter : blocksToReclaim) {
    auto block = blocks.find(iter);

    if (block == blocks.end()) {
      panic("Invalid block");
    }

    // Copy valid pages to free block
    for (uint32_t pageIndex = 0; pageIndex < params.pagesInBlock; pageIndex++) {
      // Valid?
      if (block->second.getPageInfo(pageIndex, lpns, bit)) {
        if (!bRandomTweak) {
          bit.set();
        }

        // Retrive free block
        auto freeBlock = blocks.find(getLastFreeBlock(bit));

        // Issue Read
        req.blockIndex = block->first;
        req.pageIndex = pageIndex;
        req.ioFlag = bit;

        readRequests.push_back(req);

        // Update mapping table
        uint32_t newBlockIdx = freeBlock->first;

        for (uint32_t idx = 0; idx < bitsetSize; idx++) {
          if (bit.test(idx)) {
            // Invalidate
            block->second.invalidate(pageIndex, idx);

            auto mappingList = table.find(lpns.at(idx));

            if (mappingList == table.end()) {
              panic("Invalid mapping table entry");
            }

            auto &mapping = mappingList->second.at(idx);

            uint32_t newPageIdx = freeBlock->second.getNextWritePageIndex(idx);

            mapping.first = newBlockIdx;
            mapping.second = newPageIdx;

            freeBlock->second.write(newPageIdx, lpns.at(idx), idx);

            // Issue Write
            req.blockIndex = newBlockIdx;
            req.pageIndex = newPageIdx;

            if (bRandomTweak) {
              req.ioFlag.reset();
              req.ioFlag.set(idx);
            }
            else {
              req.ioFlag.set();
            }

            writeRequests.push_back(req);

            stat.validPageCopies++;
          }
        }

        stat.validSuperPageCopies++;
      }
    }

    // Erase block
    req.blockIndex = block->first;
    req.pageIndex = 0;
    req.ioFlag.set();

    eraseRequests.push_back(req);
  }

  // Do actual I/O here
  // This handles PAL2 limitation (SIGSEGV, infinite loop, or so-on)
  uint64_t beginAt;
  uint64_t readFinishedAt = getTick();
  uint64_t writeFinishedAt = readFinishedAt;
  uint64_t eraseFinishedAt = readFinishedAt;

  for (auto &iter : readRequests) {
    beginAt = getTick();
    pread((uint64_t) &iter, (uint64_t)&beginAt, 0);
    //pPAL->read(iter, beginAt);

    readFinishedAt = MAX(readFinishedAt, beginAt);
  }

  for (auto &iter : writeRequests) {
    beginAt = readFinishedAt;
    pwrite((uint64_t) &iter, (uint64_t)&beginAt, 0); // will implement it in the backend so that ticks are handled
    // pPAL->write(iter, beginAt);

    writeFinishedAt = MAX(writeFinishedAt, beginAt);
  }

  for (auto &iter : eraseRequests) {
    beginAt = readFinishedAt;

    eraseInternal(iter, beginAt);

    eraseFinishedAt = MAX(eraseFinishedAt, beginAt);
  }

  uint64_t tick = MAX(writeFinishedAt, eraseFinishedAt);
  return tick;
}

uint64_t FTL::readInternal(Request &req) {
  PAL::Request palRequest(req);
  uint64_t beginAt;
  uint64_t finishedAt = getTick();
  auto mappingList = table.find(req.lpn);
  if (mappingList != table.end()) {

    for (uint32_t idx = 0; idx < bitsetSize; idx++) {
      if (req.ioFlag.test(idx) || !bRandomTweak) {
        auto &mapping = mappingList->second.at(idx);

        if (mapping.first < params.totalPhysicalBlocks &&
            mapping.second < params.pagesInBlock) {
          palRequest.blockIndex = mapping.first;
          palRequest.pageIndex = mapping.second;

          if (bRandomTweak) {
            palRequest.ioFlag.reset();
            palRequest.ioFlag.set(idx);
          }
          else {
            palRequest.ioFlag.set();
          }

          auto block = blocks.find(palRequest.blockIndex);

          if (block == blocks.end()) {
            panic("Block is not in use");
          }

          beginAt = getTick();

          block->second.read(palRequest.pageIndex, idx);
          pread((uint64_t) &palRequest, (uint64_t)&beginAt, 0);
          // pPAL->read(palRequest, beginAt);

          finishedAt = MAX(finishedAt, beginAt);
        }
      }
    }

    return finishedAt;
  }
  return getTick(); // No mapping found, return current tick
}

uint64_t FTL::writeInternal(Request &req, bool sendToPAL) {
  printf("calling write interal!\n");
  PAL::Request palRequest(req);
  HashMap<uint32_t, Block>::iterator block;
  auto mappingList = table.find(req.lpn);
  uint64_t beginAt;
  uint64_t finishedAt = getTick();
  bool readBeforeWrite = false;

  if (mappingList != table.end()) {
    for (uint32_t idx = 0; idx < bitsetSize; idx++) {
      if (req.ioFlag.test(idx) || !bRandomTweak) {
        auto &mapping = mappingList->second.at(idx);

        if (mapping.first < params.totalPhysicalBlocks &&
            mapping.second < params.pagesInBlock) {
          block = blocks.find(mapping.first);

          // Invalidate current page
          block->second.invalidate(mapping.second, idx);
        }
      }
    }
  }
  else {
    // Create empty mapping
    auto ret = table.emplace(
        req.lpn,
        Vector<Pair<uint32_t, uint32_t>>(
            bitsetSize, {params.totalPhysicalBlocks, params.pagesInBlock}));

    if (!ret.second) {
      panic("Failed to insert new mapping");
    }

    mappingList = ret.first;
  }

  // Write data to free block
  block = blocks.find(getLastFreeBlock(req.ioFlag));

  if (block == blocks.end()) {
    panic("No such block");
  }

  if (!bRandomTweak && !req.ioFlag.all()) {
    // We have to read old data
    readBeforeWrite = true;
  }

  for (uint32_t idx = 0; idx < bitsetSize; idx++) {
    if (req.ioFlag.test(idx) || !bRandomTweak) {
      uint32_t pageIndex = block->second.getNextWritePageIndex(idx);
      auto &mapping = mappingList->second.at(idx);

      beginAt = getTick();

      block->second.write(pageIndex, req.lpn, idx);

      // Read old data if needed (Only executed when bRandomTweak = false)
      // Maybe some other init procedures want to perform 'partial-write'
      // So check sendToPAL variable
      if (readBeforeWrite && sendToPAL) {
        palRequest.blockIndex = mapping.first;
        palRequest.pageIndex = mapping.second;

        // We don't need to read old data
        palRequest.ioFlag = req.ioFlag;
        palRequest.ioFlag.flip();
        beginAt = getTick();
        pread((uint64_t) &palRequest, (uint64_t)&beginAt, 0);
        //pPAL->read(palRequest, beginAt);
      }

      // update mapping to table
      mapping.first = block->first;
      mapping.second = pageIndex;

      if (sendToPAL) {
        palRequest.blockIndex = block->first;
        palRequest.pageIndex = pageIndex;

        if (bRandomTweak) {
          palRequest.ioFlag.reset();
          palRequest.ioFlag.set(idx);
        }
        else {
          palRequest.ioFlag.set();
        }
        pwrite((uint64_t) &palRequest, (uint64_t)&beginAt, 0);
        //pPAL->write(palRequest, beginAt);
      }

      finishedAt = MAX(finishedAt, beginAt);
    }
  }

  // Exclude CPU operation when initializing
  // if (sendToPAL) {
  //   tick = finishedAt;
  // }

  // GC if needed
  // I assumed that init procedure never invokes GC
  static float gcThreshold = params.ftl_gc_threshold_ratio;;

  if (freeBlockRatio() < gcThreshold) {
    if (!sendToPAL) {
      panic("ftl: GC triggered while in initialization");
    }

    Vector<uint32_t> list;
    uint64_t beginAt = getTick();

    selectVictimBlock(list);

    printf("GC   | On-demand | %u blocks will be reclaimed", list.size());

    uint64_t ret_time = doGarbageCollection(list);

    printf(" GC Done | %u - %u (%u)", ret_time, beginAt, ret_time - beginAt);

    stat.gcCount++;
    stat.reclaimedBlocks += list.size();
  }
  return finishedAt; // ignore gc time in PAL(the cpu time will still be considered as well as the time for writing to PAL for specific request and extra GC requests will affect PAL performance of subsequent requests)
    
}

uint64_t FTL::trimInternal(Request &req) {
  auto mappingList = table.find(req.lpn);

  if (mappingList != table.end()) {

    // Do trim
    for (uint32_t idx = 0; idx < bitsetSize; idx++) {
      auto &mapping = mappingList->second.at(idx);
      auto block = blocks.find(mapping.first);

      if (block == blocks.end()) {
        panic("Block is not in use");
      }

      block->second.invalidate(mapping.second, idx);
    }

    // Remove mapping
    table.erase(mappingList);
  }
  return getTick();
}

void FTL::eraseInternal(PAL::Request &req, uint64_t &tick) {
  uint64_t beginAt = getTick();
  static uint64_t threshold = params.bad_block_threshold;
  auto block = blocks.find(req.blockIndex);
  uint64_t pal_time;
  // Sanity checks
  if (block == blocks.end()) {
    panic("No such block");
  }

  if (block->second.getValidPageCount() != 0) {
    panic("There are valid pages in victim block");
  }

  // Erase block
  block->second.erase();
  perase((uint64_t) &req, (uint64_t)&tick, 0);
  // pPAL->erase(req, tick);

  // Check erase count
  uint32_t erasedCount = block->second.getEraseCount();

  if (erasedCount < threshold) {
    // Reverse search
    auto iter = freeBlocks.end();

    while (true) {
      --iter;

      if (iter->getEraseCount() <= erasedCount) {
        // emplace: insert before pos
        ++iter;

        break;
      }

      if (iter == freeBlocks.begin()) {
        break;
      }
    }

    // Insert block to free block list
    freeBlocks.emplace(iter, move(block->second));
    nFreeBlocks++;
  }

  // Remove block from block list
  blocks.erase(block);
  uint64_t endAt = getTick();
  tick += endAt - beginAt;
}

float FTL::calculateWearLeveling() {
  uint64_t totalEraseCnt = 0;
  uint64_t sumOfSquaredEraseCnt = 0;
  uint64_t numOfBlocks = params.totalLogicalBlocks;
  uint64_t eraseCnt;

  for (auto iter : blocks) {
    eraseCnt = iter.second.getEraseCount();
    totalEraseCnt += eraseCnt;
    sumOfSquaredEraseCnt += eraseCnt * eraseCnt;
  }

  // freeBlocks is sorted
  // Calculate from backward, stop when eraseCnt is zero
  for (auto riter = freeBlocks.rbegin(); riter != freeBlocks.rend(); ++riter) {
    eraseCnt = riter->getEraseCount();

    if (eraseCnt == 0) {
      break;
    }

    totalEraseCnt += eraseCnt;
    sumOfSquaredEraseCnt += eraseCnt * eraseCnt;
  }

  if (sumOfSquaredEraseCnt == 0) {
    return -1;  // no meaning of wear-leveling
  }

  return (float)totalEraseCnt * totalEraseCnt /
         (numOfBlocks * sumOfSquaredEraseCnt);
}

void FTL::calculateTotalPages(uint64_t &valid, uint64_t &invalid) {
  valid = 0;
  invalid = 0;
  int i = 0;
  for (auto &iter : blocks) {
    valid += iter.second.getValidPageCount();
    invalid += iter.second.getDirtyPageCount();
  }
}

} // namespace FTL