// replay_bench.cpp: how fast does a restart rebuild the store from the log?
//
// Writes N records (32-byte values, one put per record, Write level so the writing itself is
// quick), closes, and times a reopen. Also times the same N puts into a store with no log, so
// the cost of replay can be read against the cost of building the table directly.
//
// usage: replay_bench <records> <path>
#include "kvstore.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

static double secs(std::chrono::steady_clock::time_point a) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
}

int main(int argc, char** argv) {
    long n = argc > 1 ? std::atol(argv[1]) : 1000000;
    std::string path = argc > 2 ? argv[2] : "replay.wal";
    std::remove(path.c_str());
    char v[32] = {};

    double t_plain;
    {
        KVStore s;
        auto t0 = std::chrono::steady_clock::now();
        for (long i = 0; i < n; i++) { v[0] = (char)i; s.put(i * 7919, v, sizeof v); }
        t_plain = secs(t0);
    }
    double t_log;
    {
        KVStore s;
        WalOptions o;
        o.durability = Durability::Write;
        std::string err;
        if (!s.open_wal(path, o, nullptr, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        auto t0 = std::chrono::steady_clock::now();
        for (long i = 0; i < n; i++) { v[0] = (char)i; s.put(i * 7919, v, sizeof v); }
        t_log = secs(t0);
    }
    struct stat st;
    stat(path.c_str(), &st);
    double mb = st.st_size / 1048576.0;

    KVStore r;
    WalOptions o;
    o.durability = Durability::Write;
    WalReplayInfo info;
    std::string err;
    auto t0 = std::chrono::steady_clock::now();
    bool ok = r.open_wal(path, o, &info, &err);
    double t_replay = secs(t0);
    if (!ok || info.records != (uint64_t)n || r.size() != (size_t)n) { std::fprintf(stderr, "replay mismatch\n"); return 1; }
    std::printf("records=%ld log_mb=%.1f bytes_per_record=%.1f build_no_log_s=%.3f build_with_log_s=%.3f replay_s=%.3f "
                "replay_records_per_s=%.0f replay_mb_per_s=%.0f\n",
                n, mb, mb * 1048576.0 / n, t_plain, t_log, t_replay, n / t_replay, mb / t_replay);
    std::remove(path.c_str());
    return 0;
}
