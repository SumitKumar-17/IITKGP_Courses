// txn_test.cpp: concurrent checks for Part C.
//
//   1. atomicity   a reader's snapshot never sees part of a committed transaction
//      (and the same test against plain put() calls does see torn groups, so the
//      check is able to fail)
//   2. isolation   a held snapshot never changes while the writer keeps committing,
//      and the writer is never blocked by it
//   3. index       within one snapshot the index agrees with a scan of the table
//   4. join        every join sees one version: pairs are consistent and a join
//      repeated on the same snapshot gives the same answer
//
// usage: txn_test [seconds-per-test]
#include "join.h"
#include "kvstore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <thread>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
#define CHECKF(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); failures++; } } while (0)

using clk = std::chrono::steady_clock;
static int g_seconds = 2;

template <class F>
static void run_for(int seconds, F&& f) {
    auto end = clk::now() + std::chrono::seconds(seconds);
    while (clk::now() < end) f();
}

// ---------------------------------------------------------------- 1. atomicity

static long atomicity_violations(bool use_txn, long& commits) {
    KVStore store;
    const int K = 8;
    for (long k = 0; k < K; k++) { long v = 0; store.put(k, &v, sizeof v); }

    std::atomic<bool> stop{false};
    std::atomic<long> bad{0}, snaps{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; t++) readers.emplace_back([&] {
        while (!stop.load()) {
            KVStore::Snapshot s = store.acquire_snapshot();
            long first = -1;
            bool torn = false;
            for (long k = 0; k < K; k++) {
                long v = 0;
                store.get(s, k, &v, sizeof v);
                if (k == 0) first = v; else if (v != first) torn = true;
            }
            if (torn) bad++;
            snaps++;
        }
    });

    long version = 0;
    commits = 0;
    run_for(g_seconds, [&] {
        ++version;
        if (use_txn) {
            Transaction t(store);
            for (long k = 0; k < K; k++) t.put(k, &version, sizeof version);
            t.commit();
        } else {
            for (long k = 0; k < K; k++) store.put(k, &version, sizeof version);
        }
        commits++;
    });
    stop = true;
    for (auto& r : readers) r.join();
    return bad.load();
}

static void test_atomicity() {
    long commits = 0;
    long bad = atomicity_violations(true, commits);
    std::printf("atomicity: %ld transactions committed, %ld torn snapshots\n", commits, bad);
    CHECK(bad == 0);
    CHECK(commits > 1000);

    long ctrl_commits = 0;
    long ctrl_bad = atomicity_violations(false, ctrl_commits);
    std::printf("atomicity control (plain puts): %ld torn snapshots\n", ctrl_bad);
    CHECKF(ctrl_bad > 0, "the control found nothing, so the test above proves nothing");
}

// ------------------------------------------------- 1b. conflicts and write skew

static void test_conflicts() {
    KVStore s;
    long x = 5;
    s.put(1, &x, sizeof x);

    // Lost update: two transactions start from the same state and both write key 1.
    // The first commit wins, the second is refused and nothing of it is published.
    {
        Transaction t1(s), t2(s);
        long a = 6, b = 7, other = 99;
        t1.put(1, &a, sizeof a);
        t2.put(1, &b, sizeof b);
        t2.put(2, &other, sizeof other);
        CHECK(t1.commit());
        CHECK(!t2.commit());
        long got = 0;
        CHECK(s.get(1, &got, sizeof got) && got == 6);
        CHECK(!s.get(2, &got, sizeof got));
    }
    // After the loser restarts from the new state it goes through.
    {
        Transaction t(s);
        long b = 8;
        t.put(1, &b, sizeof b);
        CHECK(t.commit());
    }
    // A transaction that starts from an older snapshot conflicts only on keys that changed since.
    {
        long k3 = 3, k4 = 4;
        s.put(3, &k3, sizeof k3);
        KVStore::Snapshot old = s.acquire_snapshot();
        s.put(3, &k4, sizeof k4);
        Transaction t(s, old);
        long v = 1;
        t.put(1, &v, sizeof v);
        CHECK(t.commit());
        Transaction t2(s, old);
        t2.put(3, &v, sizeof v);
        CHECK(!t2.commit());
    }

    // Write skew: two doctors are on call and the rule is that at least one must stay.
    // Each transaction reads both rows from its own snapshot, sees the other still on
    // call, and takes only itself off. They write different keys, so first-committer-wins
    // has nothing to object to, and both commit. Snapshot isolation allows this.
    {
        KVStore d;
        long on = 1, off = 0;
        d.put(100, &on, sizeof on);   // alice
        d.put(101, &on, sizeof on);   // bob
        KVStore::Snapshot sa = d.acquire_snapshot();
        KVStore::Snapshot sb = d.acquire_snapshot();
        long a0, b0, a1, b1;
        d.get(sa, 100, &a0, sizeof a0); d.get(sa, 101, &b0, sizeof b0);
        d.get(sb, 100, &a1, sizeof a1); d.get(sb, 101, &b1, sizeof b1);
        Transaction ta(d, sa), tb(d, sb);
        if (a0 + b0 >= 2) ta.put(100, &off, sizeof off);
        if (a1 + b1 >= 2) tb.put(101, &off, sizeof off);
        CHECK(ta.commit());
        CHECK(tb.commit());
        long a, b;
        d.get(100, &a, sizeof a); d.get(101, &b, sizeof b);
        std::printf("write skew: both commits succeeded, doctors on call afterwards = %ld (rule wanted at least 1)\n", a + b);
        CHECK(a + b == 0);
    }
    std::printf("conflicts: lost update refused, write skew reproduced\n");
}

// ---------------------------------------------------------------- 2. isolation

static void test_isolation() {
    KVStore store;
    for (long k = 0; k < 2000; k++) store.put(k, &k, sizeof k);
    store.set_reclaim_batch(64);

    KVStore::Snapshot held = store.acquire_snapshot();
    std::vector<long> before(4000);
    size_t n0 = store.keys(held, before.data(), before.size());
    CHECK(n0 == 2000);

    std::atomic<bool> stop{false};
    std::atomic<long> commits{0};
    std::thread writer([&] {
        std::mt19937_64 rng(5);
        long v = 100000;
        while (!stop.load()) {
            Transaction t(store);
            for (int i = 0; i < 4; i++) { long k = rng() % 3000; ++v; t.put(k, &v, sizeof v); }
            t.commit();
            if (rng() % 8 == 0) store.erase(rng() % 3000);
            commits++;
        }
    });

    long reads = 0, changed = 0;
    run_for(g_seconds, [&] {
        for (long k = 0; k < 2000; k += 7) {
            long v = -1;
            bool ok = store.get(held, k, &v, sizeof v);
            if (!ok || v != k) changed++;
            reads++;
        }
        std::vector<long> now(4000);
        if (store.keys(held, now.data(), now.size()) != n0 || now != before) changed++;
    });
    long c = commits.load();
    stop = true;
    writer.join();

    auto st = store.reclaim_stats();
    std::printf("isolation: %ld reads under one snapshot while the writer made %ld commits, %ld changes seen\n",
                reads, c, changed);
    std::printf("           backlog_peak=%lu (the held snapshot kept %lu versions alive)\n",
                (unsigned long)st.backlog_peak, (unsigned long)(st.retired - st.freed));
    CHECK(changed == 0);
    CHECKF(c > 1000, "writer made only %ld commits, it was blocked", c);
    CHECK(st.retired - st.freed > 1000);

    { KVStore::Snapshot drop = std::move(held); }
    // Each publish frees at most 64 versions, so the backlog drains over backlog/64 more writes.
    long x = 0;
    uint64_t backlog = store.reclaim_stats().retired - store.reclaim_stats().freed;
    for (uint64_t i = 0; i < backlog / 64 + 1000; i++) store.put(1, &x, sizeof x);
    auto after = store.reclaim_stats();
    std::printf("           after release: %lu writes drained the backlog to %lu; no pass freed more than %lu\n",
                (unsigned long)(backlog / 64 + 1000), (unsigned long)(after.retired - after.freed),
                (unsigned long)after.max_pass_freed);
    CHECK(after.retired - after.freed < 200);
    CHECK(after.max_pass_freed <= 64);
}

// -------------------------------------------------------------------- 3. index

struct Row { char sym[8]; long n; };

static void sym_name(char (&out)[8], int i) { std::memset(out, 0, 8); std::snprintf(out, 8, "S%03d", i); }

static bool index_matches_scan(const KVStore& store, int nsyms) {
    KVStore::Snapshot snap = store.acquire_snapshot();
    std::vector<long> all(10000);
    size_t n = store.keys(snap, all.data(), all.size());
    std::map<std::string, std::set<long>> truth;
    for (size_t i = 0; i < n; i++) {
        Row r;
        if (!store.get(snap, all[i], &r, sizeof r)) return false;
        truth[std::string(r.sym, 8)].insert(all[i]);
    }
    size_t seen = 0;
    for (int s = 0; s < nsyms; s++) {
        char f[8]; sym_name(f, s);
        long ids[2000];
        int m = store.index_get(snap, f, 8, ids, 2000);
        std::set<long> got(ids, ids + m);
        if ((int)got.size() != m) return false;
        auto it = truth.find(std::string(f, 8));
        std::set<long> want = it == truth.end() ? std::set<long>() : it->second;
        if (got != want) return false;
        seen += got.size();
    }
    return seen == n;
}

static void test_index() {
    const int nsyms = 20;
    KVStore store;
    std::mt19937_64 rng(3);
    for (long k = 0; k < 500; k++) { Row r{}; sym_name(r.sym, rng() % nsyms); r.n = k; store.put(k, &r, sizeof r); }
    CHECK(store.create_index(offsetof(Row, sym), 8));
    CHECK(index_matches_scan(store, nsyms));

    std::atomic<bool> stop{false};
    std::atomic<long> bad{0}, checks{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < 3; t++) readers.emplace_back([&] {
        while (!stop.load()) { if (!index_matches_scan(store, nsyms)) bad++; checks++; }
    });
    long ops = 0;
    run_for(g_seconds, [&] {
        int kind = rng() % 4;
        if (kind == 0) {
            long k = rng() % 600; Row r{}; sym_name(r.sym, rng() % nsyms); r.n = ops;
            store.put(k, &r, sizeof r);
        } else if (kind == 1) {
            store.erase(rng() % 600);
        } else {
            Transaction t(store);
            for (int i = 0; i < 6; i++) { long k = rng() % 600; Row r{}; sym_name(r.sym, rng() % nsyms); r.n = ops; t.put(k, &r, sizeof r); }
            t.commit();
        }
        ops++;
    });
    stop = true;
    for (auto& r : readers) r.join();
    std::printf("index: %ld writes, %ld snapshot-vs-scan comparisons, %ld mismatches\n", ops, checks.load(), bad.load());
    CHECK(bad == 0);
    CHECK(index_matches_scan(store, nsyms));
}

// --------------------------------------------------------------------- 4. join

static void load_trades(KVStore& trades, int n) {
    for (long k = 0; k < n; k++) {
        Trade t{}; t.trade_id = k; std::snprintf(t.symbol, 8, "SYM%03d", (int)(k % 50)); t.price = 100 + k % 97; t.ts = k;
        trades.put(k, &t, sizeof t);
    }
}

static void load_instruments(KVStore& ins, int tick) {
    ins.create_index(offsetof(Instrument, symbol), sizeof(Instrument::symbol));
    for (int s = 0; s < 50; s++) {
        Instrument i{}; std::snprintf(i.symbol, 8, "SYM%03d", s); std::snprintf(i.venue, 8, "NSE"); i.tick_size = tick;
        ins.put(s, &i, sizeof i);
    }
}

static void test_join() {
    KVStore trades, instruments;
    load_trades(trades, 5000);
    load_instruments(instruments, 1);

    JoinCounts full = index_nested_loop_join(trades, instruments, 100000, [](const Trade& t, const Instrument& i) {
        if (std::memcmp(t.symbol, i.symbol, 8) != 0) std::abort();
    });
    CHECKF(full.outer_rows == 5000 && full.matches == 5000, "rows=%ld matches=%ld", full.outer_rows, full.matches);
    JoinCounts part = index_nested_loop_join(trades, instruments, 100, [](const Trade&, const Instrument&) {});
    CHECK(part.outer_rows == 100 && part.matches == 100);

    // The writer keeps changing prices, deleting and adding trades, and bumping tick_size on
    // every instrument in one transaction. Joins running meanwhile must stay self-consistent.
    std::atomic<bool> stop{false};
    std::atomic<long> bad{0}, joins{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; t++) readers.emplace_back([&] {
        while (!stop.load()) {
            long ticks_seen = -1;
            bool mixed = false;
            JoinCounts c = index_nested_loop_join(trades, instruments, 100000, [&](const Trade& tr, const Instrument& in) {
                if (std::memcmp(tr.symbol, in.symbol, 8) != 0) bad++;
                if (ticks_seen < 0) ticks_seen = in.tick_size; else if (in.tick_size != ticks_seen) mixed = true;
            });
            if (c.matches != c.outer_rows) bad++;
            if (mixed) bad++;
            joins++;
        }
    });
    std::mt19937_64 rng(11);
    int tick = 1;
    long commits = 0;
    run_for(g_seconds, [&] {
        Transaction ti(instruments);
        ++tick;
        for (int s = 0; s < 50; s++) {
            Instrument i{}; std::snprintf(i.symbol, 8, "SYM%03d", s); std::snprintf(i.venue, 8, "NSE"); i.tick_size = tick;
            ti.put(s, &i, sizeof i);
        }
        ti.commit();
        for (int j = 0; j < 20; j++) {
            long k = rng() % 6000;
            if (rng() % 5 == 0) { trades.erase(k); continue; }
            Trade t{}; t.trade_id = k; std::snprintf(t.symbol, 8, "SYM%03d", (int)(rng() % 50)); t.price = 1; t.ts = tick;
            trades.put(k, &t, sizeof t);
        }
        commits++;
    });
    stop = true;
    for (auto& r : readers) r.join();
    std::printf("join: %ld joins ran against %ld writer rounds, %ld inconsistencies\n", joins.load(), commits, bad.load());
    CHECK(bad == 0);
    CHECK(joins > 10);

    // A join repeated on one snapshot is identical even after the tables change.
    auto digest = [&](const KVStore::Snapshot& snap) {
        std::vector<long> ks(100000);
        size_t n = trades.keys(snap, ks.data(), ks.size());
        std::sort(ks.begin(), ks.begin() + n);
        unsigned long h = 1469598103934665603UL;
        for (size_t i = 0; i < n; i++) {
            Trade t;
            if (!trades.get(snap, ks[i], &t, sizeof t)) continue;
            long ids[8];
            int m = instruments.index_get(snap, t.symbol, 8, ids, 8);
            for (int j = 0; j < m; j++) {
                Instrument in;
                instruments.get(snap, ids[j], &in, sizeof in);
                h = (h ^ (unsigned long)(t.ts * 131 + in.tick_size + ks[i])) * 1099511628211UL;
            }
        }
        return h;
    };
    KVStore::Snapshot snap = trades.acquire_snapshot();
    unsigned long first = digest(snap);
    for (int r = 0; r < 20; r++) {
        Instrument i{}; std::snprintf(i.symbol, 8, "SYM%03d", r); i.tick_size = 1000 + r; instruments.put(r, &i, sizeof i);
        Trade t{}; t.trade_id = r; std::snprintf(t.symbol, 8, "SYM%03d", r); t.ts = 777 + r; trades.put(r, &t, sizeof t);
    }
    unsigned long second = digest(snap);
    KVStore::Snapshot fresh = trades.acquire_snapshot();
    unsigned long third = digest(fresh);
    CHECKF(first == second, "join result changed under a held snapshot");
    CHECKF(first != third, "the new snapshot should see the later writes");
    std::printf("join on a held snapshot: before=%lx after writes=%lx new snapshot=%lx\n", first, second, third);
}

int main(int argc, char** argv) {
    if (argc > 1) g_seconds = std::atoi(argv[1]);
    test_atomicity();
    test_conflicts();
    test_isolation();
    test_index();
    test_join();
    std::printf(failures ? "txn_test: %d failure(s)\n" : "txn_test: all passed\n", failures);
    return failures != 0;
}
