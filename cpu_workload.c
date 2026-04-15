#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

int main() {
    printf("[CPU] Workload Started (PID: %d)\n", getpid());
    fflush(stdout);

    double pi = 0.0;
    long long iterations = 0;
    double sign = 1.0;

    while (1) {
        pi += sign / (2.0 * iterations + 1.0);
        sign = -sign;
        iterations++;

        if (iterations % 50000000 == 0) {
            printf("[CPU] Running... pi estimation = %.10f\n", pi * 4.0);
            fflush(stdout);
        }
    }

    return 0;
}
