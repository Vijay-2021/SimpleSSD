#include "def.hh"

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

namespace FTL {

Request::_Request(uint32_t iocount)
    : reqID(0), reqSubID(0), lpn(0), ioFlag(iocount), reqType(FTL_REQ_EMPTY) {
}

Request::_Request() : reqID(0), reqSubID(0), lpn(0), ioFlag(0), reqType(FTL_REQ_EMPTY) {}

}


void* operator new(size_t size) {
    void* p = malloc(size);
    return p;
}

void operator delete(void* p) noexcept {
    free(p);
}

void operator delete(void* ptr, size_t size) noexcept {
    (void)size; 
    free(ptr);
}

void* operator new[](size_t size) noexcept {
    return malloc(size);
}

void operator delete[](void* ptr) noexcept {
    free(ptr);
}

void operator delete[](void* ptr, size_t size) noexcept {
    (void)size;
    free(ptr);
}