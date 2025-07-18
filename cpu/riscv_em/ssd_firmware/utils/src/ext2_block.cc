#include "cs_instructions.h"
#include "ext2_block.hh"
#include "icl.hh"
#include "def.hh"
#include "utils.h"

static ICL::ICL* internal_icl = nullptr;
static uint32_t page_size = 16384; // 16KB
static uint32_t lba_size = 512; // 512B
static uint64_t global_req_id = 0;

void set_ext2_icl(ICL::ICL* icl) {
    internal_icl = icl;
}

int block_read(blockno_t block, void *buf) {
    printf("trying to read flash for block %u\n", block);
    ICL::Request req;
    LPNRange lpnRange;
    uint32_t page_per_lba = page_size / lba_size;
    lpnRange.slpn = block / page_per_lba;
    lpnRange.nlp = (1 + page_per_lba - 1) / page_per_lba;
    req.range = lpnRange;
    req.offset = block % (page_size / lba_size);
    req.length = 1;
    req.reqID = global_req_id++;
    req.reqSubID = 0;
    internal_icl->read(req);
    lread((uint64_t)buf, block, 1);
    printf("read flash successfully\n");
    return 0;
}


int block_write(blockno_t block, void *buf) {
    printf("trying to write flash for block %u\n", block);
    ICL::Request req;
    LPNRange lpnRange;
    uint32_t page_per_lba = page_size / lba_size;
    lpnRange.slpn = block / page_per_lba;
    lpnRange.nlp = (1 + page_per_lba - 1) / page_per_lba;
    req.range = lpnRange;
    req.offset = block % (page_size / lba_size);
    req.length = 1;
    req.reqID = global_req_id++;
    req.reqSubID = 0;
    internal_icl->write(req);
    lwrite((uint64_t)buf, block, 1);
    printf("write flash successfully\n");
    return 0;
}


blockno_t block_get_volume_size() {
    return 32*1024*1024; // random number but whatever
}

int block_get_block_size() {
    return BLOCK_SIZE;
}


int block_get_device_read_only() {
    return 0;
}


int block_get_error() {
    return 0;
}