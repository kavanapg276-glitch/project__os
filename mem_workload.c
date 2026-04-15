#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

int main(int argc, char *argv[]) {
    int target_mb = 100;
    if (argc > 1) {
        target_mb = atoi(argv[1]);
    }

    printf("[MEM] Allocating %d MB at start\n", target_mb);
    fflush(stdout);

    int allocated_mb = 0;
    while (allocated_mb < target_mb) {
        void *ptr = malloc(1024 * 1024);
        if (ptr != NULL) {
            memset(ptr, 1, 1024 * 1024);
            allocated_mb++;
            printf("[MEM] Allocated %d MB total\n", allocated_mb);
            fflush(stdout);
        } else {
            printf("[MEM] Allocation failed at %d MB\n", allocated_mb);
            fflush(stdout);
            break;
        }
        sleep(1);
    }

    printf("[MEM] Done allocating. Holding memory...\n");
    fflush(stdout);

    while (1) {
        sleep(1);
    }

    return 0;
}
