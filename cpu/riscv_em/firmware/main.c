#include <stdint.h>
#include <stddef.h>
#include "cs_instructions.h"
#include "ext2.h"
#include "utils.h"

static char* test = "Hello World from a simple RV32I ISA emulator v2!\n";

int main(void)
{
	print("starting probe\n");
	ext2_priv_data priv;
	ext2_probe(&priv);
	printf("first bgd: %d\n", priv.first_bgd);
	ext2_mount(&priv);
	ext2_touch("/home/csd/hello_csd.txt", &priv);
	printf("tried to make file\n");
	print("completed new firmwares\n");
    return 0;
}
