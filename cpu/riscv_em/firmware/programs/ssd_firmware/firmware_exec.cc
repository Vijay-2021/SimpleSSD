#include "ssd_firmware.hh"
#include "embext.h"
#include "utils.h"
int main(int argc, char** argv) {
    Firmware firmware; // just initialize it for now
    firmware.print_status();
    while(1) {
        // do nothing
    }
    return 0;
}