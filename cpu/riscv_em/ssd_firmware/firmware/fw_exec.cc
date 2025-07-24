#include "abstract_ftl.hh"
#include "abstract_icl.hh"
#include "datastructure_locks_icl.hh"
#include "simple_icl.hh"
#include "partitioned_icl.hh"
#include "partition_datastructure_icl.hh"
#include "locked_ftl.hh"
#include "unlocked_ftl.hh"
#include "utils.h"
#include "def.hh"
#include "cs_instructions.h"
#include "mutex.h"
#include "embext.hh"
#include "memory_allocator.hh"
#include "print_wrapper.hh"
#include "string_search.hh"

struct FirmwareData {
    FTL::FTL *ftl;
    ICL::AbstractICL *icl;
};

struct CSDJob {
    char fname[256];
    uint64_t file_size;
};

struct CSDData {
    MemoryAllocator *allocator;
    EmbExt2 *ext2;
    uint64_t start_pc;
};

Mutex queue_mutex;
int num_active_cores = 0;
EmbExt2 *fs_context = nullptr;
MemoryAllocator* allocator = nullptr;
PrintWrapper* print_wrapper = nullptr;

extern char _heap_start;

uint64_t heap_end = 0x100100000; // This is the end of the heap
uint64_t heap_begin = (uint64_t)&_heap_start;

void process_requests(FirmwareData *data) {
    ICL::AbstractICL *cache = data->icl;
    FTL::FTL *ftl = data->ftl;
    ICL::Request req;
    bool csd_job_loaded = false; 
    void *csd_buffer = nullptr;
    char csd_fname[256];
    while(1) {
        mutex_lock_untracked(&queue_mutex);
        if(getReqQueueSize() == 0) {
            // read_buffer((uint64_t)&csd_job_loaded, getCoreId(), FIRMWARE_CSD_JOB_LOADED);
            // if (getCSDQueueSize() > 0 && !csd_job_loaded) {
            //     read_buffer((uint64_t)csd_fname, getCoreId(), FIRMWARE_CSD_QUEUE_TOP);
            //     mutex_unlock(&queue_mutex);
            //     string_search("/home/data/inputs/test_hit.txt", "Hello", fs_context);
            // } else 
            if (num_active_cores == 0) {
                req.reqType == ICL_REQ_EMPTY;
                stop_sim();
                mutex_unlock(&queue_mutex);
                continue;
            } else {
                mutex_unlock(&queue_mutex);
                continue; // wait for active cores to finish
            }
        }

        read_buffer((uint64_t)&req, getCoreId(), FIRMWARE_QUEUE_TOP);
        num_active_cores++;
        mutex_unlock(&queue_mutex);
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
        mutex_lock_untracked(&queue_mutex);
        num_active_cores--;
        mutex_unlock(&queue_mutex);
    }
}

int main(int argc, char** argv) {
    printf("Starting SSD firmware\n");
    init_allocator((void*)heap_begin, (void*)heap_end, 1024*64, 64, 8); // Initialize the memory allocator
    FTL::ftl_params fparams;
    ICL::icl_params cparams;
    read_buffer((uint64_t)&fparams, 0, FIRMWARE_FTL_PARAMS);
    read_buffer((uint64_t)&cparams, 0, FIRMWARE_ICL_PARAMS);
    printf("read params\n");
    ICL::AbstractICL* cache; 
    FTL::FTL ftl(fparams); // just initialize it for now
    switch (cparams.cacheType) {
        case ICL::ICL_CACHE_SIMPLE:
            printf("Using SimpleICL cache\n");
            cache = new ICL::SimpleICL(cparams, &ftl);
            break;
        case ICL::ICL_CACHE_PARTITIONED: 
            printf("Using PartitionedICL cache\n");
            cache = new ICL::PartitionedICL(cparams, &ftl);
            break;
        case ICL::ICL_CACHE_DATASTRUCTURE:
            printf("Using DataStructureICL cache\n");
            cache = new ICL::DSICL(cparams, &ftl);
            break;
        case ICL::ICL_CACHE_PARTITIONED_DATASTRUCTURE:
            printf("Using PartitionedDataStructureICL cache\n");
            cache = new ICL::PartitionedDSICL(cparams, &ftl);
            break;
        default:
            panic("Unknown cache type: %d\n", cparams.cacheType);
    }
    
    write_buffer((uint64_t)&cache->icl_stats, 0, ICL_STAT_LOC);
    write_buffer((uint64_t)&ftl.ftl_stats, 0, FTL_STAT_LOC);
    printf("icl and ftl loaded successfully!\n");
    set_ext2_icl(cache);
    // fs_context = new EmbExt2(2048, block_get_volume_size(), 0);
    // allocator = new MemoryAllocator();
    // print_wrapper = new PrintWrapper();
    // printf("this is fine too\n");
    FirmwareData data;
    data.ftl = &ftl;
    data.icl = cache;
    mutex_init(&queue_mutex);
    write_buffer((uint64_t)&process_requests, (uint64_t)&data, CORE_SETUP_COMPLETED); // signal that core setup is done
    stop_sim();
    process_requests(&data);
    
    return 0;
}