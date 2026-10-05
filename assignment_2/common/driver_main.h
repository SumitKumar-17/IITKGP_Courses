// driver_main.h: shared main() body for every benchmark driver.
//
// workload_run() loads the table with the writer and only then starts the
// readers, so it has no writer running during the read phase. The
// background writer here waits for the load to finish (by polling for the
// last trade id) and then keeps overwriting existing trades at a fixed
// rate until the readers are done. Only one thread writes at a time.
#pragma once
#include "kvstore.h"
#include "workload_gen.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>

struct DriverOptions {
    int readers = 8;
    int seconds = 5;
    long writer_rate = 50000;   // puts per second, 0 turns the writer off
};

inline DriverOptions parse_options(int argc, char** argv) {
    DriverOptions o;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string k = argv[i];
        long v = std::atol(argv[i + 1]);
        if (k == "--readers") o.readers = (int)v;
        else if (k == "--seconds") o.seconds = (int)v;
        else if (k == "--writer-rate") o.writer_rate = v;
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); std::exit(2); }
    }
    return o;
}

inline void background_writer(KVStore& store, const WorkloadConfig& cfg, long rate,
                              const std::atomic<bool>& stop) {
    const long last_id = (long)(cfg.m - 1) * 100000 + (cfg.n - 1);
    char probe[32];
    while (!stop.load() && !store.get(last_id, probe, sizeof probe))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    std::mt19937_64 rng(12345);
    char payload[32];
    std::memset(payload, 0, sizeof payload);
    const auto gap = std::chrono::nanoseconds(1000000000L / rate);
    auto next = std::chrono::steady_clock::now();
    long counter = 0;
    while (!stop.load(std::memory_order_relaxed)) {
        long i = rng() % cfg.m, j = rng() % cfg.n;
        std::memcpy(payload, &counter, sizeof counter);
        counter++;
        store.put(i * 100000 + j, payload, sizeof payload);
        next += gap;
        while (std::chrono::steady_clock::now() < next && !stop.load(std::memory_order_relaxed)) {}
    }
}

inline int driver_main(int argc, char** argv, const char* label, bool allow_writer) {
    DriverOptions o = parse_options(argc, argv);
    if (!allow_writer) o.writer_rate = 0;

    KVStore trades;
    WorkloadConfig cfg;
    cfg.num_readers = o.readers;
    cfg.duration_sec = o.seconds;
    WorkloadResults res;

    std::atomic<bool> stop{false};
    std::thread writer;
    if (o.writer_rate > 0)
        writer = std::thread(background_writer, std::ref(trades), std::cref(cfg), o.writer_rate, std::cref(stop));

    workload_run(trades, nullptr, cfg, res);
    stop = true;
    if (writer.joinable()) writer.join();

    std::string full = std::string(label) + "/r" + std::to_string(o.readers);
    print_results(full, res);
    return 0;
}
