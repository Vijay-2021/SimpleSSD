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

AbstractICL::AbstractICL(icl_params& cparams, FTL::FTL *ftl) :
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
  printf("calling icl initializer\n");
  printf("Line count in superpage: %u, line count in max IO: %u, way size: %u\n",
         lineCountInSuperPage, lineCountInMaxIO, waySize);
    printf("super page size is: %u, parallel IO: %u\n",
           superPageSize, parallelIO);
  uint64_t cacheSize = params.cacheSize;
  uint64_t heap_top = get_heap_top();
  write_buffer((uint64_t)&heap_top, 0, ICL_LOW);
  cache_buffer = (uint8_t*)malloc(cacheSize);
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
  mutex_init(&stat_mutex);
  memset(&stat, 0, sizeof(stat));
}

AbstractICL::~AbstractICL() {
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
}

uint32_t AbstractICL::calcSetIndex(uint64_t lca) {
  return lca % setSize;
}

void AbstractICL::calcIOPosition(uint64_t lca, uint32_t &row, uint32_t &col) {
  uint32_t tmp = lca % lineCountInMaxIO;

  row = tmp % lineCountInSuperPage;
  col = tmp / lineCountInSuperPage;
}

uint32_t AbstractICL::getEmptyWay(uint32_t setIdx) {
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

uint32_t AbstractICL::getValidWay(uint64_t lca) {
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

void AbstractICL::checkSequential(Request &req, SequentialDetect &data) {
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


void AbstractICL::resetStatValues() {
  memset(&icl_stats, 0, sizeof(icl_stats));
}

uint32_t AbstractICL::evictFunction(uint32_t setIdx) {
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

Line* AbstractICL::compareFunction(Line *a, Line *b) {
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