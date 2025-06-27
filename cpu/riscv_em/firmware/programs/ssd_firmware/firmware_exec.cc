#include "ftl.hh"
#include "icl.hh"
#include "embext.h"
#include "utils.h"
#include "def.hh"
#include "cs_instructions.h"



void process_request(uint8_t req_id, uint64_t req_time) {

}

int main(int argc, char** argv) {
    FTL::ftl_params fparams;
    ICL::icl_params cparams;
    read_buffer((uint64_t)&fparams, 0, FIRMWARE_FTL_PARAMS);
    read_buffer((uint64_t)&cparams, 0, FIRMWARE_ICL_PARAMS);
    printf("read params\n");
    FTL::FTL ftl(fparams); // just initialize it for now
    ICL::ICL cache(cparams, &ftl);
    
    ICL::Request req;
    printf("icl and ftl loaded successfully!\n");
    stop_sim();
    while(1) {
        printf("executing loop\n");
        printf("test get tick: %u\n", getTick());
        if(getReqQueueSize() == 0) {
            req.reqType == ICL_REQ_EMPTY;
            stop_sim();   
        }
        read_buffer((uint64_t)&req, getCoreId(), FIRMWARE_QUEUE_TOP);
        if (req.reqType == ICL_REQ_READ) {
            cache.read(req);
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_WRITE) {
            printf("calling write\n");
            cache.write(req);
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_TRIM) {
            printf("calling trim\n");
            cache.trim(req.range);
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_FORMAT) {
            printf("calling format\n");
            cache.format(req.range); // format not supported yet
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_FLUSH) {
            printf("calling flush\n");
            cache.flush(req.range);
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_EMPTY) {
            // do nothing, just wait for the next request
        } else {
            panic("Unknown request type: %d\n", req.reqType);
        }
    }
    return 0;
}