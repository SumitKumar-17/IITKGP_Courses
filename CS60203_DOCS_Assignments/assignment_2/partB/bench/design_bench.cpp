// Compares ways of building an RCU version of the table before we commit to one
// for kvstore.cpp. Every design publishes a new immutable root with a single
// atomic store and uses the same epoch scheme, so only the copy-on-write unit
// differs:
//   full     copy the whole slot array on every put
//   dirseg   root of NSEG segment pointers; a put copies the root and one
//            segment (inline 64-byte slots, or 16-byte slots + value block)
//   trie     radix trie, FAN children per node, path copying; leaves hold a
//            few inline entries
//
// usage: design_bench <design> [param] [keys] [writer_puts_per_sec (0 = flat out)]
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <string>
#include <thread>
#include <vector>

using clk = std::chrono::steady_clock;

static inline uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

constexpr uint64_t kIdle = ~0ULL;

struct Epochs {
    std::atomic<uint64_t> global{1};
    struct alignas(64) Slot { std::atomic<uint64_t> e{kIdle}; };
    Slot slots[64];

    void enter(int tid) {
        slots[tid].e.store(global.load(std::memory_order_relaxed), std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
    void leave(int tid) { slots[tid].e.store(kIdle, std::memory_order_release); }
    uint64_t min_active() {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        uint64_t m = kIdle;
        for (auto& s : slots) { uint64_t e = s.e.load(std::memory_order_acquire); if (e < m) m = e; }
        return m;
    }
};

// One record per published version: everything that version replaced.
struct Garbage {
    Garbage* next = nullptr;
    uint64_t epoch = 0;
    int n = 0;
    void* p[24];
};

// Layout: each design's `cur` (the only thing readers load) sits on a cache line of
// its own, away from the writer's bookkeeping below. Sharing a line made the readers'
// numbers depend on where the object happened to land.
struct alignas(64) Base {
    Epochs ep;
    alignas(64) Garbage *head = nullptr, *tail = nullptr;
    Garbage* open = nullptr;
    size_t batch = 1 << 30;
    size_t retired = 0, freed = 0, peak = 0;

    void begin() { open = new Garbage(); }
    void drop(void* p) { if (p) open->p[open->n++] = p; }
    void finish() {
        open->epoch = ep.global.load(std::memory_order_relaxed);
        if (tail) tail->next = open; else head = open;
        tail = open; open = nullptr;
        ep.global.fetch_add(1, std::memory_order_acq_rel);
        retired++;
        if (retired - freed > peak) peak = retired - freed;
        reclaim();
    }
    void reclaim() {
        uint64_t m = ep.min_active();
        size_t k = 0;
        while (head && head->epoch < m && k < batch) {
            Garbage* g = head; head = g->next; if (!head) tail = nullptr;
            for (int i = 0; i < g->n; i++) std::free(g->p[i]);
            delete g; freed++; k++;
        }
    }
};

// ---------------------------------------------------------------- full copy
struct Full : Base {
    struct alignas(64) Slot { long key; uint32_t len, used; char v[48]; };
    struct Root { size_t cap, n; Slot s[]; };
    alignas(64) std::atomic<Root*> cur;
    Full() {
        Root* r = (Root*)std::aligned_alloc(64, 64 + 64 * sizeof(Slot));
        r->cap = 64; r->n = 0; std::memset(r->s, 0, 64 * sizeof(Slot)); cur = r;
    }
    static Slot* probe(Root* r, long k) {
        size_t i = mix(k) & (r->cap - 1);
        while (r->s[i].used && r->s[i].key != k) i = (i + 1) & (r->cap - 1);
        return &r->s[i];
    }
    bool get(int tid, long k, void* out, size_t cap) {
        ep.enter(tid);
        Root* r = cur.load(std::memory_order_acquire);
        Slot* s = probe(r, k);
        bool ok = s->used;
        if (ok) std::memcpy(out, s->v, s->len < cap ? s->len : cap);
        ep.leave(tid);
        return ok;
    }
    void put(long k, const void* v, size_t len) {
        begin();
        Root* o = cur.load(std::memory_order_relaxed);
        bool exists = probe(o, k)->used;
        size_t cap = o->cap;
        if (!exists && (o->n + 1) * 2 > cap) cap *= 2;
        Root* r = (Root*)std::aligned_alloc(64, 64 + cap * sizeof(Slot));
        r->cap = cap; r->n = 0; std::memset(r->s, 0, cap * sizeof(Slot));
        for (size_t i = 0; i < o->cap; i++) if (o->s[i].used) { *probe(r, o->s[i].key) = o->s[i]; r->n++; }
        Slot* s = probe(r, k);
        if (!s->used) { s->used = 1; s->key = k; r->n++; }
        s->len = len; std::memcpy(s->v, v, len);
        cur.store(r, std::memory_order_seq_cst);
        drop(o);
        finish();
    }
};

// ------------------------------------------------------------------- dirseg
template <bool Inline>
struct DirSeg : Base {
    struct alignas(64) SlotI { long key; uint32_t len, used; char v[48]; };
    struct Val { uint32_t len; char d[]; };
    struct SlotP { long key; Val* val; };
    using Slot = typename std::conditional<Inline, SlotI, SlotP>::type;
    struct Seg { uint32_t cap, n; Slot s[]; };
    struct Root { size_t n; Seg* dir[]; };

    size_t nseg, shift;
    alignas(64) std::atomic<Root*> cur;

    explicit DirSeg(size_t nseg_) : nseg(nseg_) {
        shift = 64 - __builtin_ctzll(nseg);
        Root* r = (Root*)std::malloc(sizeof(Root) + nseg * sizeof(Seg*));
        r->n = nseg;
        for (size_t i = 0; i < nseg; i++) r->dir[i] = make_seg(8);
        cur = r;
    }
    static Seg* make_seg(uint32_t cap) {
        Seg* s = (Seg*)std::aligned_alloc(64, ((sizeof(Seg) + cap * sizeof(Slot)) + 63) / 64 * 64);
        s->cap = cap; s->n = 0; std::memset(s->s, 0, cap * sizeof(Slot)); return s;
    }
    static bool used(const Slot& s) { if constexpr (Inline) return s.used; else return s.val != nullptr; }
    static Slot* probe(Seg* g, long k) {
        size_t i = mix(k) & (g->cap - 1);
        while (used(g->s[i]) && g->s[i].key != k) i = (i + 1) & (g->cap - 1);
        return &g->s[i];
    }
    size_t index(long k) const { return mix(k) >> shift; }

    bool get(int tid, long k, void* out, size_t cap) {
        ep.enter(tid);
        Root* r = cur.load(std::memory_order_acquire);
        Seg* g = r->dir[index(k)];
        Slot* s = probe(g, k);
        bool ok = used(*s);
        if (ok) {
            if constexpr (Inline) std::memcpy(out, s->v, s->len < cap ? s->len : cap);
            else std::memcpy(out, s->val->d, s->val->len < cap ? s->val->len : cap);
        }
        ep.leave(tid);
        return ok;
    }
    void put(long k, const void* v, size_t len) {
        begin();
        Root* o = cur.load(std::memory_order_relaxed);
        size_t di = index(k);
        Seg* og = o->dir[di];
        bool exists = used(*probe(og, k));
        uint32_t cap = og->cap;
        if (!exists && (og->n + 1) * 2 > cap) cap *= 2;
        Seg* g = make_seg(cap);
        for (uint32_t i = 0; i < og->cap; i++) if (used(og->s[i])) { *probe(g, og->s[i].key) = og->s[i]; g->n++; }
        Slot* s = probe(g, k);
        if (!used(*s)) { s->key = k; g->n++; if constexpr (Inline) s->used = 1; }
        if constexpr (Inline) { s->len = len; std::memcpy(s->v, v, len); }
        else {
            if (exists) drop(s->val);
            Val* nv = (Val*)std::malloc(sizeof(Val) + len); nv->len = len; std::memcpy(nv->d, v, len); s->val = nv;
        }
        Root* r = (Root*)std::malloc(sizeof(Root) + nseg * sizeof(Seg*));
        r->n = nseg;
        std::memcpy(r->dir, o->dir, nseg * sizeof(Seg*));
        r->dir[di] = g;
        cur.store(r, std::memory_order_seq_cst);
        drop(o); drop(og);
        finish();
    }
};

// --------------------------------------------------------------------- trie
// Radix trie over the hash. Inner nodes have 2^FANBITS children; a leaf keeps up
// to LEAFMAX entries as a key array followed by value blocks, so a lookup scans
// a few contiguous keys and then touches one value.
template <int FANBITS, int LEAFMAX>
struct Trie : Base {
    static constexpr int FAN = 1 << FANBITS;
    struct Val { uint32_t len; char d[44]; };
    struct Leaf { uint32_t n, pad; long k[]; };   // k[n] keys, then n Vals
    struct Node { uintptr_t c[FAN]; };            // low bit set: leaf; 0: empty
    alignas(64) std::atomic<uintptr_t> cur{0};

    static bool is_leaf(uintptr_t p) { return p & 1; }
    static Leaf* leaf(uintptr_t p) { return (Leaf*)(p & ~1ULL); }
    static uintptr_t tag(Leaf* l) { return (uintptr_t)l | 1; }
    static Val* vals(Leaf* l) { return (Val*)(l->k + l->n); }
    static int chunk(uint64_t h, int depth) { return (h >> (64 - FANBITS * (depth + 1))) & (FAN - 1); }
    static constexpr int kMaxDepth = 64 / FANBITS - 1;
    static Leaf* new_leaf(uint32_t n) {
        Leaf* l = (Leaf*)std::malloc(sizeof(Leaf) + n * (sizeof(long) + sizeof(Val)));
        l->n = n; return l;
    }

    bool get(int tid, long k, void* out, size_t cap) {
        ep.enter(tid);
        uintptr_t p = cur.load(std::memory_order_acquire);
        uint64_t h = mix(k);
        int d = 0;
        while (p && !is_leaf(p)) p = ((Node*)p)->c[chunk(h, d++)];
        bool ok = false;
        if (p) {
            Leaf* l = leaf(p);
            for (uint32_t i = 0; i < l->n; i++) if (l->k[i] == k) {
                Val& v = vals(l)[i];
                std::memcpy(out, v.d, v.len < cap ? v.len : cap); ok = true; break;
            }
        }
        ep.leave(tid);
        return ok;
    }

    uintptr_t insert(uintptr_t p, uint64_t h, int d, long k, const Val& v) {
        if (!p || is_leaf(p)) {
            Leaf* ol = p ? leaf(p) : nullptr;
            uint32_t on = ol ? ol->n : 0;
            int at = -1;
            for (uint32_t i = 0; i < on; i++) if (ol->k[i] == k) at = i;
            uint32_t nn = at >= 0 ? on : on + 1;
            if (nn <= (uint32_t)LEAFMAX || d >= kMaxDepth) {
                Leaf* l = new_leaf(nn);
                for (uint32_t i = 0; i < on; i++) { l->k[i] = ol->k[i]; vals(l)[i] = vals(ol)[i]; }
                uint32_t slot = at >= 0 ? at : on;
                l->k[slot] = k; vals(l)[slot] = v;
                drop(ol);
                return tag(l);
            }
            Node* n = (Node*)std::calloc(1, sizeof(Node));
            for (uint32_t i = 0; i < on; i++) {
                uint64_t hh = mix(ol->k[i]);
                int c = chunk(hh, d);
                n->c[c] = insert(n->c[c], hh, d + 1, ol->k[i], vals(ol)[i]);
            }
            drop(ol);
            int c = chunk(h, d);
            n->c[c] = insert(n->c[c], h, d + 1, k, v);
            return (uintptr_t)n;
        }
        Node* o = (Node*)p;
        Node* n = (Node*)std::malloc(sizeof(Node));
        std::memcpy(n, o, sizeof(Node));
        int c = chunk(h, d);
        n->c[c] = insert(o->c[c], h, d + 1, k, v);
        drop(o);
        return (uintptr_t)n;
    }
    void put(long k, const void* v, size_t len) {
        begin();
        Val val; val.len = len; std::memcpy(val.d, v, len);
        uintptr_t o = cur.load(std::memory_order_relaxed);
        cur.store(insert(o, mix(k), 0, k, val), std::memory_order_seq_cst);
        finish();
    }
};

// ------------------------------------------------------------------- driver
struct Result { double load_s, put_ns, mops, wrate; size_t peak, retired; double mb; };

template <class S>
Result run(S& s, size_t keys, long rate, int readers, double secs) {
    char v[32] = {};
    auto t0 = clk::now();
    for (size_t i = 0; i < keys; i++) { long k = (i / 100) * 100000 + i % 100; std::memcpy(v, &i, 8); s.put(k, v, 32); }
    double load = std::chrono::duration<double>(clk::now() - t0).count();
    double mb = mallinfo2().uordblks / 1048576.0;
    {
        char chk[32];
        for (size_t i = 0; i < keys; i++) {
            long k = (i / 100) * 100000 + i % 100;
            size_t got = ~0ULL;
            if (!s.get(0, k, chk, 32)) { std::fprintf(stderr, "MISSING key %ld\n", k); std::exit(1); }
            std::memcpy(&got, chk, 8);
            if (got != i) { std::fprintf(stderr, "WRONG value for key %ld\n", k); std::exit(1); }
        }
    }

    std::atomic<bool> stop{false};
    std::vector<uint64_t> ops(readers, 0);
    std::vector<std::thread> th;
    for (int t = 0; t < readers; t++) th.emplace_back([&, t] {
        uint64_t x = mix(t + 1), n = 0; char out[32];
        while (!stop.load(std::memory_order_relaxed)) {
            x = mix(x);
            size_t i = (x % 10 < 8) ? ((x >> 8) % (keys / 100)) * 100 : (x >> 8) % keys;
            s.get(t, (i / 100) * 100000 + i % 100, out, 32);
            n++;
        }
        ops[t] = n;
    });
    uint64_t puts = 0, ptime = 0;
    auto start = clk::now();
    auto next = start;
    uint64_t x = 99;
    while (std::chrono::duration<double>(clk::now() - start).count() < secs) {
        x = mix(x);
        size_t i = x % keys; long k = (i / 100) * 100000 + i % 100;
        auto a = clk::now();
        s.put(k, v, 32);
        ptime += std::chrono::duration_cast<std::chrono::nanoseconds>(clk::now() - a).count();
        puts++;
        if (rate > 0) { next += std::chrono::nanoseconds(1000000000L / rate); while (clk::now() < next) {} }
    }
    stop = true;
    for (auto& t : th) t.join();
    uint64_t total = 0; for (auto o : ops) total += o;
    return {load, (double)ptime / puts, total / secs / 1e6, puts / secs, s.peak, s.retired, mb};
}

#define TRIE_CASE(F, L) if (param == F && leafmax == L) { Trie<F, L> s; r = run(s, keys, rate, 8, 3); }

int main(int argc, char** argv) {
    std::string d = argc > 1 ? argv[1] : "dirseg-inline";
    int param = argc > 2 ? atoi(argv[2]) : 0;
    size_t keys = argc > 3 ? atol(argv[3]) : 100000;
    long rate = argc > 4 ? atol(argv[4]) : 50000;
    int leafmax = argc > 5 ? atoi(argv[5]) : 4;
    Result r{};
    if (d == "full") { Full s; r = run(s, keys, rate, 8, 3); }
    else if (d == "dirseg-inline") { DirSeg<true> s(param); r = run(s, keys, rate, 8, 3); }
    else if (d == "dirseg-ptr") { DirSeg<false> s(param); r = run(s, keys, rate, 8, 3); }
    else if (d == "trie") {
        TRIE_CASE(4, 4) TRIE_CASE(5, 4) TRIE_CASE(6, 4)
        TRIE_CASE(4, 8) TRIE_CASE(5, 8) TRIE_CASE(6, 8)
        TRIE_CASE(4, 16) TRIE_CASE(5, 16) TRIE_CASE(6, 16)
        TRIE_CASE(6, 32) TRIE_CASE(8, 16) TRIE_CASE(8, 32)
    }
    std::printf("%-14s param=%-5d leaf=%-3d keys=%-8zu load=%6.2fs  put=%8.0f ns  writer=%8.0f/s  readers=%6.1f Mops/s  peak_retired=%zu  heap=%.1fMB\n",
                d.c_str(), param, leafmax, keys, r.load_s, r.put_ns, r.wrate, r.mops, r.peak, r.mb);
}
