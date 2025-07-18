#ifndef __DEF_H__
#define __DEF_H__

typedef enum {
  /* Common FTL configuration */        
  FTL_MAPPING_MODE          = 0,
  FTL_OVERPROVISION_RATIO   = 1,
  FTL_GC_THRESHOLD_RATIO    = 2,
  FTL_BAD_BLOCK_THRESHOLD   = 3,
  FTL_FILLING_MODE          = 4,
  FTL_FILL_RATIO            = 5,
  FTL_INVALID_PAGE_RATIO    = 6,
  FTL_GC_MODE               = 7,
  FTL_GC_RECLAIM_BLOCK      = 8,
  FTL_GC_RECLAIM_THRESHOLD  = 9,
  FTL_GC_EVICT_POLICY       = 10,
  FTL_GC_D_CHOICE_PARAM     = 11,
  FTL_USE_RANDOM_IO_TWEAK   = 12,

  /* N+K Mapping configuration*/
  FTL_NKMAP_N               = 13,
  FTL_NKMAP_K               = 14,
} FTL_CONFIG;

typedef enum {
  PAGE_MAPPING = 0,
} MAPPING;

typedef enum {
  GC_MODE_0 = 0,  // Reclaim fixed number of blocks
  GC_MODE_1 = 1,  // Reclaim blocks until threshold
} GC_MODE;

typedef enum {
  FILLING_MODE_0 = 0,
  FILLING_MODE_1 = 1,
  FILLING_MODE_2 = 2,
} FILLING_MODE;

typedef enum {
  POLICY_GREEDY        = 0,  // Select the block with the least valid pages
  POLICY_COST_BENEFIT  = 1,
  POLICY_RANDOM        = 2,  // Select the block randomly
  POLICY_DCHOICE       = 3,
} EVICT_POLICY;

typedef enum {
    FIRMWARE_FTL_PARAMS = 0, 
    FIRMWARE_ICL_PARAMS = 1,
    FIRMWARE_QUEUE_TOP = 2,
    FIRMWARE_TICK = 3,
    FIRMWARE_CYCLE = 4,
    FIRMWARE_CORE_ID = 5,
    FIRMWARE_QUEUE_SIZE = 6,
    FIRMWARE_CSD_QUEUE_TOP = 7,
    FIRMWARE_CSD_QUEUE_SIZE = 8,
    FIRMWARE_CSD_JOB_LOADED = 9,
    FIRMWARE_CSD_JOB_FINISHED = 10,
} DATA_REQ;

typedef enum {
  FIRMWARE_REQ_DONE = 0,
  FIRMWARE_REQ_FAILED = 1,
  ICL_STAT_LOC = 2,
  FTL_STAT_LOC = 3,
  ICL_LOW = 4, 
  ICL_HIGH = 5, 
  CORE_SETUP_COMPLETED = 6,
  START_CSD_JOB = 7,
  RESUME_CSD_JOB = 8,
  CSD_JOB_FINISHED = 9,
  CLEANED_CSD_JOB = 10, 
  ACQUIRE_MUTEX_LOCK = 11,
  ACQUIRED_MUTEX_LOCK = 12,
} DATA_RESP; 

typedef enum {
  ICL_REQ_READ = 0,
  ICL_REQ_WRITE = 1,
  ICL_REQ_TRIM = 2,
  ICL_REQ_FORMAT = 3,
  ICL_REQ_FLUSH = 4,
  ICL_REQ_EMPTY = 5,
} ICL_REQ_TYPE;

#endif