#include "ftl.hh"
#include "icl.hh"
#include "embext.h"
#include "utils.h"
#include "def.hh"
#include "cs_instructions.h"

int exec(FTL::FTL* ftl, ICL::ICL* icl) {
    ICL::Request req; // core local request to copy data into
    while(1) {
        printf("executing loop\n");
        printf("test get tick: %u\n", getTick());
        if(getReqQueueSize() == 0) {
            req.reqType == ICL_REQ_EMPTY;
            stop_sim();   
        }
        read_buffer((uint64_t)&req, getCoreId(), FIRMWARE_QUEUE_TOP);
        if (req.reqType == ICL_REQ_READ) {
            icl->read(req);
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_WRITE) {
            printf("calling write\n");
            icl->write(req);
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_TRIM) {
            printf("calling trim\n");
            icl->trim(req.range);
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_FORMAT) {
            printf("calling format\n");
            icl->format(req.range); // format not supported yet
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_FLUSH) {
            printf("calling flush\n");
            icl->flush(req.range);
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_EMPTY) {
            // do nothing, just wait for the next request
        } else {
            panic("Unknown request type: %d\n", req.reqType);
        }
    }
    return 0;
}