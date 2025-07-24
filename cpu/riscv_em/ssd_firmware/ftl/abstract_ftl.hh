// #ifndef __ABSTRACT_FTL_HH__
// #define __ABSTRACT_FTL_HH__

// namespace FTL {

// struct __attribute__((packed, aligned(4))) ftl_params {
//   uint64_t totalPhysicalBlocks;
//   uint64_t totalLogicalBlocks;
//   uint64_t pagesInBlock;
//   uint32_t pageSize;
//   uint32_t ioUnitInPage;
//   uint32_t pageCountToMaxPerf;  
//   uint32_t bRandomTweak;
//   float ftl_fill_ratio;
//   float ftl_invalid_page_ratio;
//   FILLING_MODE ftl_filling_mode; // or FILLING_MODE
//   float ftl_gc_threshold_ratio;
//   GC_MODE ftl_gc_mode;
//   EVICT_POLICY ftl_evict_policy;
//   uint32_t choiceParam;
//   uint64_t ftl_gc_reclaim_block;
//   float ftl_gc_reclaim_threshold;
//   uint64_t bad_block_threshold;
// };

// class AbstractFTL {
//     protected:
//         ftl_params params;
//         struct 
//     public:
//         virtual uint64_t read(Request &) = 0;
//         virtual uint64_t write(Request &) = 0;
//         virtual uint64_t trim(Request &) = 0;
//         virtual uint64_t format(LPNRange &) = 0;
//         AbstractFTL(ftl_params &fparams); 
//         ~AbstractFTL();

// };

// }

// #endif