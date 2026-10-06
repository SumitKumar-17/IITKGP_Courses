// wal_test.cpp: the write-ahead log, tested on its own and through the store.
//
//   1. round trip      every durability level: write, close, reopen, compare with an oracle
//   2. truncation      the file cut at every byte of its last records and at random points
//   3. corruption      random bit flips: replay must stop at exactly the damaged record
//   4. sequence        a record removed or repeated is noticed
//   5. continue        after recovery from damage the log keeps working
//   6. group commit    many threads committing in Sync mode all survive a reopen
//   7. misc            large values, a file that is not a log, a second open
#include "kvstore.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <random>
#include <string>
#include <thread>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
#define CHECKF(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); failures++; } } while (0)

using State = std::map<long, std::string>;

static std::string dir;

static std::string file_bytes(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static void write_bytes(const std::string& p, const std::string& b) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(b.data(), static_cast<std::streamsize>(b.size()));
}

// A value carries its own length in the first four bytes so it can be read back whole.
static std::string make_value(uint32_t len, int seed) {
    std::string v(len, 'a');
    std::memcpy(&v[0], &len, 4);
    for (uint32_t i = 4; i < len; i++) v[i] = static_cast<char>('a' + (seed + i) % 23);
    return v;
}

static State read_state(const KVStore& s) {
    State out;
    KVStore::Snapshot snap = s.acquire_snapshot();
    std::vector<long> ks(200000);
    size_t n = s.keys(snap, ks.data(), ks.size());
    std::vector<char> buf(1 << 18);
    for (size_t i = 0; i < n; i++) {
        s.get(snap, ks[i], buf.data(), buf.size());
        uint32_t len;
        std::memcpy(&len, buf.data(), 4);
        out[ks[i]] = std::string(buf.data(), len);
    }
    return out;
}

static std::string tmp(const char* name) { return dir + "/" + name; }

static WalOptions opts(Durability d) {
    WalOptions o;
    o.durability = d;
    o.prealloc_bytes = 1 << 20;
    return o;
}

// ------------------------------------------------------------------ 1. round trip

struct Row { char sym[8]; long n; };

static void test_roundtrip(Durability d, const char* name) {
    std::string path = tmp(name);
    ::unlink(path.c_str());
    State oracle;
    long committed = 0;
    {
        KVStore s;
        std::string err;
        WalReplayInfo info;
        CHECK(s.open_wal(path, opts(d), &info, &err));
        CHECK(info.records == 0 && info.clean_end);
        std::mt19937_64 rng(11);
        auto put = [&](long k, uint32_t len) {
            std::string v = make_value(len, (int)rng());
            CHECK(s.put(k, v.data(), v.size()));
            oracle[k] = v;
            committed++;
        };
        for (long k = 0; k < 300; k++) put(k, 4 + rng() % 40);
        put(1000, 600);                                // heap-backed value
        put(1001, 5000);
        for (long k = 0; k < 300; k += 3) { CHECK(s.erase(k)); oracle.erase(k); committed++; }
        CHECK(!s.erase(100000));                       // absent: not logged
        put(5, 20);                                    // overwrite
        {
            Transaction t(s);
            for (long k = 2000; k < 2016; k++) { std::string v = make_value(30, (int)k); t.put(k, v.data(), v.size()); oracle[k] = v; }
            CHECK(t.commit());
            committed++;
        }
        {
            Transaction a(s), b(s);                    // lost update: b is refused and must not be logged
            std::string va = make_value(20, 1), vb = make_value(20, 2);
            a.put(7, va.data(), va.size());
            b.put(7, vb.data(), vb.size());
            CHECK(a.commit()); oracle[7] = va; committed++;
            CHECK(!b.commit());
        }
        WalStats st = s.wal_stats();
        CHECKF(st.records == (uint64_t)committed, "records=%lu committed=%ld", (unsigned long)st.records, committed);
    }
    KVStore r;
    std::string err;
    WalReplayInfo info;
    CHECKF(r.open_wal(path, opts(d), &info, &err), "%s", err.c_str());
    CHECKF(info.records == (uint64_t)committed && info.clean_end, "records=%lu committed=%ld %s", (unsigned long)info.records, committed, info.stopped.c_str());
    CHECK(read_state(r) == oracle);
    CHECK(r.size() == oracle.size());
}

static void test_roundtrip_index() {
    std::string path = tmp("index.wal");
    ::unlink(path.c_str());
    std::map<std::string, std::set<long>> want;
    auto sym = [](int i) { char c[8] = {}; std::snprintf(c, 8, "S%03d", i); return std::string(c, 8); };
    {
        KVStore s;
        CHECK(s.open_wal(path, opts(Durability::Write)));
        for (long k = 0; k < 100; k++) { Row r{}; std::memcpy(r.sym, sym(k % 10).data(), 8); r.n = k; s.put(k, &r, sizeof r); }
        CHECK(s.create_index(offsetof(Row, sym), 8));
        for (long k = 100; k < 200; k++) { Row r{}; std::memcpy(r.sym, sym(k % 10).data(), 8); r.n = k; s.put(k, &r, sizeof r); }
        for (long k = 0; k < 200; k += 7) s.erase(k);
        { Row r{}; std::memcpy(r.sym, sym(3).data(), 8); s.put(1, &r, sizeof r); }   // field change
    }
    KVStore r;
    CHECK(r.open_wal(path, opts(Durability::Write)));
    KVStore::Snapshot snap = r.acquire_snapshot();
    std::vector<long> ks(1000);
    size_t n = r.keys(snap, ks.data(), ks.size());
    std::map<std::string, std::set<long>> truth;
    for (size_t i = 0; i < n; i++) { Row row; r.get(snap, ks[i], &row, sizeof row); truth[std::string(row.sym, 8)].insert(ks[i]); }
    size_t seen = 0;
    for (int i = 0; i < 10; i++) {
        std::string f = sym(i);
        long ids[300];
        int m = r.index_get(snap, f.data(), 8, ids, 300);
        std::set<long> got(ids, ids + m);
        CHECK(got == truth[f]);
        seen += got.size();
    }
    CHECK(seen == n);
}

// ------------------------------------------------- shared log for the damage tests

struct Built {
    std::string path;
    std::vector<uint64_t> end_of_record;   // file offset just past record i (1-based: [0] is the header end)
    std::vector<State> after;              // state after i records
};

// 300 records, each a 3-key transaction, with a few erases and values of mixed size.
static Built build_log(const char* name, int records) {
    Built b;
    b.path = tmp(name);
    ::unlink(b.path.c_str());
    KVStore s;
    CHECK(s.open_wal(b.path, opts(Durability::Write)));
    State cur;
    b.end_of_record.push_back(16);
    b.after.push_back(cur);
    std::mt19937_64 rng(77);
    uint64_t prev_bytes = 0;
    for (int i = 1; i <= records; i++) {
        if (i % 11 == 0) {
            long k = (long)(rng() % 60);
            if (cur.count(k)) { CHECK(s.erase(k)); cur.erase(k); }
            else { std::string v = make_value(24, i); CHECK(s.put(k, v.data(), v.size())); cur[k] = v; }
        } else {
            Transaction t(s);
            for (int j = 0; j < 3; j++) {
                long k = (long)(rng() % 60);
                std::string v = make_value(8 + rng() % 90, i);
                t.put(k, v.data(), v.size());
                cur[k] = v;
            }
            CHECK(t.commit());
        }
        uint64_t bytes = s.wal_stats().bytes;
        b.end_of_record.push_back(16 + bytes);
        (void)prev_bytes;
        b.after.push_back(cur);
    }
    return b;
}

static void replay_into(const std::string& path, WalReplayInfo& info, State& st, bool& ok, std::string& err) {
    KVStore r;
    ok = r.open_wal(path, opts(Durability::Write), &info, &err);
    if (ok) st = read_state(r);
}

// --------------------------------------------------------------- 2. truncation

static void test_truncation(const Built& b) {
    std::string whole = file_bytes(b.path);
    int total = static_cast<int>(b.end_of_record.size()) - 1;
    uint64_t end = b.end_of_record[total];
    std::vector<uint64_t> cuts;
    for (uint64_t c = b.end_of_record[total - 3]; c <= end; c++) cuts.push_back(c);     // every byte of the last 3 records
    std::mt19937_64 rng(5);
    for (int i = 0; i < 150; i++) cuts.push_back(16 + rng() % (end - 16));
    int checked = 0;
    for (uint64_t cut : cuts) {
        std::string p = tmp("trunc.wal");
        write_bytes(p, whole.substr(0, cut));
        int expect = 0;
        while (expect < total && b.end_of_record[expect + 1] <= cut) expect++;
        WalReplayInfo info; State st; bool ok; std::string err;
        replay_into(p, info, st, ok, err);
        CHECKF(ok, "cut %lu: %s", (unsigned long)cut, err.c_str());
        CHECKF(info.records == (uint64_t)expect, "cut %lu: replayed %lu records, expected %d", (unsigned long)cut, (unsigned long)info.records, expect);
        CHECKF(st == b.after[expect], "cut %lu: state is not the state after %d records", (unsigned long)cut, expect);
        CHECKF(info.records_after_damage == 0, "cut %lu: a torn tail reported %lu valid records after it", (unsigned long)cut, (unsigned long)info.records_after_damage);
        checked++;
    }
    std::printf("truncation: %d cuts, every one recovered exactly the complete records before it\n", checked);
}

// ------------------------------------------------------------------ 3. corruption

static void test_corruption(const Built& b) {
    std::string whole = file_bytes(b.path);
    int total = static_cast<int>(b.end_of_record.size()) - 1;
    uint64_t end = b.end_of_record[total];
    std::mt19937_64 rng(9);
    int stopped_early = 0;
    for (int trial = 0; trial < 600; trial++) {
        uint64_t pos = 16 + rng() % (end - 16);
        std::string bad = whole;
        bad[pos] ^= static_cast<char>(1 + rng() % 255);
        std::string p = tmp("corrupt.wal");
        write_bytes(p, bad);
        int j = 0;                                   // the record the damaged byte lies in
        while (b.end_of_record[j + 1] <= pos) j++;
        WalReplayInfo info; State st; bool ok; std::string err;
        replay_into(p, info, st, ok, err);
        CHECKF(ok, "flip at %lu: %s", (unsigned long)pos, err.c_str());
        CHECKF(info.records == (uint64_t)j, "flip at %lu (record %d): replayed %lu", (unsigned long)pos, j + 1, (unsigned long)info.records);
        CHECKF(st == b.after[j], "flip at %lu: state differs from the state before the damaged record", (unsigned long)pos);
        CHECK(!info.clean_end && !info.stopped.empty());
        // Damage in a record that is not the last has valid records after it: that is
        // corruption, not a torn tail, and replay must say so.
        CHECKF(info.records_after_damage == (uint64_t)(total - j - 1),
               "flip at %lu (record %d of %d): %lu records after the damage reported, expected %d", (unsigned long)pos,
               j + 1, total, (unsigned long)info.records_after_damage, total - j - 1);
        stopped_early++;
    }
    // Damage inside the file header is refused outright.
    for (int i = 0; i < 8; i++) {
        std::string bad = whole;
        bad[i] ^= 0x40;
        std::string p = tmp("badhdr.wal");
        write_bytes(p, bad);
        KVStore r;
        std::string err;
        CHECK(!r.open_wal(p, opts(Durability::Write), nullptr, &err));
    }
    std::printf("corruption: %d random bit flips, every one stopped replay at exactly the damaged record and reported the valid records it had to drop\n", stopped_early);
}

// -------------------------------------------------------------------- 4. sequence

static void test_sequence(const Built& b) {
    std::string whole = file_bytes(b.path);
    int total = static_cast<int>(b.end_of_record.size()) - 1;
    for (int k : {5, 100, 250}) {
        uint64_t a = b.end_of_record[k - 1], e = b.end_of_record[k];
        std::string missing = whole.substr(0, a) + whole.substr(e);                 // record k removed
        std::string dup = whole.substr(0, e) + whole.substr(a, e - a) + whole.substr(e);   // record k repeated
        for (int variant = 0; variant < 2; variant++) {
            std::string p = tmp("seq.wal");
            write_bytes(p, variant == 0 ? missing : dup);
            int expect = variant == 0 ? k - 1 : k;
            WalReplayInfo info; State st; bool ok; std::string err;
            replay_into(p, info, st, ok, err);
            CHECK(ok);
            CHECKF(info.records == (uint64_t)expect, "%s %d: replayed %lu, expected %d", variant ? "repeat" : "remove", k, (unsigned long)info.records, expect);
            CHECK(st == b.after[expect]);
            CHECK(!info.clean_end && info.stopped.find("sequence") != std::string::npos);
        }
    }
    (void)total;
    std::printf("sequence: removed and repeated records were caught by the sequence numbers\n");
}

// -------------------------------------------------------------------- 5. continue

static void test_continue(const Built& b) {
    std::string whole = file_bytes(b.path);
    int cut_record = 120;
    uint64_t cut = b.end_of_record[cut_record] + 11;               // a torn record after record 120
    std::string p = tmp("cont.wal");
    write_bytes(p, whole.substr(0, cut));
    State expect = b.after[cut_record];
    {
        KVStore s;
        WalReplayInfo info; std::string err;
        CHECK(s.open_wal(p, opts(Durability::Write), &info, &err));
        CHECK(info.records == (uint64_t)cut_record && !info.clean_end && info.discarded_bytes > 0);
        for (long k = 500; k < 520; k++) { std::string v = make_value(16, (int)k); CHECK(s.put(k, v.data(), v.size())); expect[k] = v; }
    }
    KVStore r;
    WalReplayInfo info; std::string err;
    CHECK(r.open_wal(p, opts(Durability::Write), &info, &err));
    CHECKF(info.records == (uint64_t)cut_record + 20 && info.clean_end, "records %lu clean %d", (unsigned long)info.records, info.clean_end);
    CHECK(read_state(r) == expect);
    std::printf("continue: after dropping a torn tail the log took 20 more records and replayed cleanly\n");
}

// ----------------------------------------------------------------- 6. group commit

static void test_group_commit(int delay_us) {
    std::string path = tmp("group.wal");
    ::unlink(path.c_str());
    const int T = 8, N = 150;
    uint64_t syncs = 0, records = 0;
    {
        KVStore s;
        WalOptions o = opts(Durability::Sync);
        o.group_delay_us = delay_us;
        CHECK(s.open_wal(path, o));
        std::vector<std::thread> th;
        for (int t = 0; t < T; t++) th.emplace_back([&, t] {
            for (int i = 0; i < N; i++) {
                std::string v = make_value(24, i);
                Transaction tx(s);
                tx.put((long)t * 1000000 + i, v.data(), v.size());
                tx.put((long)t * 1000000 + 500000 + i, v.data(), v.size());
                CHECK(tx.commit());
            }
        });
        for (auto& x : th) x.join();
        WalStats st = s.wal_stats();
        syncs = st.syncs; records = st.records;
        CHECK(records == (uint64_t)T * N);
    }
    KVStore r;
    WalReplayInfo info;
    CHECK(r.open_wal(path, opts(Durability::Sync), &info));
    CHECK(info.records == (uint64_t)T * N && info.clean_end);
    CHECK(r.size() == (size_t)T * N * 2);
    // With no delay batching depends on timing, so it is only required not to be worse than one
    // sync per commit; with a delay for followers to arrive, sharing must show.
    CHECKF(syncs <= records && (delay_us == 0 || syncs < records), "no batching: %lu syncs for %lu commits", (unsigned long)syncs, (unsigned long)records);
    std::printf("group commit (delay %d us): %lu commits shared %lu syncs (%.1f commits per sync), all present after reopen\n",
                delay_us, (unsigned long)records, (unsigned long)syncs, (double)records / (double)syncs);
}

// ----------------------------------------------------------------------- 7. misc

static void test_misc() {
    std::string p = tmp("notalog.wal");
    write_bytes(p, std::string(100, 'x'));
    KVStore a;
    std::string err;
    CHECK(!a.open_wal(p, opts(Durability::Write), nullptr, &err) && !err.empty());

    std::string q = tmp("twice.wal");
    ::unlink(q.c_str());
    KVStore b;
    CHECK(b.open_wal(q, opts(Durability::Write)));
    CHECK(!b.open_wal(q, opts(Durability::Write), nullptr, &err));

    // A value bigger than the preallocated chunk.
    std::string r = tmp("big.wal");
    ::unlink(r.c_str());
    std::string big = make_value(3u << 20, 3);
    {
        KVStore s;
        WalOptions o = opts(Durability::Write);
        o.prealloc_bytes = 1 << 16;
        CHECK(s.open_wal(r, o));
        CHECK(s.put(1, big.data(), big.size()));
    }
    KVStore s2;
    CHECK(s2.open_wal(r, opts(Durability::Write)));
    std::vector<char> buf(4u << 20);
    CHECK(s2.get(1, buf.data(), buf.size()) && std::memcmp(buf.data(), big.data(), big.size()) == 0);
}

int main(int argc, char** argv) {
    int groups = argc > 1 ? std::atoi(argv[1]) : 1;
    char tmpl[] = "./waltest.XXXXXX";
    if (!mkdtemp(tmpl)) { std::perror("mkdtemp"); return 2; }
    dir = tmpl;

    test_roundtrip(Durability::Async, "async.wal");
    test_roundtrip(Durability::Write, "write.wal");
    test_roundtrip(Durability::Sync, "sync.wal");
    test_roundtrip_index();
    std::printf("round trip: async, write and sync logs reopened to exactly the state that was written, index included\n");

    Built b = build_log("base.wal", 300);
    test_truncation(b);
    test_corruption(b);
    test_sequence(b);
    test_continue(b);
    for (int i = 0; i < groups; i++) { test_group_commit(0); test_group_commit(200); }
    test_misc();

    std::string cmd = "rm -rf " + dir;
    if (std::system(cmd.c_str()) != 0) { /* leftovers are harmless */ }
    std::printf(failures ? "wal_test: %d failure(s)\n" : "wal_test: all passed\n", failures);
    return failures != 0;
}
