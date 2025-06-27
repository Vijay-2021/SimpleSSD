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

#ifndef __ICL_GENERIC_CACHE__
#define __ICL_GENERIC_CACHE__

#include "vector.hh"
#include "ftl.hh"

namespace ICL {

typedef struct _Line {
  uint64_t tag;
  uint64_t lastAccessed;
  uint64_t insertedAt;
  bool dirty;
  bool valid;

  _Line();
  _Line(uint64_t, bool);
} Line;

typedef enum {
  POLICY_RANDOM,               //!< Select way in random
  POLICY_FIFO,                 //!< Select way that lastly inserted
  POLICY_LEAST_RECENTLY_USED,  //!< Select way that least recently used
} EVICT_POLICY;

typedef enum {
  MODE_SUPERPAGE,  //!< Read one page from one super block (super page)
  MODE_ALL,        //!< Read one page from all NAND flashes
} PREFETCH_MODE;

typedef PREFETCH_MODE EVICT_MODE;

struct __attribute__((packed, aligned(4))) icl_params {
    uint32_t pageSize;
    uint32_t pageCountToMaxPerf;
    uint32_t ioUnitInPage;
    uint32_t waySize;
    uint32_t prefetchCount;
    float prefetchRatio;
    uint32_t useReadCaching;
    uint32_t useWriteCaching;
    uint32_t useReadPrefetch;
    uint32_t useRandomIOTweak;
    uint64_t cacheSize;
    EVICT_MODE iclEvictGranularity;
    PREFETCH_MODE iclPrefetchGranularity;
};



class ICL {
 private:
  FTL::FTL *pFTL;
  const uint32_t superPageSize;
  const uint32_t parallelIO;
  uint32_t lineCountInSuperPage;
  uint32_t lineCountInMaxIO;
  uint32_t lineSize;
  uint32_t setSize;
  uint32_t waySize;

  const uint32_t prefetchIOCount;
  const float prefetchIORatio;

  const bool useReadCaching;
  const bool useWriteCaching;
  const bool useReadPrefetch;

  bool bSuperPage;

  struct SequentialDetect {
    bool enabled;
    Request lastRequest;
    uint32_t hitCounter;
    uint32_t accessCounter;

    SequentialDetect() : enabled(false), hitCounter(0), accessCounter(0) {
      lastRequest.reqID = 1;
    }
  } readDetect;

  uint64_t prefetchTrigger;
  uint64_t lastPrefetched;

  PREFETCH_MODE prefetchMode;
  EVICT_MODE evictMode;

  Vector<Line *> cacheData;
  Vector<Line **> evictData;

  uint32_t evictFunction(uint32_t setIdx);
  Line* compareFunction(Line *a, Line *b);

  uint64_t getCacheLatency();

  uint32_t calcSetIndex(uint64_t);
  void calcIOPosition(uint64_t, uint32_t &, uint32_t &);

  uint32_t getEmptyWay(uint32_t);
  uint32_t getValidWay(uint64_t);
  void checkSequential(Request &, SequentialDetect &);

  void evictCache(bool = true);

  // Stats
  struct {
    uint64_t request[2];
    uint64_t cache[2];
  } stat;

  icl_params params;

 public:
  ICL(icl_params& cparams, FTL::FTL *ftl);
  ~ICL();

  bool read(Request &);
  bool write(Request &);
  void flush(LPNRange &);
  void trim(LPNRange &);
  void format(LPNRange &);

  void resetStatValues();
};

}

#endif
