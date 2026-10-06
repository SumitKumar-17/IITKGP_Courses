// crash_test.cpp: kill a process in the middle of committing and see what recovery finds.
//
// A child process opens the log and commits 8-key transactions as fast as it can; each
// transaction writes the same version number to all 8 keys, plus words derived from it.
// After every commit that returns it publishes the version in shared memory ("acked").
// The parent sends SIGKILL at a random moment, reopens the log in a fresh store and checks:
//
//   * the 8 keys are all absent or all present, with one version and consistent contents
//     (a transaction is recovered whole or not at all);
//   * Write and Sync: the recovered version is at least the last acknowledged one, so no
//     acknowledged commit is lost. (SIGKILL cannot take back what write() already gave the
//     kernel, which is why Write survives a process crash; Sync also covers power loss,
//     which this test cannot simulate.)
//   * Async: the recovered version may trail the acknowledged one by the flush interval's
//     worth of commits; the loss is measured and reported;
//   * never ahead of what the child can have been committing (acked + 2).
//
// The same log file carries on across iterations, so every recovery is also followed by
// more appends.
//
// usage: crash_test [iterations per level]
#include "kvstore.h"

#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>

struct Row { uint64_t version, a, b, c; };
static Row make_row(long key, uint64_t v) {
    return {v, v * 3 + 1, v ^ 0x9e3779b97f4a7c15ULL, static_cast<uint64_t>(key) * 1000003ULL + v * 7};
}
static bool row_ok(long key, const Row& r) {
    Row e = make_row(key, r.version);
    return r.a == e.a && r.b == e.b && r.c == e.c;
}

struct Shared { std::atomic<uint64_t> acked; };

static const int kKeys = 8;

// Returns the recovered version, 0 for empty, or -1 if the keys are inconsistent.
static long recovered_version(const KVStore& s) {
    int present = 0;
    long version = -1;
    for (long k = 0; k < kKeys; k++) {
        Row r;
        if (!s.get(k, &r, sizeof r)) continue;
        present++;
        if (!row_ok(k, r)) return -1;
        if (version >= 0 && static_cast<long>(r.version) != version) return -1;
        version = static_cast<long>(r.version);
    }
    if (present == 0) return 0;
    return present == kKeys ? version : -1;
}

static void child_main(const std::string& path, const WalOptions& o, Shared* sh) {
    KVStore s;
    std::string err;
    if (!s.open_wal(path, o, nullptr, &err)) { std::fprintf(stderr, "child: %s\n", err.c_str()); _exit(3); }
    long v0 = recovered_version(s);
    if (v0 < 0) _exit(4);
    for (uint64_t v = static_cast<uint64_t>(v0) + 1;; v++) {
        Transaction t(s);
        for (long k = 0; k < kKeys; k++) { Row r = make_row(k, v); t.put(k, &r, sizeof r); }
        if (t.commit()) sh->acked.store(v, std::memory_order_release);
    }
}

int main(int argc, char** argv) {
    int iterations = argc > 1 ? std::atoi(argv[1]) : 30;
    char tmpl[] = "./crashtest.XXXXXX";
    if (!mkdtemp(tmpl)) { std::perror("mkdtemp"); return 2; }
    std::string dir = tmpl;

    Shared* sh = static_cast<Shared*>(mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE,
                                           MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    int failures = 0;
    struct Level { Durability d; const char* name; } levels[] = {
        {Durability::Write, "write"}, {Durability::Sync, "sync"}, {Durability::Async, "async"}};

    for (const Level& L : levels) {
        std::string path = dir + "/" + L.name + ".wal";
        WalOptions o;
        o.durability = L.d;
        o.prealloc_bytes = 4 << 20;
        std::mt19937_64 rng(1234);
        long prior = 0, worst_loss = 0, lost_runs = 0, torn_tails = 0, empty_runs = 0;
        double run_seconds = 0;
        uint64_t last_acked = 0;
        for (int it = 0; it < iterations; it++) {
            sh->acked.store(static_cast<uint64_t>(prior));
            pid_t pid = fork();
            if (pid == 0) { child_main(path, o, sh); _exit(0); }
            auto run_us = 15000 + rng() % 90000;
            std::this_thread::sleep_for(std::chrono::microseconds(run_us));
            run_seconds += run_us / 1e6;
            kill(pid, SIGKILL);
            int status = 0;
            waitpid(pid, &status, 0);
            uint64_t acked = sh->acked.load(std::memory_order_acquire);

            KVStore r;
            WalReplayInfo info;
            std::string err;
            if (!r.open_wal(path, o, &info, &err)) {
                std::printf("FAIL %s iteration %d: recovery failed: %s\n", L.name, it, err.c_str());
                failures++;
                break;
            }
            long v = recovered_version(r);
            if (v < 0) {
                std::printf("FAIL %s iteration %d: recovered a partial or inconsistent transaction\n", L.name, it);
                failures++;
                break;
            }
            if (!info.clean_end) torn_tails++;
            if (v == 0) empty_runs++;
            if (v > static_cast<long>(acked) + 2) {
                std::printf("FAIL %s iteration %d: recovered version %ld is ahead of acked %lu\n", L.name, it, v, (unsigned long)acked);
                failures++;
            }
            if (L.d != Durability::Async && v < static_cast<long>(acked)) {
                std::printf("FAIL %s iteration %d: acknowledged commit %lu lost (recovered %ld)\n", L.name, it, (unsigned long)acked, v);
                failures++;
            }
            if (v < static_cast<long>(acked)) {
                lost_runs++;
                worst_loss = std::max(worst_loss, static_cast<long>(acked) - v);
            }
            if (v < prior) {
                std::printf("FAIL %s iteration %d: version went backwards (%ld after %ld)\n", L.name, it, v, prior);
                failures++;
            }
            prior = v;
            last_acked = acked;
        }
        double rate = prior / run_seconds;
        std::printf("%-5s: %d kills, %ld commits (about %.0f a second), torn tails found %ld, runs that lost acknowledged commits %ld "
                    "(worst %ld commits, about %.1f ms of work)\n",
                    L.name, iterations, prior, rate, torn_tails, lost_runs, worst_loss, rate > 0 ? worst_loss / rate * 1000 : 0.0);
        (void)last_acked; (void)empty_runs;
    }
    std::string cmd = "rm -rf " + dir;
    if (std::system(cmd.c_str()) != 0) { /* harmless */ }
    std::printf(failures ? "crash_test: %d failure(s)\n" : "crash_test: all passed\n", failures);
    return failures != 0;
}
