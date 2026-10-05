// bench.cpp: the Part D experiments the provided generator cannot report.
//
// The generator gives p50/p99/p999 but no maximum, so it cannot show the longest
// reader stall that D.2 asks for. This harness reads the store the same way the
// generator does (same trade layout, 80% reads on every 100th trade, 20% on a
// random one, every call timed) but keeps a histogram per reader instead of a
// vector of samples, so it also reports the exact maximum and how many calls
// took longer than 100 us, 1 ms and 10 ms.
//
//   --mode point        point gets with a background writer (D.2, and D.1 cross-check)
//   --mode join         index-nested-loop joins in the readers          (D.3)
//   --mode bigtxn       one transaction of N keys, timed, for N in --sizes (D.3 COW)
//
// Reclamation (RCU build only):
//   --reclaim bounded --batch N   the store's own bounded pass, N versions at most
//   --reclaim none                never free (batch 0)
//   --reclaim stw --trigger T     naive stop-the-world: when T versions are waiting,
//                                 park every reader at an operation boundary, free
//                                 the whole list at once, let them go
//   --hold-ms H --hold-period P   a thread that pins a snapshot for H ms every P ms,
//                                 like a join in progress, so a backlog builds
#include "kvstore.h"
#include "schema.h"
#ifdef HAVE_RCU
#include "join.h"
#endif

#include <malloc.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

using clk = std::chrono::steady_clock;

static inline uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(clk::now().time_since_epoch()).count();
}

// ------------------------------------------------------------------- histogram
// Log-linear: 64 buckets per power of two, so a percentile is within 1.6% of exact.
struct Hist {
    static constexpr int kBuckets = 2400;
    uint64_t c[kBuckets] = {};
    uint64_t n = 0, max_ns = 0, over_100us = 0, over_1ms = 0, over_10ms = 0;

    static int index(uint64_t ns) {
        if (ns < 64) return static_cast<int>(ns);
        int msb = 63 - __builtin_clzll(ns);
        int sub = static_cast<int>((ns >> (msb - 6)) & 63);
        int i = (msb - 5) * 64 + sub;
        return i < kBuckets ? i : kBuckets - 1;
    }
    static uint64_t lower(int idx) {
        if (idx < 64) return idx;
        int msb = idx / 64 + 5, sub = idx % 64;
        return (uint64_t(1) << msb) | (uint64_t(sub) << (msb - 6));
    }
    void add(uint64_t ns) {
        c[index(ns)]++;
        n++;
        if (ns > max_ns) max_ns = ns;
        if (ns > 100000) { over_100us++; if (ns > 1000000) { over_1ms++; if (ns > 10000000) over_10ms++; } }
    }
    void merge(const Hist& o) {
        for (int i = 0; i < kBuckets; i++) c[i] += o.c[i];
        n += o.n; over_100us += o.over_100us; over_1ms += o.over_1ms; over_10ms += o.over_10ms;
        if (o.max_ns > max_ns) max_ns = o.max_ns;
    }
    uint64_t pct(double p) const {
        if (!n) return 0;
        uint64_t want = static_cast<uint64_t>(p * n), seen = 0;
        for (int i = 0; i < kBuckets; i++) { seen += c[i]; if (seen > want) return lower(i); }
        return max_ns;
    }
    void dump(const char* path) const {
        FILE* f = std::fopen(path, "w");
        if (!f) return;
        std::fprintf(f, "bucket_lower_ns,count\n");
        for (int i = 0; i < kBuckets; i++) if (c[i]) std::fprintf(f, "%lu,%lu\n", (unsigned long)lower(i), (unsigned long)c[i]);
        std::fclose(f);
    }
};

// ---------------------------------------------------------------------- options
struct Opts {
    std::string mode = "point", label = "run", reclaim = "bounded", hist, sizes = "1,16,256,4096,65536,100000";
    int readers = 8, seconds = 5, batch = 64, txn = 0, hold_ms = 0, hold_period_ms = 100, outer = 1000;
    long writer_rate = 50000;   // puts (or transactions) per second; 0 off, -1 flat out
    long trigger = 4096;
    int reps = 5;
};

static Opts parse(int argc, char** argv) {
    Opts o;
    std::map<std::string, std::string> kv;
    for (int i = 1; i + 1 < argc; i += 2) kv[argv[i]] = argv[i + 1];
    auto geti = [&](const char* k, long d) { return kv.count(k) ? std::atol(kv[k].c_str()) : d; };
    auto gets = [&](const char* k, const std::string& d) { return kv.count(k) ? kv[k] : d; };
    o.mode = gets("--mode", o.mode);
    o.label = gets("--label", o.label);
    o.reclaim = gets("--reclaim", o.reclaim);
    o.hist = gets("--hist", "");
    o.sizes = gets("--sizes", o.sizes);
    o.readers = geti("--readers", o.readers);
    o.seconds = geti("--seconds", o.seconds);
    o.batch = geti("--batch", o.batch);
    o.txn = geti("--writer-txn", o.txn);
    o.hold_ms = geti("--hold-ms", o.hold_ms);
    o.hold_period_ms = geti("--hold-period", o.hold_period_ms);
    o.outer = geti("--outer", o.outer);
    o.writer_rate = geti("--writer-rate", o.writer_rate);
    o.trigger = geti("--trigger", o.trigger);
    o.reps = geti("--reps", o.reps);
    return o;
}

static double rss_peak_mb() {
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[256];
    double kb = 0;
    while (std::fgets(line, sizeof line, f)) if (std::sscanf(line, "VmHWM: %lf kB", &kb) == 1) break;
    std::fclose(f);
    return kb / 1024.0;
}

static double heap_mb() { return mallinfo2().uordblks / 1048576.0; }

// ----------------------------------------------------------------------- data
constexpr int kM = 1000, kN = 100;     // the generator's defaults: 100,000 trades

static Trade make_trade(int i, int j) {
    Trade t{};
    t.trade_id = static_cast<long>(i) * 100000 + j;
    std::snprintf(t.symbol, sizeof t.symbol, "SYM%03d", (i + j) % 50);
    t.price = 100.0 + (i % 97) * 0.37;
    t.ts = static_cast<long>(now_ns());
    return t;
}

static void load_trades(KVStore& s, std::vector<long>& hot) {
    long count = 0;
    for (int i = 0; i < kM; i++)
        for (int j = 0; j < kN; j++) {
            Trade t = make_trade(i, j);
            s.put(t.trade_id, &t, sizeof t);
            if (++count % 100 == 0) hot.push_back(t.trade_id);
        }
}

#ifdef HAVE_RCU
static void load_instruments(KVStore& s) {
    s.create_index(offsetof(Instrument, symbol), sizeof(Instrument::symbol));
    for (int k = 0; k < 50; k++) {
        Instrument ins{};
        std::snprintf(ins.symbol, sizeof ins.symbol, "SYM%03d", k);
        std::snprintf(ins.venue, sizeof ins.venue, "NSE");
        ins.tick_size = 1;
        s.put(k, &ins, sizeof ins);
    }
}
#endif

static long random_trade_id(std::mt19937_64& rng) {
    return static_cast<long>(rng() % kM) * 100000 + static_cast<long>(rng() % kN);
}

// ----------------------------------------------------------------------- shared
struct Shared {
    std::atomic<bool> stop{false};
    std::atomic<bool> frozen{false};
    std::atomic<int> parked{0};
};

struct WriterStats {
    uint64_t ops = 0, keys = 0;
    Hist lat;
    uint64_t stw_events = 0, stw_max_ns = 0, stw_total_ns = 0;
};

static void pause_hint() { __asm__ __volatile__("pause"); }

static void reader_point(const KVStore& store, const std::vector<long>& hot, Shared& sh, bool gate,
                         int tid, Hist& h) {
    std::mt19937_64 rng(1000 + tid);
    Trade t;
    while (!sh.stop.load(std::memory_order_relaxed)) {
        long id = (!hot.empty() && rng() % 10 < 8) ? hot[rng() % hot.size()] : random_trade_id(rng);
        uint64_t t0 = now_ns();
        if (gate && sh.frozen.load(std::memory_order_acquire)) {
            sh.parked.fetch_add(1);
            while (sh.frozen.load(std::memory_order_acquire)) pause_hint();
            sh.parked.fetch_sub(1);
        }
        store.get(id, &t, sizeof t);
        h.add(now_ns() - t0);
    }
}

#ifdef HAVE_RCU
static void reader_join(const KVStore& trades, const KVStore& instruments, size_t outer, Shared& sh,
                        Hist& h, uint64_t& rows) {
    unsigned long sink = 0;
    while (!sh.stop.load(std::memory_order_relaxed)) {
        uint64_t t0 = now_ns();
        JoinCounts c = index_nested_loop_join(trades, instruments, outer,
                                              [&](const Trade& t, const Instrument& i) { sink += t.ts + i.tick_size; });
        h.add(now_ns() - t0);
        rows += c.outer_rows;
    }
    if (sink == 42) std::puts("");
}
#endif

// One writer thread: puts, or transactions of K keys, at a fixed rate or flat out.
static void writer_loop(KVStore& store, const Opts& o, Shared& sh, WriterStats& ws) {
    std::mt19937_64 rng(77);
    Trade t = make_trade(0, 0);
    const auto gap = o.writer_rate > 0 ? std::chrono::nanoseconds(1000000000L / o.writer_rate)
                                       : std::chrono::nanoseconds(0);
    auto next = clk::now();
    [[maybe_unused]] uint64_t last_stw_retired = 0;
    while (!sh.stop.load(std::memory_order_relaxed)) {
        uint64_t t0 = now_ns();
        if (o.txn > 0) {
            Transaction tx(store);
            for (int k = 0; k < o.txn; k++) {
                t.trade_id = random_trade_id(rng);
                t.ts = static_cast<long>(ws.ops);
                tx.put(t.trade_id, &t, sizeof t);
            }
            tx.commit();
            ws.keys += o.txn;
        } else {
            t.trade_id = random_trade_id(rng);
            t.ts = static_cast<long>(ws.ops);
            store.put(t.trade_id, &t, sizeof t);
            ws.keys++;
        }
        ws.lat.add(now_ns() - t0);
        ws.ops++;

#ifdef HAVE_RCU
        if (o.reclaim == "stw") {
            // A freeze cannot free versions a snapshot still pins, so wait for a further
            // `trigger` versions after each one instead of freezing on every write.
            auto st = store.reclaim_stats();
            if (static_cast<long>(st.retired - st.freed) >= o.trigger &&
                static_cast<long>(st.retired - last_stw_retired) >= o.trigger) {
                last_stw_retired = st.retired;
                uint64_t s0 = now_ns();
                sh.frozen.store(true, std::memory_order_seq_cst);
                while (sh.parked.load(std::memory_order_acquire) < o.readers && !sh.stop.load()) pause_hint();
                store.reclaim_all();
                sh.frozen.store(false, std::memory_order_release);
                uint64_t d = now_ns() - s0;
                ws.stw_events++;
                ws.stw_total_ns += d;
                if (d > ws.stw_max_ns) ws.stw_max_ns = d;
            }
        }
#endif
        if (o.writer_rate > 0) {
            next += gap;
            while (clk::now() < next && !sh.stop.load(std::memory_order_relaxed)) pause_hint();
        }
    }
}

#ifdef HAVE_RCU
static void snapshot_holder(const KVStore& store, const Opts& o, Shared& sh, uint64_t& holds) {
    while (!sh.stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, o.hold_period_ms - o.hold_ms)));
        if (sh.stop.load()) break;
        KVStore::Snapshot s = store.acquire_snapshot();
        std::this_thread::sleep_for(std::chrono::milliseconds(o.hold_ms));
        holds++;
    }
}
#endif

// -------------------------------------------------------------------- run modes
static int run_workload(const Opts& o) {
    KVStore trades;
    std::vector<long> hot;
    load_trades(trades, hot);
#ifdef HAVE_RCU
    KVStore instruments;
    if (o.mode == "join") load_instruments(instruments);
    if (o.reclaim == "none" || o.reclaim == "stw") trades.set_reclaim_batch(0);
    else trades.set_reclaim_batch(o.batch);
    auto base = trades.reclaim_stats();
#else
    (void)hot;
#endif
    double heap_loaded = heap_mb();

    Shared sh;
    std::vector<Hist> hists(o.readers);
    std::vector<uint64_t> rows(o.readers, 0);
    std::vector<std::thread> pool;
    bool gate = o.reclaim == "stw";
    for (int i = 0; i < o.readers; i++) {
        if (o.mode == "join") {
#ifdef HAVE_RCU
            pool.emplace_back(reader_join, std::cref(trades), std::cref(instruments), (size_t)o.outer,
                              std::ref(sh), std::ref(hists[i]), std::ref(rows[i]));
#endif
        } else {
            pool.emplace_back(reader_point, std::cref(trades), std::cref(hot), std::ref(sh), gate, i, std::ref(hists[i]));
        }
    }
    WriterStats ws;
    std::thread writer;
    if (o.writer_rate != 0) writer = std::thread(writer_loop, std::ref(trades), std::cref(o), std::ref(sh), std::ref(ws));
    [[maybe_unused]] uint64_t holds = 0;
#ifdef HAVE_RCU
    std::thread holder;
    if (o.hold_ms > 0) holder = std::thread(snapshot_holder, std::cref(trades), std::cref(o), std::ref(sh), std::ref(holds));
#endif

    uint64_t t0 = now_ns();
    std::this_thread::sleep_for(std::chrono::seconds(o.seconds));
    double secs = (now_ns() - t0) / 1e9;
    sh.stop = true;
    for (auto& t : pool) t.join();
    if (writer.joinable()) writer.join();
#ifdef HAVE_RCU
    if (holder.joinable()) holder.join();
#endif

    Hist all;
    uint64_t total_rows = 0;
    for (int i = 0; i < o.readers; i++) { all.merge(hists[i]); total_rows += rows[i]; }
    if (!o.hist.empty()) all.dump(o.hist.c_str());

    std::printf("result mode=%s label=%s readers=%d secs=%.2f ops=%lu ops_per_s=%.0f p50_ns=%lu p99_ns=%lu p999_ns=%lu max_ns=%lu "
                "gt100us=%lu gt1ms=%lu gt10ms=%lu",
                o.mode.c_str(), o.label.c_str(), o.readers, secs, (unsigned long)all.n, all.n / secs,
                (unsigned long)all.pct(0.50), (unsigned long)all.pct(0.99), (unsigned long)all.pct(0.999),
                (unsigned long)all.max_ns, (unsigned long)all.over_100us, (unsigned long)all.over_1ms,
                (unsigned long)all.over_10ms);
    if (o.mode == "join") std::printf(" rows_per_s=%.0f", total_rows / secs);
    std::printf(" writer_ops_per_s=%.0f writer_keys_per_s=%.0f writer_p50_ns=%lu writer_p99_ns=%lu writer_max_ns=%lu",
                ws.ops / secs, ws.keys / secs, (unsigned long)ws.lat.pct(0.50), (unsigned long)ws.lat.pct(0.99),
                (unsigned long)ws.lat.max_ns);
#ifdef HAVE_RCU
    auto st = trades.reclaim_stats();
    std::printf(" retired=%lu freed=%lu backlog_peak=%lu passes=%lu max_pass_freed=%lu max_pass_us=%.1f holds=%lu"
                " stw_events=%lu stw_max_us=%.1f stw_total_ms=%.1f",
                (unsigned long)(st.retired - base.retired), (unsigned long)(st.freed - base.freed),
                (unsigned long)st.backlog_peak, (unsigned long)st.passes, (unsigned long)st.max_pass_freed,
                st.max_pass_ns / 1000.0, (unsigned long)holds, (unsigned long)ws.stw_events,
                ws.stw_max_ns / 1000.0, ws.stw_total_ns / 1e6);
#endif
    std::printf(" heap_loaded_mb=%.1f heap_end_mb=%.1f rss_peak_mb=%.1f\n", heap_loaded, heap_mb(), rss_peak_mb());
    return 0;
}

// One transaction of N keys on a loaded table. Each size is repeated and the
// median reported. With reclamation off, heap growth shows the copy-on-write
// cost (the new path and leaves plus the old ones waiting to be freed).
static int run_bigtxn(const Opts& o) {
    KVStore store;
    std::vector<long> hot;
    load_trades(store, hot);
#ifdef HAVE_RCU
    store.set_reclaim_batch(0);
#endif
    std::vector<long> ids;
    ids.reserve(kM * kN);
    for (int i = 0; i < kM; i++) for (int j = 0; j < kN; j++) ids.push_back(static_cast<long>(i) * 100000 + j);
    std::mt19937_64 rng(5);
    std::shuffle(ids.begin(), ids.end(), rng);

    std::printf("size,time_ms_median,ns_per_key,extra_heap_mb\n");
    std::string sizes = o.sizes;
    size_t pos = 0;
    while (pos < sizes.size()) {
        size_t comma = sizes.find(',', pos);
        long sz = std::atol(sizes.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos).c_str());
        pos = comma == std::string::npos ? sizes.size() : comma + 1;
        std::vector<double> times, extra;
        for (int rep = 0; rep < o.reps; rep++) {
            double h0 = heap_mb();
            uint64_t a = now_ns();
            Transaction tx(store);
            Trade t = make_trade(0, 0);
            for (long k = 0; k < sz; k++) { t.trade_id = ids[k]; t.ts = rep; tx.put(t.trade_id, &t, sizeof t); }
            tx.commit();
            uint64_t b = now_ns();
            times.push_back((b - a) / 1e6);
            extra.push_back(heap_mb() - h0);
#ifdef HAVE_RCU
            store.reclaim_all();
#endif
        }
        std::sort(times.begin(), times.end());
        std::sort(extra.begin(), extra.end());
        double med = times[times.size() / 2];
        std::printf("%ld,%.3f,%.0f,%.2f\n", sz, med, med * 1e6 / sz, extra[extra.size() / 2]);
    }
    return 0;
}

int main(int argc, char** argv) {
    Opts o = parse(argc, argv);
    if (o.mode == "bigtxn") return run_bigtxn(o);
    return run_workload(o);
}
