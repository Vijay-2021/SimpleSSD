#include <capstone/capstone.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

void print_usage(const char *progname) {
    printf("Usage: %s -f <filename> -p<program size>\n", progname);
    printf("  -f <filename> Input binary file\n");
    printf("  -p <program size> Size of binary in bytes\n");
    printf("  -h Show this help message\n");
}

void disassemble_riscv(const uint8_t *code, size_t code_size) {
    csh handle;
    cs_insn insn;
    cs_insn *insn_ptr = &insn;

    if (cs_open(CS_ARCH_RISCV, CS_MODE_RISCV64, &handle) != CS_ERR_OK) {
        perror("failed to open capstone");
        return;
    }

    cs_option(handle, CS_OPT_DETAIL, CS_OPT_OFF);  // Optional: turn off extra detail

    const uint8_t *code_ptr = code;
    size_t size_left = code_size;
    uint64_t address = 0x1000;

    while (cs_disasm_iter(handle, &code_ptr, &size_left, &address, &insn_ptr)) {
        printf("0x%"PRIx64":\t%s\t%s\n", insn.address, insn.mnemonic, insn.op_str);
        // Handle each instruction individually here
    }

    cs_close(&handle);
}

int main(int argc, char** argv) {
    const char *filename = NULL;
    const char *program_size = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "f:p")) != -1) {
        switch (opt) {
            case 'f':
                filename = optarg;
                break;
            case 'p':
                program_size = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (!filename || !program_size) {
        fprintf(stderr, "Missing required arguments.\n\n");
        print_usage(argv[0]);
        return 1;
    }
    int code_size = atoi(program_size);
    disassemble_riscv(filename, code_size);
    return 0;
}