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
    read_buffer((uint64_t)&fparams, FIRMWARE_FTL_PARAMS);
    read_buffer((uint64_t)&cparams, FIRMWARE_ICL_PARAMS);
    printf("read params\n");
    FTL::FTL ftl(fparams); // just initialize it for now
    ICL::ICL cache(cparams, &ftl);
    
    ICL::Request req;
    printf("icl and ftl loaded successfully!\n");
    stop_sim();
    while(1) {
        printf("executing loop\n");
        read_buffer((uint64_t)&req, FIRMWARE_QUEUE_TOP);
        if (req.reqType == ICL_REQ_READ) {
            printf("calling read\n");
            cache.read(req);
            stop_sim();
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_WRITE) {
            printf("calling write\n");
            cache.write(req);
            stop_sim();
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_TRIM) {
            printf("calling trim\n");
            cache.trim(req.range);
            stop_sim();
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_FORMAT) {
            printf("calling format\n");
            cache.format(req.range); // format not supported yet
            stop_sim();
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_EMPTY) {
            // do nothing, just wait for the next request
        } else {
            panic("Unknown request type: %d\n", req.reqType);
        }
    }
    return 0;
}