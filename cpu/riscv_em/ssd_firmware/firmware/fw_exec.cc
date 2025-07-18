#include "ftl.hh"
#include "abstract_icl.hh"
#include "datastructure_locks_icl.hh"
#include "simple_icl.hh"
#include "partitioned_icl.hh"
#include "partition_datastructure_icl.hh"
#include "utils.h"
#include "def.hh"
#include "cs_instructions.h"
#include "mutex.h"
#include "embext.hh"
#include "memory_allocator.hh"
#include "print_wrapper.hh"
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
void process_requests(FirmwareData *data) {
    ICL::AbstractICL *cache = data->icl;
    FTL::FTL *ftl = data->ftl;
    ICL::Request req;
    bool csd_job_loaded = false; 
    void *csd_buffer = nullptr;
    while(1) {
        mutex_lock(&queue_mutex);
        if(getReqQueueSize() == 0) {
            read_buffer((uint64_t)&csd_job_loaded, getCoreId(), FIRMWARE_CSD_JOB_LOADED);
            if (getCSDQueueSize() > 0 && !csd_job_loaded) {
                char csd_job[256];
                memset(csd_job, 0, sizeof(csd_job));
                read_buffer((uint64_t)csd_job, getCoreId(), FIRMWARE_CSD_QUEUE_TOP); // break this into two instructions to avoid deadlock
                //mutex_unlock(&queue_mutex);
                printf("~~~~~~~~READ QUEUE~~~~~~~~~~~~~~ for file name %s\n", csd_job);
                void* vfe = fs_context->open(csd_job, O_RDONLY, 0777);
                printf("~~~~~~~~OPENED FILE~~~~~~~~~~~~~~\n");
                if (vfe == nullptr) {
                    printf("Failed to open file %s\n", csd_job);
                    continue; // skip this job
                }
                printf("~~~~~~~~FILE SUCCESS~~~~~~~~~~~~~~\n");
                int size = fs_context->lseek(vfe, 0, SEEK_END);
                if (size <= 0) {
                    fs_context->close(vfe);
                    printf("Failed to seek to end of file %s\n", csd_job);
                    continue; // skip this job
                    // handle error
                }
                printf("~~~~~~~~LSEEK SUCCESS~~~~~~~~~~~~~~\n");
                if (fs_context->lseek(vfe, 0, SEEK_SET) < 0) {
                    fs_context->close(vfe);
                    printf("Failed to seek to start of file %s\n", csd_job);
                    continue; // skip this job
                    // handle error
                }
                printf("~~~~~~~SEEK BACK SUCCESSS~~~~~~~~~~~~~~\n");
                printf("File %s size: %d bytes\n", csd_job, size);
                uint8_t *file = (uint8_t *)allocator->mmalloc(size);
                if (!file) {
                    fs_context->close(vfe);
                    printf("Failed to allocate memory for file %s\n", csd_job);
                    continue; // skip this job
                    // handle error
                }
                printf("~~~~~~~~MALLOC SUCCESS~~~~~~~~~~~~~~\n");
                int bytes_read = fs_context->read(vfe, file, size);
                if (bytes_read != size) {
                    allocator->mfree(file);
                    fs_context->close(vfe);
                    printf("Failed to read file %s, expected %d bytes, got %d bytes\n", csd_job, size, bytes_read);
                    continue; // skip this job
                    // handle error
                }
                printf("~~~~~~~~READ SUCCESS~~~~~~~~~~~~~~\n");
                fs_context->close(vfe);
                printf("~~~~~~~~CLOSED FILE ENTRY~~~~~~~~~~~~~~\n");
                CSDData job_data;
                job_data.start_pc = (uint64_t)file;
                job_data.allocator = allocator;
                job_data.ext2 = fs_context;
    
                printf("~~~~~~~~WRITING TO START CSD with file pointer %u~~~~~~~~~~~~~~\n", (uint64_t)file);
                write_buffer((uint64_t)&job_data, getCoreId(), START_CSD_JOB); 
                mutex_unlock(&queue_mutex);
                continue; // if we re-enter the firmware, we should process the request queue again
            } else if (csd_job_loaded) {
                bool csd_job_finished = false;
                read_buffer((uint64_t)&csd_job_finished, getCoreId(), FIRMWARE_CSD_JOB_FINISHED);
                if (csd_job_finished) {
                    mutex_unlock(&queue_mutex);
                    free(csd_buffer);
                    write_buffer(0, getCoreId(), CLEANED_CSD_JOB);
                } else {
                    mutex_unlock(&queue_mutex);
                    write_buffer(0, getCoreId(), RESUME_CSD_JOB);
                }
            } else if (num_active_cores == 0) {
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
        mutex_lock(&queue_mutex);
        num_active_cores--;
        mutex_unlock(&queue_mutex);
    }
}

int main(int argc, char** argv) {
    printf("Starting SSD firmware\n");
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
        case ICL::ICL_CACHE_DATASTRUCTURE:
            printf("Using DataStructureICL cache\n");
            cache = new ICL::DSICL(cparams, &ftl);
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
    fs_context = new EmbExt2(2048, block_get_volume_size(), 0);
    allocator = new MemoryAllocator();
    print_wrapper = new PrintWrapper();
    printf("this is fine too\n");
    FirmwareData data;
    data.ftl = &ftl;
    data.icl = cache;
    mutex_init(&queue_mutex);
    write_buffer((uint64_t)&process_requests, (uint64_t)&data, CORE_SETUP_COMPLETED); // signal that core setup is done
    stop_sim();
    process_requests(&data);
    
    return 0;
}