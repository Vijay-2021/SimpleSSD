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


#include "icl.hh"
#include "random.h"
#include "memory.h"
#include "def.hh"
#include "cs_instructions.h"
#include "new.hh"
#include "firmware_utils.h"
namespace ICL {

Line::_Line()
    : tag(0), lastAccessed(0), insertedAt(0), dirty(false), valid(false) {}

Line::_Line(uint64_t t, bool d)
    : tag(t), lastAccessed(0), insertedAt(0), dirty(d), valid(true) {}

ICL::ICL(icl_params& cparams, FTL::FTL *ftl) :
      pFTL(ftl),
      params(cparams),
      superPageSize(cparams.pageSize),
      parallelIO(cparams.pageCountToMaxPerf),
      lineCountInSuperPage(cparams.ioUnitInPage),
      lineCountInMaxIO(cparams.pageCountToMaxPerf * cparams.ioUnitInPage),
      waySize(cparams.waySize),
      prefetchIOCount(cparams.prefetchCount),
      prefetchIORatio(cparams.prefetchRatio),
      useReadCaching(cparams.useReadCaching),
      useWriteCaching(cparams.useWriteCaching),
      useReadPrefetch(cparams.useReadPrefetch)  {
  printf("calling icl initializer!\n");
  uint64_t cacheSize = params.cacheSize;
  uint64_t heap_top = get_heap_top();
  write_buffer((uint64_t)&heap_top, 0, ICL_LOW);
  cache_buffer = (uint8_t*)malloc(cacheSize);
  if (cache_buffer == NULL) {
    printf("ICL: Failed to allocate cache memory of size %u bytes.\n", cacheSize);
    return;
  }
  heap_top = get_heap_top();
  write_buffer((uint64_t)&heap_top, 0, ICL_HIGH);
  
  lineSize = superPageSize / lineCountInSuperPage;

  if (lineSize != superPageSize) {
    bSuperPage = true;
  }

  if (!params.useRandomIOTweak) {
    lineSize = superPageSize;
    lineCountInSuperPage = 1;
    lineCountInMaxIO = parallelIO;
  }
  copy_buffer = (uint8_t*)malloc(lineSize); // use this to simulate reading cache data out to a buffer for HIL processing
  if (!useReadCaching && !useWriteCaching) {
    printf("Read and write caching are disabled, not creating cache.\n");
    printf("useReadCaching: %u useWriteCaching: %u useReadPrefetch: %u\n",
         (uint32_t)useReadCaching, (uint32_t)useWriteCaching, (uint32_t)useReadPrefetch);
    return;
  }

  // Fully-associated?
  if (waySize == 0) {
    setSize = 1;
    waySize = MAX(cacheSize / lineSize, 1);
  }
  else {
    setSize = MAX(cacheSize / lineSize / waySize, 1);
  }

  // debugprint(
  //    LOG_ICL_GENERIC_CACHE,
  //    "CREATE  | Set size %u | Way size %u | Line size %u | Capacity %" PRIu64,
  //    setSize, waySize, lineSize, (uint64_t)setSize * waySize * lineSize);
  // debugprint(LOG_ICL_GENERIC_CACHE,
  //           "CREATE  | line count in super page %u | line count in max I/O %u",
  //           lineCountInSuperPage, lineCountInMaxIO);

  cacheData.resize(setSize);
  for (uint32_t i = 0; i < setSize; i++) {
    cacheData[i] = new Line[waySize]();
  }

  evictData.resize(lineCountInSuperPage);

  for (uint32_t i = 0; i < lineCountInSuperPage; i++) {
    evictData[i] = (Line **)calloc(parallelIO, sizeof(Line *));
  }

  prefetchTrigger = 0xFFFFFFFFFFFFFFFF;// numeric_limits<uint64_t>::max();

  evictMode = params.iclEvictGranularity;
  prefetchMode = params.iclPrefetchGranularity;

  memset(&stat, 0, sizeof(stat));
}

ICL::~ICL() {
  if (!useReadCaching && !useWriteCaching) {
    return;
  }

  for (uint32_t i = 0; i < setSize; i++) {
    delete[] cacheData[i];
  }

  for (uint32_t i = 0; i < lineCountInSuperPage; i++) {
    free(evictData[i]);
  }
  free(cache_buffer);
  free(copy_buffer);
}

uint32_t ICL::calcSetIndex(uint64_t lca) {
  return lca % setSize;
}

void ICL::calcIOPosition(uint64_t lca, uint32_t &row, uint32_t &col) {
  uint32_t tmp = lca % lineCountInMaxIO;

  row = tmp % lineCountInSuperPage;
  col = tmp / lineCountInSuperPage;
}

uint32_t ICL::getEmptyWay(uint32_t setIdx) {
  uint32_t retIdx = waySize;
  uint64_t minInsertedAt = 0xFFFFFFFFFFFFFFFF; // std::numeric_limits<uint64_t>::max();

  for (uint32_t wayIdx = 0; wayIdx < waySize; wayIdx++) {
    Line &line = cacheData[setIdx][wayIdx];

    if (!line.valid) {
      if (minInsertedAt > line.insertedAt) {
        minInsertedAt = line.insertedAt;
        retIdx = wayIdx;
      }
    }
  }

  return retIdx;
}

uint32_t ICL::getValidWay(uint64_t lca) {
  uint32_t setIdx = calcSetIndex(lca);
  uint32_t wayIdx;
  for (wayIdx = 0; wayIdx < waySize; wayIdx++) {
    Line &line = cacheData[setIdx][wayIdx];
    if (line.valid && line.tag == lca) {
      break;
    }
  }

  return wayIdx;
}

void ICL::checkSequential(Request &req, SequentialDetect &data) {
  if (data.lastRequest.reqID == req.reqID) {
    data.lastRequest.range = req.range;
    data.lastRequest.offset = req.offset;
    data.lastRequest.length = req.length;

    return;
  }

  if (data.lastRequest.range.slpn * lineSize + data.lastRequest.offset +
          data.lastRequest.length ==
      req.range.slpn * lineSize + req.offset) {
    if (!data.enabled) {
      data.hitCounter++;
      data.accessCounter += data.lastRequest.offset + data.lastRequest.length;

      if (data.hitCounter >= prefetchIOCount &&
          (float)data.accessCounter / superPageSize >= prefetchIORatio) {
        data.enabled = true;
      }
    }
  }
  else {
    data.enabled = false;
    data.hitCounter = 0;
    data.accessCounter = 0;
  }

  data.lastRequest = req;
}

void ICL::evictCache(bool flush) {
  FTL::Request reqInternal(lineCountInSuperPage);

  for (uint32_t row = 0; row < lineCountInSuperPage; row++) {
    uint64_t tick = getTick();
    for (uint32_t col = 0; col < parallelIO; col++) {
      uint64_t beginAt = tick; // all parallel requests start at the same time

      if (evictData[row][col] == nullptr) {
        continue;
      }

      if (evictData[row][col]->valid && evictData[row][col]->dirty) {
        reqInternal.lpn = evictData[row][col]->tag / lineCountInSuperPage;
        reqInternal.ioFlag.reset();
        reqInternal.ioFlag.set(row);

        beginAt = pFTL->write(reqInternal); // update with ftl return time
      }

      if (flush) {
        evictData[row][col]->valid = false;
        evictData[row][col]->tag = 0;
      }

      evictData[row][col]->insertedAt = beginAt;
      evictData[row][col]->lastAccessed = beginAt;
      evictData[row][col]->dirty = false;
      evictData[row][col] = nullptr;
    }
  }
}

// True when hit
bool ICL::read(Request &req) {
  bool ret = false;
  icl_stats.read_requests++;
  uint64_t start_cycle = getCycle();

  // debugprint(LOG_ICL_GENERIC_CACHE,
  //           "READ  | REQ %7u-%-4u | LCA %" PRIu64 " | SIZE %" PRIu64,
  //           req.reqID, req.reqSubID, req.range.slpn, req.length);

  if (useReadCaching) {
    uint32_t setIdx = calcSetIndex(req.range.slpn);
    uint32_t wayIdx;
    // uint64_t arrived = getTick();
    if (useReadPrefetch) {
      checkSequential(req, readDetect);
    }
    wayIdx = getValidWay(req.range.slpn);
    // Do we have valid data?
    if (wayIdx != waySize) {

      // Wait cache to be valid(this shouldn't happen?)
      // if (tick < cacheData[setIdx][wayIdx].insertedAt) {
      //   tick = cacheData[setIdx][wayIdx].insertedAt;
      // }

      // Update last accessed time
      icl_stats.read_cache_hits++;
      icl_stats.read_req_cycles += getCycle() - start_cycle;
      cacheData[setIdx][wayIdx].lastAccessed = getTick();

      if (req.length < lineSize) {
        memcpy(copy_buffer, cache_buffer + (setIdx * waySize + wayIdx) * lineSize, req.length);
      } else {
        uint64_t length = req.length;
        while (length > 0) {
          uint64_t read_length = MIN(length, lineSize);
          memcpy(copy_buffer, cache_buffer + (setIdx * waySize + wayIdx) * lineSize, read_length);
          length -= read_length;
        }
      }
      // debugprint(LOG_ICL_GENERIC_CACHE,
      //           "READ  | Cache hit at (%u, %u) | %" PRIu64 " - %" PRIu64
      //           " (%" PRIu64 ")",
      //           setIdx, wayIdx, arrived, tick, tick - arrived);

      ret = true;

      // Do we need to prefetch data?
      if (useReadPrefetch && req.range.slpn == prefetchTrigger) {
        // debugprint(LOG_ICL_GENERIC_CACHE, "READ  | Prefetch triggered");
        req.range.slpn = lastPrefetched;

        goto ICL_GENERIC_CACHE_READ;
      } else {
        process_request(getTick());
      }
    }
    // We should read data from NVM
    else {
    ICL_GENERIC_CACHE_READ:
      FTL::Request reqInternal(lineCountInSuperPage, req);
      Vector<Pair<uint64_t, uint64_t>> readList;
      uint32_t row, col;  // Variable for I/O position (IOFlag)
      uint64_t dramAt;
      uint64_t beginLCA, endLCA;
      

      if (readDetect.enabled) {

        if (!ret) {
          // debugprint(LOG_ICL_GENERIC_CACHE, "READ  | Read ahead triggered");
        }

        beginLCA = req.range.slpn;

        // If super-page is disabled, just read all pages from all planes
        if (prefetchMode == MODE_ALL || !bSuperPage) {
          endLCA = beginLCA + lineCountInMaxIO;
          prefetchTrigger = beginLCA + lineCountInMaxIO / 2;
        }
        else {
          endLCA = beginLCA + lineCountInSuperPage;
          prefetchTrigger = beginLCA + lineCountInSuperPage / 2;
        }

        lastPrefetched = endLCA;
      }
      else {
        beginLCA = req.range.slpn;
        endLCA = beginLCA + 1;
      }

      for (uint64_t lca = beginLCA; lca < endLCA; lca++) {

        // Check cache
        if (getValidWay(lca) != waySize) {
          continue;
        }

        // Find way to write data read from NVM
        setIdx = calcSetIndex(lca);
        wayIdx = getEmptyWay(setIdx);

        if (wayIdx == waySize) {
          wayIdx = evictFunction(setIdx);

          if (cacheData[setIdx][wayIdx].dirty) {
            // We need to evict data before write
            calcIOPosition(cacheData[setIdx][wayIdx].tag, row, col);
            evictData[row][col] = cacheData[setIdx] + wayIdx;
          }
        }

        cacheData[setIdx][wayIdx].insertedAt = getTick();
        cacheData[setIdx][wayIdx].lastAccessed = getTick();
        cacheData[setIdx][wayIdx].valid = true;
        cacheData[setIdx][wayIdx].dirty = false;

        readList.push_back({lca, ((uint64_t)setIdx << 32) | wayIdx});
      }

      evictCache();

      uint64_t beginAt, finishedAt = getTick();

      for (auto &iter : readList) {
        Line *pLine = &cacheData[iter.second >> 32][iter.second & 0xFFFFFFFF];

        // Read data
        reqInternal.lpn = iter.first / lineCountInSuperPage;
        reqInternal.ioFlag.reset();
        reqInternal.ioFlag.set(iter.first % lineCountInSuperPage);

        beginAt = getTick();  // Ignore cache metadata access

        // If superPageSizeData is true, read first LPN only
        uint64_t read_time = pFTL->read(reqInternal);

        // DRAM delay
        // dramAt = pLine->insertedAt;
        memcpy(cache_buffer + (iter.second >> 32) * waySize * lineSize + (iter.second & 0xFFFFFFFF) * lineSize, copy_buffer, lineSize);

        // Set cache data
        beginAt = MAX(beginAt, read_time);

        pLine->insertedAt = beginAt;
        pLine->lastAccessed = beginAt;
        pLine->tag = iter.first;

        if (pLine->tag == req.range.slpn) {
          finishedAt = beginAt;
        }

        // debugprint(LOG_ICL_GENERIC_CACHE,
        //           "READ  | Cache miss at (%u, %u) | %" PRIu64 " - %" PRIu64
        //           " (%" PRIu64 ")",
        //           iter.second >> 32, iter.second & 0xFFFFFFFF, tick, beginAt,
        //           beginAt - tick);
      }

      process_request(finishedAt);
    }
  }
  else {
    FTL::Request reqInternal(lineCountInSuperPage, req);

    //simulates writing data to dram after retrieving it from NVM, however we do in opposite order for timing correctness
    if (req.length <= lineSize) {
      memcpy(cache_buffer, copy_buffer, req.length);
    }
    else {
      uint64_t length = req.length;
      while (length > 0) {
        uint64_t read_length = MIN(length, lineSize);
        memcpy(cache_buffer, copy_buffer, read_length); // the exact location of copy doesn't matter, we just need to simulate writing data
        length -= read_length;
      }
    }
    process_request(pFTL->read(reqInternal));
  }

  stat.request[0]++;

  if (ret) {
    stat.cache[0]++;
  }

  return ret;
}

// True when cold-miss/hit
bool ICL::write(Request &req) {
  printf("icl write called!\n");
  bool ret = false;
  uint64_t tick = getTick();
  uint64_t flash = tick;
  bool dirty = false;
  icl_stats.write_requests++;
  uint64_t start_cycle = getCycle();

  // debugprint(LOG_ICL_GENERIC_CACHE,
  //           "WRITE | REQ %7u-%-4u | LCA %" PRIu64 " | SIZE %" PRIu64,
  //           req.reqID, req.reqSubID, req.range.slpn, req.length);

  FTL::Request reqInternal(lineCountInSuperPage, req);

  if (req.length < lineSize) {
    dirty = true;
  }
  else {
    printf("immediately calling ftl for write\n");
    flash = pFTL->write(reqInternal);
    process_request(flash);
  }

  if (useWriteCaching) {
    uint32_t setIdx = calcSetIndex(req.range.slpn);
    uint32_t wayIdx;

    wayIdx = getValidWay(req.range.slpn);

    // Can we update old data?
    if (wayIdx != waySize) {
      uint64_t arrived = tick;

      // Wait cache to be valid
      if (tick < cacheData[setIdx][wayIdx].insertedAt) {
        tick = cacheData[setIdx][wayIdx].insertedAt;
      }

      // TODO: TEMPORAL CODE
      // We should only show DRAM latency when cache become dirty
      if (dirty) {
        // Update last accessed time
        cacheData[setIdx][wayIdx].insertedAt = tick;
        cacheData[setIdx][wayIdx].lastAccessed = tick;
      }
      else {
        cacheData[setIdx][wayIdx].insertedAt = flash;
        cacheData[setIdx][wayIdx].lastAccessed = flash;
      }

      // Update last accessed time
      cacheData[setIdx][wayIdx].dirty = dirty; 

      // DRAM access
      if (req.length < lineSize) {
        memcpy(cache_buffer + (setIdx * waySize + wayIdx) * lineSize, copy_buffer, req.length);
      } else {
        uint64_t length = req.length;
        while (length > 0) {
          uint64_t write_length = MIN(length, lineSize);
          memcpy(cache_buffer + (setIdx * waySize + wayIdx) * lineSize, copy_buffer, write_length);
          length -= write_length;
        }
      }

      // debugprint(LOG_ICL_GENERIC_CACHE,
      //           "WRITE | Cache hit at (%u, %u) | %" PRIu64 " - %" PRIu64
      //           " (%" PRIu64 ")",
      //           setIdx, wayIdx, arrived, tick, tick - arrived);

      ret = true;
      if (dirty) {
        process_request(getTick());
      }
    }
    else {
      uint64_t arrived = tick;

      wayIdx = getEmptyWay(setIdx);

      // Do we have place to write data?
      if (wayIdx != waySize) {
        // Wait cache to be valid
        if (tick < cacheData[setIdx][wayIdx].insertedAt) {
          tick = cacheData[setIdx][wayIdx].insertedAt;
        }

        // TODO: TEMPORAL CODE
        // We should only show DRAM latency when cache become dirty
        if (dirty) {
          // Update last accessed time
          cacheData[setIdx][wayIdx].insertedAt = getTick();
          cacheData[setIdx][wayIdx].lastAccessed = getTick();
        }
        else {
          cacheData[setIdx][wayIdx].insertedAt = flash;
          cacheData[setIdx][wayIdx].lastAccessed = flash;
        }

        // Update last accessed time
        cacheData[setIdx][wayIdx].valid = true;
        cacheData[setIdx][wayIdx].dirty = dirty;
        cacheData[setIdx][wayIdx].tag = req.range.slpn;

        // DRAM access
        if (req.length < lineSize) {
          memcpy(cache_buffer + (setIdx * waySize + wayIdx) * lineSize, copy_buffer, req.length);
        } else {
          uint64_t length = req.length;
          while (length > 0) {
            uint64_t write_length = MIN(length, lineSize);
            memcpy(cache_buffer + (setIdx * waySize + wayIdx) * lineSize, copy_buffer, write_length);
            length -= write_length;
          }
        }
        if (dirty) {
          process_request(getTick());
        }
        ret = true;
      }
      // We have to flush
      else {
        uint32_t row, col;  // Variable for I/O position (IOFlag)
        uint32_t setToFlush = calcSetIndex(req.range.slpn);

        for (setIdx = 0; setIdx < setSize; setIdx++) {
          for (wayIdx = 0; wayIdx < waySize; wayIdx++) {
            if (cacheData[setIdx][wayIdx].valid) {
              calcIOPosition(cacheData[setIdx][wayIdx].tag, row, col);

              evictData[row][col] = compareFunction(evictData[row][col],
                                                    cacheData[setIdx] + wayIdx);
            }
          }
        }

        if (evictMode == MODE_SUPERPAGE) {
          uint32_t row, col;  // Variable for I/O position (IOFlag)

          for (row = 0; row < lineCountInSuperPage; row++) {
            for (col = 0; col < parallelIO - 1; col++) {
              evictData[row][col + 1] =
                  compareFunction(evictData[row][col], evictData[row][col + 1]);
              evictData[row][col] = nullptr;
            }
          }
        }

        // We must flush setToFlush set
        bool have = false;

        for (row = 0; row < lineCountInSuperPage; row++) {
          for (col = 0; col < parallelIO; col++) {
            if (evictData[row][col] &&
                calcSetIndex(evictData[row][col]->tag) == setToFlush) {
              have = true;
            }
          }
        }

        // We don't have setToFlush
        if (!have) {
          Line *pLineToFlush = nullptr;

          for (wayIdx = 0; wayIdx < waySize; wayIdx++) {
            if (cacheData[setToFlush][wayIdx].valid) {
              pLineToFlush =
                  compareFunction(pLineToFlush, cacheData[setToFlush] + wayIdx);
            }
          }

          if (pLineToFlush) {
            calcIOPosition(pLineToFlush->tag, row, col);

            evictData[row][col] = pLineToFlush;
          }
        }


        evictCache(true);

        // Update cacheline of current request
        setIdx = setToFlush;
        wayIdx = getEmptyWay(setIdx);

        if (wayIdx == waySize) {
          panic("Cache corrupted!");
        }

        // DRAM latency
        if (req.length < lineSize) {
          memcpy(cache_buffer + (setIdx * waySize + wayIdx) * lineSize, copy_buffer, req.length);
        } else {
          uint64_t length = req.length;
          while (length > 0) {
            uint64_t write_length = MIN(length, lineSize);
            memcpy(cache_buffer + (setIdx * waySize + wayIdx) * lineSize, copy_buffer, write_length);
            length -= write_length;
          }
        }

        // Update cache data
        cacheData[setIdx][wayIdx].insertedAt = getTick();
        cacheData[setIdx][wayIdx].lastAccessed = getTick();
        cacheData[setIdx][wayIdx].valid = true;
        cacheData[setIdx][wayIdx].dirty = true;
        cacheData[setIdx][wayIdx].tag = req.range.slpn;
        if (dirty) {
          process_request(getTick());
        }
      }

      // debugprint(LOG_ICL_GENERIC_CACHE,
      //           "WRITE | Cache miss at (%u, %u) | %" PRIu64 " - %" PRIu64
      //           " (%" PRIu64 ")",
      //           setIdx, wayIdx, arrived, tick, tick - arrived);
    }

  }
  else {
    if (dirty) {
      process_request(pFTL->write(reqInternal));
    }

    // TEMP: Disable DRAM calculation for prevent conflict

    if (req.length < lineSize) {
      memcpy(copy_buffer, cache_buffer + req.range.slpn * lineSize, req.length);
    } else {
      uint64_t length = req.length;
      while (length > 0) {
        uint64_t read_length = MIN(length, lineSize);
        memcpy(copy_buffer, cache_buffer + req.range.slpn * lineSize, read_length);
        length -= read_length;
      }
    }
  }

  stat.request[1]++;

  if (ret) {
    stat.cache[1]++;
  }

  return ret;
}

// True when flushed
void ICL::flush(LPNRange &range) {
  printf("Icl flush called!\n");
  icl_stats.flush_requests++;
  uint64_t start_cycle = getCycle();
  if (useReadCaching || useWriteCaching) {
    uint64_t finishedAt = getTick();
    FTL::Request reqInternal(lineCountInSuperPage);

    for (uint32_t setIdx = 0; setIdx < setSize; setIdx++) {
      for (uint32_t wayIdx = 0; wayIdx < waySize; wayIdx++) {
        Line &line = cacheData[setIdx][wayIdx];

        if (line.tag >= range.slpn && line.tag < range.slpn + range.nlp) {
          if (line.dirty) {
            reqInternal.lpn = line.tag / lineCountInSuperPage;
            reqInternal.ioFlag.set(line.tag % lineCountInSuperPage);
            uint64_t ftlTick = pFTL->write(reqInternal);
            finishedAt = MAX(finishedAt, ftlTick);
          }

          line.valid = false;
        }
      }
    }

    process_request(finishedAt);
  }
  process_request_failed();
}

// True when hit
void ICL::trim(LPNRange &range) {
  printf("Icl trim called!\n");
  icl_stats.trim_requests++;
  uint64_t start_cycle = getCycle();
  if (useReadCaching || useWriteCaching) {
    uint64_t finishedAt = getTick();
    FTL::Request reqInternal(lineCountInSuperPage);

    for (uint32_t setIdx = 0; setIdx < setSize; setIdx++) {
      for (uint32_t wayIdx = 0; wayIdx < waySize; wayIdx++) {
        Line &line = cacheData[setIdx][wayIdx];

        if (line.tag >= range.slpn && line.tag < range.slpn + range.nlp) {
          reqInternal.lpn = line.tag / lineCountInSuperPage;
          reqInternal.ioFlag.set(line.tag % lineCountInSuperPage);
          uint64_t trim_tick = pFTL->trim(reqInternal);
          finishedAt = MAX(finishedAt, trim_tick);

          line.valid = false;
        }
      }
    }
    process_request(finishedAt);
  }
  process_request_failed();
}

void ICL::format(LPNRange &range) {
  printf("Icl format called!\n");
  icl_stats.format_requests++;
  uint64_t start_cycle = getCycle();
  if (useReadCaching || useWriteCaching) {
    uint64_t lpn;
    uint32_t setIdx;
    uint32_t wayIdx;

    for (uint64_t i = 0; i < range.nlp; i++) {
      lpn = range.slpn + i;
      setIdx = calcSetIndex(lpn);
      wayIdx = getValidWay(lpn);

      if (wayIdx != waySize) {
        // Invalidate
        cacheData[setIdx][wayIdx].valid = false;
      }
    }
  }

  // Convert unit
  range.slpn /= lineCountInSuperPage;
  range.nlp = (range.nlp - 1) / lineCountInSuperPage + 1;

  process_request(pFTL->format(range));
}


void ICL::resetStatValues() {
  memset(&icl_stats, 0, sizeof(icl_stats));
}

uint32_t ICL::evictFunction(uint32_t setIdx) {
  uint32_t wayIdx = 0;
  uint64_t min = 0xFFFFFFFFFFFFFFFF; // std::numeric_limits<uint64_t>::max();

  for (uint32_t i = 0; i < waySize; i++) {
    if (cacheData[setIdx][i].lastAccessed < min) {
      min = cacheData[setIdx][i].lastAccessed;
      wayIdx = i;
    }
  }

  return wayIdx;
}

Line* ICL::compareFunction(Line *a, Line *b) {
  if (a && b) {
    if (a->lastAccessed < b->lastAccessed) {
      return a;
    }
    else {
      return b;
    }
  }
  else if (a || b) {
    return a ? a : b;
  }
  else {
    return nullptr;
  }
}

}