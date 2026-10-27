// cpucheck: how much of each CPU do we actually get? Spins on one CPU for 300 ms
// and reports the CPU time received as a share of wall time. 100% means the core
// was ours; less means something else was running on it.
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(clockid_t c) { struct timespec t; clock_gettime(c, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char** argv) {
    int lo = argc > 1 ? atoi(argv[1]) : 0, hi = argc > 2 ? atoi(argv[2]) : 15;
    for (int cpu = lo; cpu <= hi; cpu++) {
        cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s);
        sched_setaffinity(0, sizeof s, &s);
        double w0 = now(CLOCK_MONOTONIC), c0 = now(CLOCK_PROCESS_CPUTIME_ID);
        volatile unsigned long x = 0;
        while (now(CLOCK_MONOTONIC) - w0 < 0.3) x++;
        double share = (now(CLOCK_PROCESS_CPUTIME_ID) - c0) / (now(CLOCK_MONOTONIC) - w0) * 100;
        printf("cpu%02d %5.1f%%\n", cpu, share);
    }
    return 0;
}
