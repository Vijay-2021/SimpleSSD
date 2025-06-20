#include "ssd_firmware.hh"
#include "embext.h"
#include "utils.h"
#include "def.hh"
#include "cs_instructions.h"

int main(int argc, char** argv) {
    firmware_params params;
    read_buffer((uint64_t)&params, FIRMWARE_PARAMS);
    Firmware firmware(params); // just initialize it for now
    FTL::Request req;
    uint64_t curr_tick = 0;
    stop_sim();
    while(1) {
        read_buffer((uint64_t)&req, FIRMWARE_QUEUE);
        read_buffer((uint64_t)&curr_tick, FIRMWARE_TICK);
        if (req.reqType == FTL_REQ_READ) {
            firmware.read(req, curr_tick);
            stop_sim();
            req.reqType == FTL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == FTL_REQ_WRITE) {
            firmware.write(req, curr_tick);
            stop_sim();
            req.reqType == FTL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == FTL_REQ_TRIM) {
            firmware.trim(req, curr_tick);
            stop_sim();
            req.reqType == FTL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == FTL_REQ_FORMAT) {
            // firmware.format(req); format not supported yet
            stop_sim();
            req.reqType == FTL_REQ_EMPTY; // reset the request type
        } else if (req.reqType == FTL_REQ_EMPTY) {
            // do nothing, just wait for the next request
        } else {
            panic("Unknown request type: %d\n", req.reqType);
        }
    }
    return 0;
}