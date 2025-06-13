#include "utils.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("please provide two ascii numbers \n"); 
        return 0;
    }
    printf("Hello World from a launched program!\n");
    printf("Adding %d and %d to get %d", atoi(argv[1]), atoi(argv[2]));
    return 0; 
}