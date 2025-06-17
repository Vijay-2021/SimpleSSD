#include "ssd_firmware.hh"
#include "embext.h"
#include "utils.h"

int main(int argc, char** argv) {
    Firmware firmware; // just initialize it for now
    firmware.print_status();
    while(1) {
        // do nothing
        if (firmware.get_queue.size() > 0) {
            firmware.process_req();
        } else {
            stop_sim(); // relinquish control to the simulator
        }
    }
    return 0;
}