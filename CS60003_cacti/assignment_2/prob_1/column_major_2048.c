#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define N 2048

static double A[N][N];

int main() {
    srand(42);
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            A[i][j] = (double)rand() / RAND_MAX;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    double sum = 0.0;
    /* Column-major: inner loop over rows -> strided memory access (stride = N*8 bytes) */
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++)
            sum += A[i][j];

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Column-major traversal time: %f seconds\n", elapsed);
    printf("Ignore: %f\n", sum);
    return 0;
}
