#include "ftl.hh"
#include "icl.hh"
#include "utils.h"
#include "def.hh"
#include "cs_instructions.h"
#include "mutex.h"

struct FirmwareData {
    FTL::FTL *ftl;
    ICL::ICL *icl;
};

Mutex print_mutex;

void process_requests(FirmwareData *data) {
    ICL::ICL *cache = data->icl;
    FTL::FTL *ftl = data->ftl;
    ICL::Request req;
    LPNRange range(0, 1);
    req.reqType = ICL_REQ_READ;
    req.reqID = 0;
    req.reqSubID = 0;
    req.offset = 0;
    req.length = 65536;
    req.range = range;
    uint64_t start_cycle = getCycle();
    cache->write(req);
    uint64_t end_cycle = getCycle();
    mutex_lock(&print_mutex);
    printf("Write request took %u cycles\n", end_cycle - start_cycle);
    mutex_unlock(&print_mutex);

    while(1) {
        // do nothing for now
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
    printf("initialized icl and ftl\n");
    ICL::Request req[4];
    LPNRange range[4];
    for (int i = 0; i < 4; i++) {
        req[i].reqType = ICL_REQ_READ;
        req[i].reqID = i;
        req[i].reqSubID = 0;
        req[i].offset = 0;
        req[i].length = 512;
        range[i].slpn = 0;
        range[i].nlp = 1;
        req[i].range = range[i];
    }
    printf("icl and ftl loaded successfully!\n");
    FirmwareData data;
    data.ftl = &ftl;
    data.icl = &cache;
    write_buffer((uint64_t)&process_requests, (uint64_t)&data, CORE_SETUP_COMPLETED); // signal that core setup is done
    stop_sim();
    mutex_init(&print_mutex);
    process_requests(&data);
    uint64_t first_cycle = getCycle();
    cache.read(req[getCoreId()]);
    uint64_t last_cycle = getCycle();
    printf("Read request took %u cycles\n", last_cycle - first_cycle);
    // stop_sim();
    
    return 0;
}