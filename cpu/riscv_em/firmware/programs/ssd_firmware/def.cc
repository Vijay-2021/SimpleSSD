#include "def.hh"
#include "cs_instructions.h"

LPNRange::_LPNRange() : slpn(0), nlp(0) {}

LPNRange::_LPNRange(uint64_t s, uint64_t n) : slpn(s), nlp(n) {}

namespace ICL {

Request::_Request() : reqID(0), reqSubID(0), offset(0), length(0), reqType(ICL_REQ_EMPTY) {}

}

namespace FTL {

Request::_Request()
    : reqID(0), reqSubID(0), lpn(0), ioFlag(0) {}

Request::_Request(uint32_t iocount)
    : reqID(0), reqSubID(0), lpn(0), ioFlag(iocount) {}

Request::_Request(uint32_t iocount, ICL::Request &r)
    : reqID(r.reqID),
      reqSubID(r.reqSubID),
      lpn(r.range.slpn / iocount),
      ioFlag(iocount) {
  ioFlag.set(r.range.slpn % iocount);
}

}  // namespace FTL


namespace PAL {

Request::_Request(uint32_t iocount)
    : reqID(0), reqSubID(0), blockIndex(0), pageIndex(0), ioFlag(iocount) {}

Request::_Request(FTL::Request &r)
    : reqID(r.reqID),
      reqSubID(r.reqSubID),
      blockIndex(0),
      pageIndex(0),
      ioFlag(r.ioFlag) {}

}  // namespace PAL

void process_request(uint64_t req_time) {
    uint64_t core_id = getCoreId();
    printf("Processing request with ID: %u at time: %u\n", core_id, req_time);
    write_buffer((uint64_t)&req_time, core_id, FIRMWARE_REQ_DONE);
}

void process_request_failed() {
    uint64_t core_id = getCoreId();
    printf("Processing request failed for core ID: %lu\n", core_id);
    write_buffer(0, core_id, FIRMWARE_REQ_FAILED);
}

uint64_t getTick() {
    uint64_t tick = 0;
    read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
    return tick;
}

uint64_t getCoreId() {
    uint64_t core_id;
    read_buffer((uint64_t)&core_id, 0, FIRMWARE_CORE_ID);
    return core_id;
}

uint64_t getReqQueueSize() {
    uint64_t size;
    read_buffer((uint64_t)&size, 0, FIRMWARE_QUEUE_SIZE);
    return size;
}