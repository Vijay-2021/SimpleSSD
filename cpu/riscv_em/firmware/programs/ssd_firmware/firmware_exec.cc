#include "ssd_firmware.hh"
#include "embext.h"
#include "utils.h"
#include "def.hh"
#include "cs_instructions.h"

int main(int argc, char** argv) {
    firmware_params params;
    read_buffer((uint64_t)&ssd_params, FIRMWARE_PARAMS);
    Firmware firmware(params); // just initialize it for now
    FTL::Request req;
    stop_sim();
    while(1) {
        read_buffer((uint64_t)&req, FIRMWARE_QUEUE);
        if (req.type == FTL_REQ_READ) {
            firmware.read(req);
            stop_sim();
            req.type == FTL_REQ_EMPTY; // reset the request type
        } else if (req.type == FTL_REQ_WRITE) {
            firmware.write(req);
            stop_sim();
            req.type == FTL_REQ_EMPTY; // reset the request type
        } else if (req.type == FTL_REQ_TRIM) {
            firmware.trim(req);
            stop_sim();
            req.type == FTL_REQ_EMPTY; // reset the request type
        } else if (req.type == FTL_REQ_FORMAT) {
            firmware.format(req);
            stop_sim();
            req.type == FTL_REQ_EMPTY; // reset the request type
        } else if (req.type == FTL_REQ_EMPTY) {
            // do nothing, just wait for the next request
        } else {
            panic("Unknown request type: %d\n", req.type);
        }
    }
    return 0;
}