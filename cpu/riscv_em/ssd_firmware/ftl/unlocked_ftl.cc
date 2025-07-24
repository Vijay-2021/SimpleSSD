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

#include "unlocked_ftl.hh"
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
  printf("io Unit in page: %u and pageCountToMaxPerf: %u and bRandomTweak %u and ftl_gc_mode %d and ftl_filling_mode %d and choice param %u and reclaim block %u\n", 
         params.ioUnitInPage, params.pageCountToMaxPerf, (uint32_t)params.bRandomTweak, (int)params.ftl_gc_mode, (int)params.ftl_filling_mode, params.choiceParam, params.ftl_gc_reclaim_block);
  for (uint32_t i = 0; i < params.totalPhysicalBlocks; i++) {
    freeBlocks.emplace_back(move(Block(i, params.pagesInBlock, params.ioUnitInPage)));
  }
  for (auto &block : freeBlocks) {
    if (!block.isValid()) {
      panic("Block is not valid during initialization");
    }
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
  memset(&ftl_stats, 0, sizeof(ftl_stats));
  bRandomTweak = params.bRandomTweak;
  bitsetSize = bRandomTweak ? params.ioUnitInPage : 1;
  mutex_init(&block_mutex);
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
  uint64_t ret_val;
  ftl_stats.read_requests++;
  uint64_t start_cycle;
  uint64_t end_cycle;
  read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE);
  if (req.ioFlag.count() > 0) {
    ret_val = readInternal(req);
  }
  else {
    print("FTL got empty request\n");
    ret_val = getTick();
  }
  read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
  ftl_stats.read_req_cycles += end_cycle - start_cycle;
  return ret_val;

}

uint64_t FTL::write(Request &req) {
  uint64_t ret_val;
  ftl_stats.write_requests++;
  uint64_t start_cycle = getCycle();
  if (req.ioFlag.count() > 0) {
    ret_val = writeInternal(req);
  }
  else {
    printf("FTL got empty request for data addr %u\n", (uint64_t)(&req.ioFlag.data));
    ret_val = getTick();
  }
  ftl_stats.write_req_cycles += getCycle() - start_cycle;
  return ret_val;

}

uint64_t FTL::trim(Request &req) {
  uint64_t ret_val;
  ftl_stats.trim_requests++;
  uint64_t start_cycle = getCycle();
  ret_val = trimInternal(req);
  ftl_stats.trim_req_cycles += getCycle() - start_cycle;
  return ret_val;
}


uint64_t FTL::format(LPNRange &range) {
  ftl_stats.format_requests++;
  uint64_t start_cycle = getCycle();
  PAL::Request req(params.ioUnitInPage);
  Vector<uint32_t> list;

  req.ioFlag.set();

  for (uint64_t idx = range.slpn; idx < range.slpn + range.nlp; idx++) {
      auto *mappingList = &table[idx];

      // Do trim
      mutex_lock(&block_mutex);
      for (uint32_t idx = 0; idx < bitsetSize; idx++) {
        auto &mapping = mappingList->at(idx);
        
        auto *block = &blocks[mapping.first];

        if (!block->isValid()) {
          // panic("Block is not in use");
        } else {

          block->invalidate(mapping.second, idx);

          // Collect block indices
          list.push_back(mapping.first);
        }
      }
      mutex_unlock(&block_mutex);
      table[idx].clear();
  }

  // Get blocks to erase
  merge_sort(list.begin(), list.end());
  auto last = Unique(list.begin(), list.end());
  while (last != list.end()) {
    list.pop_back();
  }

  // Do GC only in specified blocks
  uint64_t ret_val = doGarbageCollection(list);
  ftl_stats.format_req_cycles += getCycle() - start_cycle;
  return ret_val;
} 

float FTL::freeBlockRatio() {
  mutex_lock(&block_mutex);
  float ret_val = (float)nFreeBlocks / params.totalPhysicalBlocks;
  mutex_unlock(&block_mutex);
  return ret_val;
}

uint32_t FTL::convertBlockIdx(uint32_t blockIdx) {
  return blockIdx % params.pageCountToMaxPerf;
}

uint32_t FTL::getFreeBlock(uint32_t idx) {
  uint32_t blockIndex = 0;

  if (idx >= params.pageCountToMaxPerf) {
    panic("Index out of range");
  }
  mutex_lock(&block_mutex);
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
    if (blocks[blockIndex].isValid()) {
      panic("Corrupted here");
    }
    blocks[blockIndex] = move(*iter);
    // Remove found block from free block list
    freeBlocks.erase(iter);
    nFreeBlocks--;
  }
  else {
    panic("No free block left");
  }
  mutex_unlock(&block_mutex);
  return blockIndex;
}

uint32_t FTL::getLastFreeBlock(Bitset &iomap) {
  mutex_lock(&block_mutex);
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

  auto* freeBlock = &blocks[lastFreeBlock.at(lastFreeBlockIndex)];
  // Sanity check
  if (!freeBlock->isValid()) {
    panic("Corrupted over here");
  }

  // If current free block is full, get next block
  if (freeBlock->getNextWritePageIndex() == params.pagesInBlock) {
    lastFreeBlock.at(lastFreeBlockIndex) = getFreeBlock(lastFreeBlockIndex);

    bReclaimMore = true;
  }
  mutex_unlock(&block_mutex);
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
      for (size_t i = 0; i < blocks.size(); i++) {

        //block is not full!
        if (blocks[i].getNextWritePageIndex() != params.pagesInBlock) {
          continue;
        }
        weight.push_back({i, blocks[i].getValidPageCountRaw()});
      }

      break;
    case POLICY_COST_BENEFIT:
      for (size_t i = 0; i < blocks.size(); i++) {
        if (blocks[i].getNextWritePageIndex() != params.pagesInBlock) {
          continue;
        }

        temp = (float)(blocks[i].getValidPageCountRaw()) / params.pagesInBlock;
        weight.push_back(
            {i,
             temp / ((1 - temp) * (getTick() - blocks[i].getLastAccessedTime()))});
      }

      break;
    default:
      panic("Invalid evict policy");
  }
}

void FTL::selectVictimBlock(Vector<uint32_t> &list) {
  const GC_MODE mode = params.ftl_gc_mode;
  const EVICT_POLICY policy = params.ftl_evict_policy;
  uint32_t dChoiceParam = params.choiceParam;
  uint64_t nBlocks = params.ftl_gc_reclaim_block;
  Vector<Pair<uint32_t, float>> weight;
  printf("started selectVictimBlock with mode: %d, policy: %d, reclaim block: %u\n",
         mode, policy, nBlocks);
  list.clear();

  // Calculate number of blocks to reclaim
  if (mode == GC_MODE_0) {
    // DO NOTHING
  }
  else if (mode == GC_MODE_1) {
    printf("GC_MODE_1 selected\n");
    const float t = params.ftl_gc_reclaim_threshold;

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
  printf("nBlocks to reclaim: %u\n", nBlocks);

  // Calculate weights of all blocks
  calculateVictimWeight(weight, policy);
  printf("calculated victim weights, size: %u\n", weight.size());

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
  printf("finished all of this\n");

  // Sort weights
  merge_sort(
      weight.begin(), weight.end(),
      [](Pair<uint32_t, float> a, Pair<uint32_t, float> b) -> bool {
        return a.second < b.second;
      });

  // Select victims from the blocks with the lowest weight
  nBlocks = MIN(nBlocks, weight.size());

  for (uint64_t i = 0; i < nBlocks; i++) {
    printf("adding %u to list\n", weight.at(i).first);
    list.push_back(weight.at(i).first);
  }

}

uint64_t FTL::doGarbageCollection(Vector<uint32_t> &blocksToReclaim) {
  printf("doGarbageCollection called with %u blocks to reclaim\n", blocksToReclaim.size());
  PAL::Request req(params.ioUnitInPage);
  Vector<PAL::Request> readRequests;
  Vector<PAL::Request> writeRequests;
  Vector<PAL::Request> eraseRequests;
  Vector<uint64_t> lpns;
  Bitset bit(params.ioUnitInPage);

  if (blocksToReclaim.size() == 0) {
    uint64_t tick;
    read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
    return tick;
  }

  // For all blocks to reclaim, collecting request structure only
  for (auto &iter : blocksToReclaim) {
    auto *block = &blocks[iter];

    if (!block->isValid()) {
      panic("Invalid block");
    }

    // Copy valid pages to free block
    for (uint32_t pageIndex = 0; pageIndex < params.pagesInBlock; pageIndex++) {
      // Valid?
      if (block->getPageInfo(pageIndex, lpns, bit)) {
        if (!bRandomTweak) {
          bit.set();
        }

        // Retrieve free block
        uint32_t newBlockIdx = getLastFreeBlock(bit);
        auto *freeBlock = &blocks[newBlockIdx];

        // Issue Read
        req.blockIndex = newBlockIdx;
        req.pageIndex = pageIndex;
        req.ioFlag = bit;

        readRequests.push_back(req);

        // Update mapping table

        for (uint32_t idx = 0; idx < bitsetSize; idx++) {
          if (bit.test(idx)) {
            // Invalidate
            block->invalidate(pageIndex, idx);

            auto *mappingList = &table[lpns.at(idx)];

            if (mappingList->size() == 0) {
              panic("Invalid mapping table entry");
            }

            auto &mapping = mappingList->at(idx);

            uint32_t newPageIdx = freeBlock->getNextWritePageIndex(idx);

            mapping.first = newBlockIdx;
            mapping.second = newPageIdx;

            freeBlock->write(newPageIdx, lpns.at(idx), idx);

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
    req.blockIndex = iter;
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
    pread((uint64_t) &iter, (uint64_t)&beginAt);
    //pPAL->read(iter, beginAt);

    readFinishedAt = MAX(readFinishedAt, beginAt);
  }

  for (auto &iter : writeRequests) {
    beginAt = readFinishedAt;
    pwrite((uint64_t) &iter, (uint64_t)&beginAt); // will implement it in the backend so that ticks are handled
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
  auto *mappingList = &table[req.lpn];
  if (mappingList->size() == 0) { // create a mapping for testing purposes, will add configuration to this later
    table[req.lpn] = Vector<Pair<uint32_t, uint32_t>>(
            bitsetSize, {params.totalPhysicalBlocks, params.pagesInBlock});
  }
  if (mappingList->size() != 0) {
    PAL::Request palRequest(req);
    uint64_t beginAt;
    uint64_t finishedAt;
    read_buffer((uint64_t)&finishedAt, 0, FIRMWARE_TICK);
    for (uint32_t idx = 0; idx < bitsetSize; idx++) {
      if (req.ioFlag.test(idx) || !bRandomTweak) {
        auto *mapping = &mappingList->at(idx);

        if (mapping->first < params.totalPhysicalBlocks &&
            mapping->second < params.pagesInBlock) {
          palRequest.blockIndex = mapping->first;
          palRequest.pageIndex = mapping->second;

          if (bRandomTweak) {
            palRequest.ioFlag.reset();
            palRequest.ioFlag.set(idx);
          }
          else {
            palRequest.ioFlag.set();
          }
          mutex_lock(&block_mutex);
          auto* block = &blocks[palRequest.blockIndex];

          if (!block->isValid()) {
            panic("Block is not in use");
          }
          read_buffer((uint64_t)&beginAt, 0, FIRMWARE_TICK);

          block->read(palRequest.pageIndex, idx);
          mutex_unlock(&block_mutex);
          pread((uint64_t) &palRequest, (uint64_t)&beginAt);
          // pPAL->read(palRequest, beginAt);

          finishedAt = MAX(finishedAt, beginAt);
        }
      }
    }

    return finishedAt;
  }
  // If no mapping found, read
  uint64_t tick;
  read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
  return tick; // No mapping found, return current tick
}

uint64_t FTL::writeInternal(Request &req, bool sendToPAL) {
  PAL::Request palRequest(req);
  Block *block;
  auto *mappingList = &table[req.lpn];
  uint64_t beginAt = 0;
  uint64_t finishedAt = 0;
  read_buffer((uint64_t)&finishedAt, 0, FIRMWARE_TICK);
  bool readBeforeWrite = false;

  if (mappingList->size() > 0) {
    for (uint32_t idx = 0; idx < bitsetSize; idx++) {
      if (req.ioFlag.test(idx) || !bRandomTweak) {
        auto *mapping = &mappingList->at(idx);

        if (mapping->first < params.totalPhysicalBlocks &&
            mapping->second < params.pagesInBlock) {
          mutex_lock(&block_mutex);
          block = &blocks[mapping->first];

          // Invalidate current page
          block->invalidate(mapping->second, idx);
          mutex_unlock(&block_mutex);
        }
      }
    }
  }
  else {
    // Create empty mapping
    table[req.lpn] = Vector<Pair<uint32_t, uint32_t>>(
            bitsetSize, {params.totalPhysicalBlocks, params.pagesInBlock});
  }

  // Write data to free block

  uint32_t blockIdx = getLastFreeBlock(req.ioFlag);
  mutex_lock(&block_mutex);
  block = &blocks[blockIdx];

  if (!block->isValid()) {
    panic("No such block");
  }
  mutex_unlock(&block_mutex);
  if (!bRandomTweak && !req.ioFlag.all()) {
    // We have to read old data
    readBeforeWrite = true;
  }

  for (uint32_t idx = 0; idx < bitsetSize; idx++) {
    if (req.ioFlag.test(idx) || !bRandomTweak) {
      mutex_lock(&block_mutex);
      uint32_t pageIndex = block->getNextWritePageIndex(idx);
      auto *mapping = &mappingList->at(idx);

      read_buffer((uint64_t)&beginAt, 0, FIRMWARE_TICK);

      block->write(pageIndex, req.lpn, idx);
      mutex_unlock(&block_mutex);
      // Read old data if needed (Only executed when bRandomTweak = false)
      // Maybe some other init procedures want to perform 'partial-write'
      // So check sendToPAL variable
      if (readBeforeWrite && sendToPAL) {
        palRequest.blockIndex = mapping->first;
        palRequest.pageIndex = mapping->second;

        // We don't need to read old data
        palRequest.ioFlag = req.ioFlag;
        palRequest.ioFlag.flip();
        read_buffer((uint64_t)&beginAt, 0, FIRMWARE_TICK);
        pread((uint64_t) &palRequest, (uint64_t)&beginAt);
        //pPAL->read(palRequest, beginAt);
      }

      // update mapping to table
      mapping->first = blockIdx;
      mapping->second = pageIndex;

      if (sendToPAL) {
        palRequest.blockIndex = blockIdx;
        palRequest.pageIndex = pageIndex;

        if (bRandomTweak) {
          palRequest.ioFlag.reset();
          palRequest.ioFlag.set(idx);
        }
        else {
          palRequest.ioFlag.set();
        }
        pwrite((uint64_t) &palRequest, (uint64_t)&beginAt);
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
  float gcThreshold = params.ftl_gc_threshold_ratio;
  if (freeBlockRatio() < gcThreshold) {
    if (!sendToPAL) {
      panic("ftl: GC triggered while in initialization");
    }

    Vector<uint32_t> list;
    uint64_t beginAt = getTick();

    selectVictimBlock(list);

    printf("GC   | On-demand | %u blocks will be reclaimed", list.size());

    uint64_t ret_time = doGarbageCollection(list);

    printf(" GC Done | %u - %u (%u) and finished at is: %u", ret_time, beginAt, ret_time - beginAt, finishedAt);
    
  }
  return finishedAt; // ignore gc time in PAL(the cpu time will still be considered as well as the time for writing to PAL for specific request and extra GC requests will affect PAL performance of subsequent requests)
    
}

uint64_t FTL::trimInternal(Request &req) {
  auto *mappingList = &table[req.lpn];

  if (mappingList->size() > 0) {

    // Do trim
    for (uint32_t idx = 0; idx < bitsetSize; idx++) {
      auto *mapping = &mappingList->at(idx);
      if (mapping->first >= params.totalPhysicalBlocks ||
          mapping->second >= params.pagesInBlock) {
        panic("Invalid mapping");
      }
      auto block = blocks[mapping->first];
      block.invalidate(mapping->second, idx);
    }

    // Remove mapping
    table[req.lpn].clear();
  }
  uint64_t tick;
  read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
  return tick;
}

void FTL::eraseInternal(PAL::Request &req, uint64_t &tick) {
  uint64_t beginAt;
  read_buffer((uint64_t)&beginAt, 0, FIRMWARE_TICK);
  uint64_t threshold = params.bad_block_threshold;
  mutex_lock(&block_mutex);
  Block* block = &blocks[req.blockIndex];
  uint64_t pal_time;
  // Sanity checks
  if (!block->isValid()) {
    panic("No such block");
  }

  if (block->getValidPageCount() != 0) {
    panic("There are valid pages in victim block");
  }

  // Erase block
  block->erase();
  perase((uint64_t) &req, (uint64_t)&tick);
  // pPAL->erase(req, tick);

  // Check erase count
  uint32_t erasedCount = block->getEraseCount();
  
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
    freeBlocks.emplace(iter, move(*block));
    nFreeBlocks++;
  }

  // Remove block from block list
  blocks[req.blockIndex] = Block(); // set to invalid block
  mutex_unlock(&block_mutex);
  uint64_t endAt;
  read_buffer((uint64_t)&endAt, 0, FIRMWARE_TICK);
  tick += endAt - beginAt;
}

float FTL::calculateWearLeveling() {
  uint64_t totalEraseCnt = 0;
  uint64_t sumOfSquaredEraseCnt = 0;
  uint64_t numOfBlocks = params.totalLogicalBlocks;
  uint64_t eraseCnt;

  for (auto& iter : blocks) {
    eraseCnt = iter.getEraseCount();
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
    valid += iter.getValidPageCount();
    invalid += iter.getDirtyPageCount();
  }
}

} // namespace FTL