#ifndef __ABSTRACT_ICL_HH__
#define __ABSTRACT_ICL_HH__

#include "vector.hh"
#include "ftl.hh"
#include "mutex.h"
#include "abstract_icl.hh"

namespace FTL {
    class FTL;
}

namespace ICL {

typedef struct _Line {
    uint64_t tag;
    uint64_t lastAccessed;
    uint64_t insertedAt;
    bool dirty;
    bool valid;

    _Line();
    _Line(uint64_t, bool);
} Line;

typedef enum {
    POLICY_RANDOM,               //!< Select way in random
    POLICY_FIFO,                 //!< Select way that lastly inserted
    POLICY_LEAST_RECENTLY_USED,  //!< Select way that least recently used
} EVICT_POLICY;

typedef enum {
    MODE_SUPERPAGE,  //!< Read one page from one super block (super page)
    MODE_ALL,        //!< Read one page from all NAND flashes
} PREFETCH_MODE;

typedef PREFETCH_MODE EVICT_MODE;

typedef enum {
    ICL_CACHE_SIMPLE = 0,                //!< Simple cache
    ICL_CACHE_PARTITIONED = 1,           //!< Partitioned cache
    ICL_CACHE_DATASTRUCTURE = 2,         //!< Data structure cache
    ICL_CACHE_PARTITIONED_DATASTRUCTURE = 3, //!< Partitioned data structure cache
} ICL_CACHE_TYPE;



struct __attribute__((packed, aligned(4))) icl_params {
    uint32_t pageSize;
    uint32_t pageCountToMaxPerf;
    uint32_t ioUnitInPage;
    uint32_t waySize;
    uint32_t prefetchCount;
    float prefetchRatio;
    uint32_t useReadCaching;
    uint32_t useWriteCaching;
    uint32_t useReadPrefetch;
    uint32_t useRandomIOTweak;
    uint64_t cacheSize;
    EVICT_MODE iclEvictGranularity;
    PREFETCH_MODE iclPrefetchGranularity;
    ICL_CACHE_TYPE cacheType; //!< Type of cache
};

struct ICLStats {
    uint64_t read_requests;
    uint64_t write_requests;
    uint64_t trim_requests;
    uint64_t format_requests;
    uint64_t flush_requests;
    uint64_t read_cache_hits;
    uint64_t write_cache_hits;
    uint64_t read_cache_misses;
    uint64_t write_cache_misses;
    uint64_t cache_evictions;
    uint64_t read_req_cycles;
    uint64_t write_req_cycles;
    uint64_t trim_req_cycles;
    uint64_t format_req_cycles;
    uint64_t flush_req_cycles;
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t trim_bytes;
    uint64_t format_bytes;
    uint64_t flush_bytes;
    uint64_t heap_top;
};

class AbstractICL {
    protected:
        FTL::FTL *pFTL;
        const uint32_t superPageSize;
        const uint32_t parallelIO;
        uint32_t lineCountInSuperPage;
        uint32_t lineCountInMaxIO;
        uint32_t lineSize;
        uint32_t setSize;
        uint32_t waySize;

        const uint32_t prefetchIOCount;
        const float prefetchIORatio;

        const bool useReadCaching;
        const bool useWriteCaching;
        const bool useReadPrefetch;

        bool bSuperPage;
        uint8_t* cache_buffer;
        uint8_t *copy_buffer;
        struct SequentialDetect {
            bool enabled;
            Request lastRequest;
            uint32_t hitCounter;
            uint32_t accessCounter;

            SequentialDetect() : enabled(false), hitCounter(0), accessCounter(0) {
            lastRequest.reqID = 1;
            }
        } readDetect;

        uint64_t prefetchTrigger;
        uint64_t lastPrefetched;

        PREFETCH_MODE prefetchMode;
        EVICT_MODE evictMode;

        Vector<Line *> cacheData;
        Vector<Line **> evictData;

        // Stats
        struct {
            uint64_t request[2];
            uint64_t cache[2];
        } stat;


        icl_params params;
        //Mutex evict_data_mutex;
        //Mutex cache_data_mutex;
        uint32_t calcSetIndex(uint64_t lca);
        void calcIOPosition(uint64_t lca, uint32_t &row, uint32_t &col);
        uint32_t getEmptyWay(uint32_t setIdx);
        uint32_t getValidWay(uint64_t lca);
        void checkSequential(Request &req, SequentialDetect &data);
        uint32_t evictFunction(uint32_t setIdx);
        Line* compareFunction(Line *a, Line *b);
        virtual void evictCache(bool = true) = 0;
        Mutex stat_mutex;
    public:
        virtual bool read(Request &) = 0;
        virtual bool write(Request &) = 0;
        virtual void flush(LPNRange &) = 0;
        virtual void trim(LPNRange &) = 0;
        virtual void format(LPNRange &) = 0;
        AbstractICL(icl_params& cparams, FTL::FTL *ftl);
        ~AbstractICL();
        void resetStatValues();
        ICLStats icl_stats;
};
}

#endif // __ABSTRACT_ICL_HH__