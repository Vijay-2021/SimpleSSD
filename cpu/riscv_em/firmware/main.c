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
	printf("starting firmarwe\n");
	//ext2_mount(0, block_get_volume_size(), 0, &context);
	struct ext2context *context;
	ext2_mount(63, block_get_volume_size(), 0, &context);
	void *fe = ext2_open(context, "/home/test_csd.txt", O_WRONLY | O_CREAT | O_APPEND , 0777);
    if(fe == NULL) {
        printf("open fail\n");
        return 0;
    } else {
        printf("open ok\n");
    }
    int result = ext2_write(fe, "Hello world\r\n", 13);
    printf("result %d\n", result);
    ext2_close(fe);
	ext2_umount(context);
	print("finishing firmware\n");
    return 0;
}
