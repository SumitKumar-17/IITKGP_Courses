// kvstore_test.cpp: single-threaded checks for the store, the index, transactions
// and snapshots, plus reclamation bookkeeping.
#include "kvstore.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

struct Rec { long a; double b; char tag[16]; };

int main() {
    KVStore kv;
    Rec r{7, 1.5, "alpha"}, out{};

    CHECK(!kv.get(1, &out, sizeof out));
    CHECK(kv.put(1, &r, sizeof r));
    CHECK(kv.get(1, &out, sizeof out));
    CHECK(std::memcmp(&r, &out, sizeof r) == 0);

    r.a = 99;
    CHECK(kv.put(1, &r, sizeof r));
    CHECK(kv.get(1, &out, sizeof out) && out.a == 99);
    CHECK(kv.size() == 1);

    CHECK(kv.erase(1));
    CHECK(!kv.erase(1));
    CHECK(!kv.get(1, &out, sizeof out));
    CHECK(kv.size() == 0);

    std::string big(500, 'x');
    char buf[500];
    CHECK(kv.put(2, big.data(), big.size()));
    CHECK(kv.get(2, buf, sizeof buf) && std::memcmp(buf, big.data(), 500) == 0);
    std::string small = "tiny";
    CHECK(kv.put(2, small.data(), small.size()));
    CHECK(kv.get(2, buf, sizeof buf) && std::memcmp(buf, "tiny", 4) == 0);
    CHECK(kv.erase(2));

    const long N = 200000;
    for (long k = 0; k < N; k++) { long v = k * 3; CHECK(kv.put(k * 7919, &v, sizeof v)); }
    CHECK(kv.size() == (size_t)N);
    bool all = true;
    for (long k = 0; k < N; k++) { long v = 0; all &= kv.get(k * 7919, &v, sizeof v) && v == k * 3; }
    CHECK(all);

    for (long k = 0; k < N; k += 2) all &= kv.erase(k * 7919);
    CHECK(all);
    for (long k = 0; k < N; k++) {
        long v = 0;
        bool found = kv.get(k * 7919, &v, sizeof v);
        if (k % 2 == 0) all &= !found; else all &= found && v == k * 3;
    }
    CHECK(all);
    CHECK(kv.size() == (size_t)N / 2);

    // Reclamation: with no readers around, passes keep the backlog small and never
    // free more than the batch in one go.
    {
        KVStore s;
        s.set_reclaim_batch(4);
        for (long k = 0; k < 5000; k++) { long v = k; s.put(k % 700, &v, sizeof v); }
        auto st = s.reclaim_stats();
        CHECK(st.retired == 5000);
        CHECK(st.freed + 8 >= st.retired);
        CHECK(st.max_pass_freed <= 4);
    }
    {
        KVStore s;
        s.set_reclaim_batch(0);
        for (long k = 0; k < 1000; k++) s.put(k, &k, sizeof k);
        auto st = s.reclaim_stats();
        CHECK(st.freed == 0 && st.retired == 1000);
    }
    {
        // erase until empty, then reuse: pruned nodes must not leave stale links
        KVStore s;
        for (long k = 0; k < 20000; k++) s.put(k * 31, &k, sizeof k);
        for (long k = 0; k < 20000; k++) CHECK(s.erase(k * 31));
        CHECK(s.size() == 0);
        long v = 0;
        CHECK(!s.get(31, &v, sizeof v));
        for (long k = 0; k < 100; k++) s.put(k, &k, sizeof k);
        CHECK(s.size() == 100 && s.get(99, &v, sizeof v) && v == 99);
    }

    // Index: create on existing data, follow puts, field changes, erases and short rows.
    {
        struct Row { char sym[8]; long n; };
        KVStore s;
        auto put = [&](long k, const char* sym, long n) { Row r{}; std::strncpy(r.sym, sym, 7); r.n = n; s.put(k, &r, sizeof r); };
        auto find = [&](const char* sym) {
            char f[8] = {}; std::strncpy(f, sym, 7);
            long ids[16]; int m = s.index_get(f, 8, ids, 16);
            std::vector<long> v(ids, ids + m); std::sort(v.begin(), v.end()); return v;
        };
        put(1, "AAA", 1); put(2, "BBB", 2); put(3, "AAA", 3);
        CHECK(s.create_index(offsetof(Row, sym), 8));
        CHECK(!s.create_index(offsetof(Row, sym), 8));
        CHECK((find("AAA") == std::vector<long>{1, 3}));
        CHECK((find("BBB") == std::vector<long>{2}));
        put(4, "AAA", 4);
        CHECK((find("AAA") == std::vector<long>{1, 3, 4}));
        put(3, "BBB", 33);
        CHECK((find("AAA") == std::vector<long>{1, 4}));
        CHECK((find("BBB") == std::vector<long>{2, 3}));
        put(1, "AAA", 100);
        CHECK((find("AAA") == std::vector<long>{1, 4}));
        s.erase(4);
        CHECK((find("AAA") == std::vector<long>{1}));
        char tiny = 'x';
        s.put(9, &tiny, 1);
        CHECK(find("AAA").size() == 1 && find("ZZZ").empty());
        s.put(1, &tiny, 1);
        CHECK(find("AAA").empty());
        long ids[2];
        CHECK(s.index_get("BBB\0\0\0\0", 8, ids, 1) == 1);
    }

    // Transactions: all or nothing, drop on destruction, later write to a key wins.
    {
        KVStore s;
        long a = 1, b = 2, c = 3, got = 0;
        {
            Transaction t(s);
            t.put(10, &a, sizeof a); t.put(11, &b, sizeof b);
        }
        CHECK(s.size() == 0);
        Transaction t(s);
        t.put(10, &a, sizeof a); t.put(11, &b, sizeof b); t.put(10, &c, sizeof c);
        CHECK(s.size() == 0);
        CHECK(t.commit());
        CHECK(s.size() == 2 && s.get(10, &got, sizeof got) && got == 3);
        CHECK(s.get(11, &got, sizeof got) && got == 2);
        CHECK(t.commit() && s.size() == 2);
    }

    // Snapshots: fixed view, keys(), move, and pinning against reclamation.
    {
        KVStore s;
        for (long k = 0; k < 1000; k++) s.put(k, &k, sizeof k);
        KVStore::Snapshot snap = s.acquire_snapshot();
        for (long k = 0; k < 1000; k += 2) s.erase(k);
        for (long k = 1000; k < 1100; k++) s.put(k, &k, sizeof k);
        long v = 0;
        CHECK(s.get(snap, 4, &v, sizeof v) && v == 4);
        CHECK(!s.get(snap, 1050, &v, sizeof v));
        CHECK(!s.get(4, &v, sizeof v) && s.get(1050, &v, sizeof v));
        std::vector<long> ks(5000);
        CHECK(s.keys(snap, ks.data(), ks.size()) == 1000);
        CHECK(s.keys(snap, ks.data(), 10) == 10);
        KVStore::Snapshot moved = std::move(snap);
        CHECK(s.get(moved, 6, &v, sizeof v) && v == 6);
        auto during = s.reclaim_stats();
        CHECK(during.retired > during.freed + 100);   // pinned: nothing behind the snapshot is freed
        { KVStore::Snapshot drop = std::move(moved); }
        long x = 0;
        s.put(1, &x, sizeof x);
        s.put(1, &x, sizeof x);
        auto after = s.reclaim_stats();
        CHECK(after.freed > during.freed);
    }

    std::printf(failures ? "kvstore_test: %d failure(s)\n" : "kvstore_test: all passed\n", failures);
    return failures != 0;
}
