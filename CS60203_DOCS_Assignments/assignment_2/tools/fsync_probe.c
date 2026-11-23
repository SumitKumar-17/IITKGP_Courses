// fsync_probe: average fdatasync latency, in microseconds, of small writes to a preallocated file
// in the given directory. Run before a log benchmark to see what state the disk is in.
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }

int main(int argc, char** argv) {
    char path[512];
    snprintf(path, sizeof path, "%s/fsync_probe.tmp", argc > 1 ? argv[1] : ".");
    int n = argc > 2 ? atoi(argv[2]) : 200;
    int fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0 || posix_fallocate(fd, 0, 8 << 20) != 0) return 1;
    char buf[128];
    memset(buf, 7, sizeof buf);
    double total = 0;
    for (int i = 0; i < n; i++) {
        if (pwrite(fd, buf, sizeof buf, (off_t)i * 128) != (ssize_t)sizeof buf) return 1;
        double a = now_us();
        fdatasync(fd);
        total += now_us() - a;
    }
    close(fd);
    unlink(path);
    printf("%.0f\n", total / n);
    return 0;
}
