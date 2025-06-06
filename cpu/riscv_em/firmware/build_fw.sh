#/bin/bash

set -e

riscv32-unknown-elf-gcc -march=rv32imafd -mabi=ilp32d -Wl,-Bstatic,-T,sections.lds,--strip-debug -ffreestanding -nostdlib -Ifs -o hello_world_fw.elf start.s main.c -Lfs/build -lmylib
riscv32-unknown-elf-objcopy -O binary hello_world_fw.elf hello_world_fw.bin

# print text section
#riscv32-none-elf-objdump -d hx8kdemo_fw.elf | awk '{print "0x"$2","}'

# dump all
#riscv32-none-elf-objdump -d hx8kdemo_fw.elf 

