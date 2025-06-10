#/bin/bash

set -e
cd utils
make
cd ..
riscv64-unknown-elf-gcc -march=rv64imafd -mabi=lp64d -Wl,-Bstatic,-T,sections.lds,--strip-debug -ffreestanding -nostdlib -Iutils -mcmodel=medany start.s main.c -Lutils/build -lmylib -o hello_world_fw.elf
riscv64-unknown-elf-objcopy -O binary hello_world_fw.elf hello_world_fw.bin

# print text section
#riscv32-none-elf-objdump -d hx8kdemo_fw.elf | awk '{print "0x"$2","}'

# dump all
#riscv32-none-elf-objdump -d hx8kdemo_fw.elf 

