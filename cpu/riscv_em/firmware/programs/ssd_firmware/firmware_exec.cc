#include "ssd_firmware.hh"
#include "embext.h"
#include "utils.h"
#include "def.hh"
#include "cs_instructions.h"

int main(int argc, char** argv) {
    firmware_params params;
    read_buffer((uint64_t)&params, FIRMWARE_PARAMS);
    printf("read params\n");
    Firmware firmware(params); // just initialize it for now
    FTL::Request req;
    uint64_t curr_tick = 0;
    printf("all of this is fine!\n");
    stop_sim();
    while(1) {
        printf("executing loop\n");
        read_buffer((uint64_t)&req, FIRMWARE_QUEUE_TOP);
        void* buffer = malloc(req.ioFlag.allocSize);
        read_buffer((uint64_t)buffer, FIRMWARE_BITSET_BUFFER);
        req.ioFlag.data = (uint8_t*)buffer;
        read_buffer((uint64_t)&curr_tick, FIRMWARE_TICK);
        printf("reads finished with data %p and alloc size %u and data size %u\n", req.ioFlag.data, req.ioFlag.allocSize, req.ioFlag.dataSize);
        if (req.reqType == FTL_REQ_READ) {
            printf("calling read\n");
            firmware.read(req, curr_tick);
            stop_sim();
            req.reqType == FTL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == FTL_REQ_WRITE) {
            printf("calling write\n");
            firmware.write(req, curr_tick);
            stop_sim();
            req.reqType == FTL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == FTL_REQ_TRIM) {
            printf("calling trim\n");
            firmware.trim(req, curr_tick);
            stop_sim();
            req.reqType == FTL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == FTL_REQ_FORMAT) {
            printf("calling format\n");
            // firmware.format(req); format not supported yet
            stop_sim();
            req.reqType == FTL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == FTL_REQ_EMPTY) {
            // do nothing, just wait for the next request
        } else {
            free(buffer);
            panic("Unknown request type: %d\n", req.reqType);
        }
        free(buffer);
    }
    return 0;
}