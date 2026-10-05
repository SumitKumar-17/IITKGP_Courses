// stress_test.cpp: readers hammer get() while the writer hammers put()/erase().
//
// Every row is 32 bytes: a version number plus three words derived from the
// version and the key. A reader recomputes them, so a row that mixes bytes
// from two writes (or from freed and reused memory) fails the check. Each
// reader also checks that the version it sees for a key never goes backwards.
#include "kvstore.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

struct Row { uint64_t version, a, b, c; };

static Row make_row(long key, uint64_t version) {
    return {version, version * 3 + 1, version ^ 0x9e3779b97f4a7c15ULL, (uint64_t)key * 1000003ULL + version * 7};
}

static bool row_ok(long key, const Row& r) {
    Row e = make_row(key, r.version);
    return r.a == e.a && r.b == e.b && r.c == e.c;
}

int main(int argc, char** argv) {
    int readers = argc > 1 ? std::atoi(argv[1]) : 6;
    int seconds = argc > 2 ? std::atoi(argv[2]) : 5;
    int batch = argc > 3 ? std::atoi(argv[3]) : 64;
    const long kKeys = 5000;

    KVStore store;
    store.set_reclaim_batch(batch);
    for (long k = 0; k < kKeys; k++) { Row r = make_row(k, 1); store.put(k, &r, sizeof r); }

    std::atomic<bool> stop{false};
    std::atomic<long> torn{0}, regressed{0};
    std::atomic<long> reads{0}, hits{0};

    std::vector<std::thread> pool;
    for (int t = 0; t < readers; t++) {
        pool.emplace_back([&, t] {
            std::mt19937_64 rng(1000 + t);
            std::vector<uint64_t> last(kKeys, 0);
            long n = 0, h = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                long k = rng() % kKeys;
                Row r;
                std::memset(&r, 0xAB, sizeof r);
                n++;
                if (!store.get(k, &r, sizeof r)) continue;
                h++;
                if (!row_ok(k, r)) torn++;
                if (r.version < last[k]) regressed++;
                last[k] = r.version;
            }
            reads += n; hits += h;
        });
    }

    std::mt19937_64 rng(7);
    uint64_t version = 1;
    long writes = 0, erases = 0;
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < end) {
        long k = rng() % kKeys;
        if (rng() % 16 == 0) {
            store.erase(k);
            erases++;
        } else {
            Row r = make_row(k, ++version);
            store.put(k, &r, sizeof r);
            writes++;
        }
    }
    stop = true;
    for (auto& th : pool) th.join();

    auto st = store.reclaim_stats();
    std::printf("batch=%d readers=%d  writes=%ld erases=%ld  reads=%ld (hits %ld)\n",
                batch, readers, writes, erases, reads.load(), hits.load());
    std::printf("torn rows: %ld   version regressions: %ld\n", torn.load(), regressed.load());
    std::printf("retired=%lu freed=%lu backlog_peak=%lu max_pass_freed=%lu max_pass_us=%.1f\n",
                (unsigned long)st.retired, (unsigned long)st.freed, (unsigned long)st.backlog_peak,
                (unsigned long)st.max_pass_freed, st.max_pass_ns / 1000.0);

    bool ok = torn == 0 && regressed == 0 && (batch <= 0 || st.max_pass_freed <= (uint64_t)batch);
    std::printf(ok ? "stress_test: PASS\n" : "stress_test: FAIL\n");
    return ok ? 0 : 1;
}
