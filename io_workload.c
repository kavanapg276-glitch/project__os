#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>

int main() {
    printf("[I/O] Workload Started (PID: %d)\n", getpid());
    fflush(stdout);

    long long iterations = 0;

    while (1) {
        FILE *f = fopen("/tmp/iotest.txt", "w");
        if (f) {
            for (int i = 0; i < 1000; i++) {
                fprintf(f, "Testing IO workload line %d\n", i);
            }
            fclose(f);
        }

        struct timespec req = {0};
        req.tv_sec = 0;
        req.tv_nsec = 50000000L; // 50ms
        nanosleep(&req, NULL);

        iterations++;

        if (iterations % 10 == 0) {
            printf("[I/O] Loop iteration %lld\n", iterations);
            fflush(stdout);
        }
    }

    return 0;
}
