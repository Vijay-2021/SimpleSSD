#ifndef __FIRMWARE_DEFINITIONS__
#define __FIRMWARE_DEFINITIONS__

#include <stdint.h>
#include <stdbool.h>
#include "bitset.hh"
#include <limits.h>
#include "memory.h"


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
    FIRMWARE_PARAMS = 0, 
    FIRMWARE_QUEUE = 1,
    FIRMWARE_TICK = 2,
} DATA_REQ;

typedef enum {
  FTL_REQ_READ = 0,
  FTL_REQ_WRITE = 1,
  FTL_REQ_TRIM = 2,
  FTL_REQ_FORMAT = 3,
  FTL_REQ_EMPTY = 4,
} FTL_REQ_TYPE;


struct firmware_params {
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
};

namespace FTL {

typedef struct _Request {
  uint64_t reqID;  // ID of ICL::Request
  uint64_t reqSubID;
  uint64_t lpn;
  Bitset ioFlag;
  FTL_REQ_TYPE reqType;
  _Request(uint32_t);
  _Request();
} Request;

}  // namespace FTL

namespace PAL {

typedef struct _Request {
  uint64_t reqID;  // ID of ICL::Request
  uint64_t reqSubID;
  uint32_t blockIndex;
  uint32_t pageIndex;
  Bitset ioFlag;

  _Request(uint32_t);
  _Request(FTL::Request &);
} Request;

}  // namespace PAL

template <typename T>
uint8_t popcount(T v) {
  v = v - ((v >> 1) & (T) ~(T)0 / 3);
  v = (v & (T) ~(T)0 / 15 * 3) + ((v >> 2) & (T) ~(T)0 / 15 * 3);
  v = (v + (v >> 4)) & (T) ~(T)0 / 255 * 15;
  v = (T)(v * ((T) ~(T)0 / 255)) >> (sizeof(T) - 1) * CHAR_BIT;

  return (uint8_t)v;
}


void* operator new(size_t size);
void operator delete(void* p);
void operator delete(void* ptr, size_t size);
void* operator new[](size_t size);
void operator delete[](void* ptr);
void operator delete[](void* ptr, size_t size);

#endif