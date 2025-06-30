#ifndef __FIRMWARE_DEFINITIONS__
#define __FIRMWARE_DEFINITIONS__

#include <stdint.h>
#include <stdbool.h>
#include "bitset.hh"
#include <limits.h>
#include "memory.h"
#include "new.hh"

typedef struct _LPNRange {
  uint64_t slpn;
  uint64_t nlp;

  _LPNRange();
  _LPNRange(uint64_t, uint64_t);
} LPNRange;

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
    FIRMWARE_FTL_PARAMS = 0, 
    FIRMWARE_ICL_PARAMS = 1,
    FIRMWARE_QUEUE_TOP = 2,
    FIRMWARE_TICK = 3,
    FIRMWARE_CYCLE = 4,
    FIRMWARE_CORE_ID = 5,
    FIRMWARE_QUEUE_SIZE = 6,
} DATA_REQ;

typedef enum {
  FIRMWARE_REQ_DONE = 0,
  FIRMWARE_REQ_FAILED = 1,
  STAT_WRITE = 2,
} DATA_RESP; 

typedef enum {
  ICL_REQ_READ = 0,
  ICL_REQ_WRITE = 1,
  ICL_REQ_TRIM = 2,
  ICL_REQ_FORMAT = 3,
  ICL_REQ_FLUSH = 4,
  ICL_REQ_EMPTY = 5,
} ICL_REQ_TYPE;

namespace ICL {

typedef struct _Request {
  uint64_t reqID;
  uint64_t reqSubID;
  uint64_t offset;
  uint64_t length;
  LPNRange range;
  ICL_REQ_TYPE reqType;
  _Request();
} Request;

}

namespace FTL {

typedef struct _Request {
  uint64_t reqID;  // ID of ICL::Request
  uint64_t reqSubID;
  uint64_t lpn;
  Bitset ioFlag;
  _Request(uint32_t, ICL::Request &);
  _Request(uint32_t iocount);
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

void process_request(uint64_t req_time);
void process_request_failed();
uint64_t getTick();
uint64_t getCoreId();
uint64_t getReqQueueSize();
#endif