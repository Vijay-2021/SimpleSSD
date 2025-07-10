#include "ftl.hh"
#include "icl.hh"
#include "embext.h"
#include "utils.h"
#include "def.hh"
#include "cs_instructions.h"

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
    ICL::Request req;
    LPNRange range(0, 1);
    printf("icl and ftl loaded successfully!\n");
    printf("Simple computation took %u cycles\n", end_cycle - start_cycle);

    req.reqType = ICL_REQ_READ;
    req.reqID = 0; 
    req.reqSubID = 0;
    req.offset = 0;
    req.length = 512;
    req.range = range;
    uint64_t first_cycle = getCycle();
    //cache.read(req);
    uint64_t last_cycle = getCycle();
    printf("Read request took %u cycles\n", last_cycle - first_cycle);
    stop_sim();
    
    return 0;
}