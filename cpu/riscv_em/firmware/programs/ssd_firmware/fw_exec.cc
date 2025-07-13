#include "ftl.hh"
#include "icl.hh"
#include "embext.h"
#include "utils.h"
#include "def.hh"
#include "cs_instructions.h"
#include "mutex.hh"

struct FirmwareData {
    FTL::FTL *ftl;
    ICL::ICL *icl;
};

Mutex* queue_mutex;
int num_active_cores = 0;
void process_requests(FirmwareData *data) {
    ICL::ICL *cache = data->icl;
    FTL::FTL *ftl = data->ftl;
    ICL::Request req;
    while(1) {
        queue_mutex->lock();
        if(getReqQueueSize() == 0) {
            if (num_active_cores == 0) {
                req.reqType == ICL_REQ_EMPTY;
                stop_sim();
                queue_mutex->unlock();
                continue;
            } else {
                queue_mutex->unlock();
                continue; // wait for active cores to finish
            }
        }

        read_buffer((uint64_t)&req, getCoreId(), FIRMWARE_QUEUE_TOP);
        num_active_cores++;
        queue_mutex->unlock();
        if (req.reqType == ICL_REQ_READ) {
            cache->read(req);
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_WRITE) {
            cache->write(req);
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_TRIM) {
            cache->trim(req.range);
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_FORMAT) {
            cache->format(req.range); // format not supported yet
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_FLUSH) {
            cache->flush(req.range);
            req.reqType == ICL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == ICL_REQ_EMPTY) {
            // do nothing, just wait for the next request
        } else {
            panic("Unknown request type: %d\n", req.reqType);
        }
        queue_mutex->lock();
        num_active_cores--;
        queue_mutex->unlock();
    }
}

int main(int argc, char** argv) {
    FTL::ftl_params fparams;
    ICL::icl_params cparams;
    read_buffer((uint64_t)&fparams, 0, FIRMWARE_FTL_PARAMS);
    read_buffer((uint64_t)&cparams, 0, FIRMWARE_ICL_PARAMS);
    printf("read params\n");
    FTL::FTL ftl(fparams); // just initialize it for now
    ICL::ICL cache(cparams, &ftl);
    write_buffer((uint64_t)&cache.icl_stats, 0, ICL_STAT_LOC);
    write_buffer((uint64_t)&ftl.ftl_stats, 0, FTL_STAT_LOC);
    printf("icl and ftl loaded successfully!\n");
    FirmwareData data;
    data.ftl = &ftl;
    data.icl = &cache;
    queue_mutex = new Mutex();
    write_buffer((uint64_t)&process_requests, (uint64_t)&data, CORE_SETUP_COMPLETED); // signal that core setup is done
    
    process_requests(&data);
    // stop_sim();
    
    return 0;
}