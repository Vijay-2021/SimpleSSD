#include "cs_instructions.h"
#include "block.h"

int block_read(blockno_t block, void *buf) {
    lread((uint64_t)buf, block, 1);
    return 0;
}


int block_write(blockno_t block, void *buf) {
    lwrite((uint64_t)buf, block, 1);
    return 0;
}


blockno_t block_get_volume_size() {
    return 512*1024; // random number but whatever
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