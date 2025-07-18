#ifndef __FIRMWARE_DEFINITIONS__
#define __FIRMWARE_DEFINITIONS__

#include <stdint.h>
#include <stdbool.h>
#include "bitset.hh"
#include <limits.h>
#include "memory.h"
#include "new.hh"
#include "cs_instructions.h"
#include "def.h"

typedef struct _LPNRange {
  uint64_t slpn;
  uint64_t nlp;

  _LPNRange();
  _LPNRange(uint64_t, uint64_t);
} LPNRange;

struct FTLStats {
    uint64_t read_requests;
    uint64_t write_requests;
    uint64_t trim_requests;
    uint64_t format_requests;
    uint64_t garbage_collection_requests;
    uint64_t read_req_cycles;
    uint64_t write_req_cycles;
    uint64_t trim_req_cycles;
    uint64_t format_req_cycles;
    uint64_t gc_req_cycles;
};


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
static inline uint64_t getCycle() {
    uint64_t cycle = 0;
    read_buffer((uint64_t)&cycle, 0, FIRMWARE_CYCLE);
    return cycle;
}

static inline uint64_t getCoreId() {
    uint64_t core_id;
    read_buffer((uint64_t)&core_id, 0, FIRMWARE_CORE_ID);
    return core_id;
}
uint64_t getReqQueueSize();
uint64_t getCSDQueueSize();
void assert(bool condition);

#endif