#include <stdint.h>
#include <stddef.h>
#include "cs_instructions.h"
#include "embext.h"
#include "utils.h"
#include "string.h"
#include "memory.h"
#include "block.h"
static char* test = "Hello World from a simple RV32I ISA emulator v2!\n";

int main(void)
{
	print("starting firmarwe\n");
	//ext2_mount(0, block_get_volume_size(), 0, &context);
	struct ext2context *context;
	ext2_mount(63, block_get_volume_size(), 0, &context);
	void *fe = ext2_open(context, "/home/test_csd.txt", O_WRONLY | O_APPEND | O_CREAT, 0777);
    if(fe == NULL) {
        printf("open fail\n");
    }
  
    int result = ext2_write(fe, "Hello world\r\n", 13);
    if(result != 13) {
        printf("write fail\n");
    }
    ext2_close(fe);
    printf("    pass\n");
	ext2_umount(context);
	print("finishing firmware\n");
    return 0;
}
