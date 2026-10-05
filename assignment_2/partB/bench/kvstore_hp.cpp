// kvstore_hp.cpp: kvstore.cpp with hazard pointers in place of epochs, built only to
// compare the two schemes on identical code (see README.md). Differences are the
// ReadGuard, the Version sequence number, and how the reclamation floor is found.
#include "../kvstore.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr int kFanBits = 6;
constexpr int kFan = 1 << kFanBits;
constexpr uint32_t kLeafCap = 8;
constexpr int kMaxDepth = 9;       // 10 chunks of 6 bits fit in a 64-bit hash
constexpr size_t kInline = 40;

inline uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

inline int chunk(uint64_t h, int depth) { return (h >> (64 - kFanBits * (depth + 1))) & (kFan - 1); }

// ------------------------------------------------------------------ layout

struct Val {                       // 48 bytes, long values spill to the heap
    uint32_t len;
    uint32_t pad;
    union { char d[kInline]; char* heap; };
};
static_assert(sizeof(Val) == 48, "Val must stay 48 bytes");

struct Leaf {                      // followed by long k[cap], then Val v[cap]
    uint64_t build;
    uint32_t n, cap;
};
struct Node {
    uint64_t build;
    uintptr_t c[kFan];             // low bit set: Leaf, zero: empty
};

inline long* keys(Leaf* l) { return reinterpret_cast<long*>(l + 1); }
inline Val* vals(Leaf* l) { return reinterpret_cast<Val*>(keys(l) + l->cap); }
inline const long* keys(const Leaf* l) { return reinterpret_cast<const long*>(l + 1); }
inline const Val* vals(const Leaf* l) { return reinterpret_cast<const Val*>(keys(l) + l->cap); }

inline bool is_leaf(uintptr_t p) { return p & 1; }
inline Leaf* as_leaf(uintptr_t p) { return reinterpret_cast<Leaf*>(p & ~uintptr_t(1)); }
inline Node* as_node(uintptr_t p) { return reinterpret_cast<Node*>(p); }
inline uintptr_t tag(Leaf* l) { return reinterpret_cast<uintptr_t>(l) | 1; }

// ------------------------------------------------------------- epoch table
//
// One slot per reader (or per pinned snapshot). A slot holds the global epoch
// the reader saw when it started, or kIdle. The table is shared by every
// store in the process, which is conservative but keeps readers to one slot.

constexpr uint64_t kIdle = ~uint64_t(0);
constexpr int kMaxSlots = 1024;

struct alignas(64) EpochSlot {
    std::atomic<uint64_t> epoch{kIdle};
    std::atomic<bool> used{false};
};

EpochSlot g_slots[kMaxSlots];
std::atomic<int> g_slot_hwm{0};
std::atomic<uint64_t> g_epoch{1};

EpochSlot* claim_slot() {
    for (int i = 0; i < kMaxSlots; i++) {
        if (g_slots[i].used.load(std::memory_order_relaxed)) continue;
        if (g_slots[i].used.exchange(true)) continue;
        int hwm = g_slot_hwm.load();
        while (hwm < i + 1 && !g_slot_hwm.compare_exchange_weak(hwm, i + 1)) {}
        return &g_slots[i];
    }
    std::fprintf(stderr, "kvstore: more than %d concurrent readers\n", kMaxSlots);
    std::abort();
}

struct SlotHolder {
    EpochSlot* slot = nullptr;
    ~SlotHolder() {
        if (!slot) return;
        slot->epoch.store(kIdle, std::memory_order_release);
        slot->used.store(false);
    }
};

EpochSlot* my_slot() {
    thread_local SlotHolder h;
    if (!h.slot) h.slot = claim_slot();
    return h.slot;
}

// Hazard pointer: the reader publishes the Version it is about to use and
// re-checks that it is still current, which needs a second load.
template <class V>
struct ReadGuard {
    EpochSlot* s;
    const V* v;
    explicit ReadGuard(const std::atomic<V*>& cur) : s(my_slot()) {
        for (;;) {
            V* p = cur.load(std::memory_order_seq_cst);
            s->epoch.store(reinterpret_cast<uint64_t>(p), std::memory_order_seq_cst);
            if (cur.load(std::memory_order_seq_cst) == p) { v = p; return; }
        }
    }
    ~ReadGuard() { s->epoch.store(kIdle, std::memory_order_release); }
};

// Oldest version any reader still holds. Garbage from that version on stays.
template <class V>
uint64_t min_protected_seq() {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    uint64_t m = kIdle;
    int hwm = g_slot_hwm.load(std::memory_order_acquire);
    for (int i = 0; i < hwm; i++) {
        uint64_t e = g_slots[i].epoch.load(std::memory_order_acquire);
        if (e == kIdle) continue;
        uint64_t seq = reinterpret_cast<const V*>(e)->seq;
        if (seq < m) m = seq;
    }
    return m;
}

// -------------------------------------------------------------- read side

const Val* find(uintptr_t root, long key) {
    uint64_t h = mix(key);
    uintptr_t p = root;
    for (int d = 0; p && !is_leaf(p); d++) p = as_node(p)->c[chunk(h, d)];
    if (!p) return nullptr;
    const Leaf* l = as_leaf(p);
    const long* k = keys(l);
    for (uint32_t i = 0; i < l->n; i++)
        if (k[i] == key) return &vals(l)[i];
    return nullptr;
}

void copy_out(const Val& v, void* out, size_t cap) {
    std::memcpy(out, v.len > kInline ? v.heap : v.d, v.len < cap ? v.len : cap);
}

// ------------------------------------------------------------- write side

struct Ctx {
    uint64_t build;                // identifies nodes created by this write
    std::vector<void*>& retired;   // replaced objects, freed later
};

bool make_val(Val& v, const void* src, size_t len) {
    if (len > UINT32_MAX) return false;
    v.len = static_cast<uint32_t>(len);
    v.pad = 0;
    if (len <= kInline) {
        std::memcpy(v.d, src, len);
        return true;
    }
    v.heap = static_cast<char*>(std::malloc(len));
    if (!v.heap) return false;
    std::memcpy(v.heap, src, len);
    return true;
}

inline void retire_val(const Val& v, Ctx& c) {
    if (v.len > kInline) c.retired.push_back(v.heap);
}

Leaf* new_leaf(Ctx& c, uint32_t cap) {
    Leaf* l = static_cast<Leaf*>(std::malloc(sizeof(Leaf) + cap * (sizeof(long) + sizeof(Val))));
    if (!l) throw std::bad_alloc();
    l->build = c.build; l->n = 0; l->cap = cap;
    return l;
}

// A leaf or node made by this same write has not been published, so it can be
// changed in place and freed immediately. Anything else is copied and the
// original retired.
Leaf* own(Leaf* o, Ctx& c, uint32_t cap) {
    if (o->build == c.build && o->cap == cap) return o;
    Leaf* l = new_leaf(c, cap);
    l->n = o->n;
    std::memcpy(keys(l), keys(o), o->n * sizeof(long));
    std::memcpy(vals(l), vals(o), o->n * sizeof(Val));
    if (o->build == c.build) std::free(o); else c.retired.push_back(o);
    return l;
}

Node* own(Node* o, Ctx& c) {
    if (o->build == c.build) return o;
    Node* n = static_cast<Node*>(std::malloc(sizeof(Node)));
    if (!n) throw std::bad_alloc();
    std::memcpy(n, o, sizeof(Node));
    n->build = c.build;
    c.retired.push_back(o);
    return n;
}

void release(Leaf* l, Ctx& c) { if (l->build == c.build) std::free(l); else c.retired.push_back(l); }
void release(Node* n, Ctx& c) { if (n->build == c.build) std::free(n); else c.retired.push_back(n); }

uintptr_t insert(uintptr_t p, uint64_t h, int d, long key, const Val& v, Ctx& c, bool& added) {
    if (p && !is_leaf(p)) {
        Node* n = own(as_node(p), c);
        int i = chunk(h, d);
        n->c[i] = insert(n->c[i], h, d + 1, key, v, c, added);
        return reinterpret_cast<uintptr_t>(n);
    }
    Leaf* o = p ? as_leaf(p) : nullptr;
    uint32_t on = o ? o->n : 0;
    int at = -1;
    for (uint32_t i = 0; i < on; i++)
        if (keys(o)[i] == key) { at = static_cast<int>(i); break; }

    if (at >= 0) {
        Leaf* l = own(o, c, o->cap);
        retire_val(vals(l)[at], c);
        vals(l)[at] = v;
        return tag(l);
    }
    added = true;
    if (!o) {
        Leaf* l = new_leaf(c, kLeafCap);
        keys(l)[0] = key; vals(l)[0] = v; l->n = 1;
        return tag(l);
    }
    if (on < o->cap) {
        Leaf* l = own(o, c, o->cap);
        keys(l)[on] = key; vals(l)[on] = v; l->n = on + 1;
        return tag(l);
    }
    if (d >= kMaxDepth) {          // hash bits exhausted: let the leaf grow
        Leaf* l = own(o, c, o->cap * 2);
        keys(l)[on] = key; vals(l)[on] = v; l->n = on + 1;
        return tag(l);
    }
    Node* n = static_cast<Node*>(std::calloc(1, sizeof(Node)));
    if (!n) throw std::bad_alloc();
    n->build = c.build;
    bool unused = false;
    for (uint32_t i = 0; i < on; i++) {
        uint64_t hh = mix(keys(o)[i]);
        int s = chunk(hh, d);
        n->c[s] = insert(n->c[s], hh, d + 1, keys(o)[i], vals(o)[i], c, unused);
    }
    release(o, c);
    int s = chunk(h, d);
    bool dummy = false;
    n->c[s] = insert(n->c[s], h, d + 1, key, v, c, dummy);
    return reinterpret_cast<uintptr_t>(n);
}

// The caller has already checked that the key is present.
uintptr_t remove(uintptr_t p, uint64_t h, int d, long key, Ctx& c) {
    if (!is_leaf(p)) {
        Node* n = own(as_node(p), c);
        int i = chunk(h, d);
        n->c[i] = remove(n->c[i], h, d + 1, key, c);
        for (int j = 0; j < kFan; j++)
            if (n->c[j]) return reinterpret_cast<uintptr_t>(n);
        release(n, c);
        return 0;
    }
    Leaf* l = own(as_leaf(p), c, as_leaf(p)->cap);
    uint32_t i = 0;
    while (keys(l)[i] != key) i++;
    retire_val(vals(l)[i], c);
    l->n--;
    keys(l)[i] = keys(l)[l->n];
    vals(l)[i] = vals(l)[l->n];
    if (l->n == 0) { release(l, c); return 0; }
    return tag(l);
}

void free_tree(uintptr_t p) {
    if (!p) return;
    if (is_leaf(p)) {
        Leaf* l = as_leaf(p);
        for (uint32_t i = 0; i < l->n; i++)
            if (vals(l)[i].len > kInline) std::free(vals(l)[i].heap);
        std::free(l);
        return;
    }
    Node* n = as_node(p);
    for (int i = 0; i < kFan; i++) free_tree(n->c[i]);
    std::free(n);
}

}  // namespace

struct KVStore::Version {
    uintptr_t root;
    long count;
    uint64_t seq;
};

// Everything the writer replaced while making one version.
struct KVStore::Garbage {
    Garbage* next;
    uint64_t epoch;
    uint32_t n;
    void* p[];
};

namespace {
template <class T>
T* alloc_version(uintptr_t root, long count, uint64_t seq) {
    T* v = static_cast<T*>(std::malloc(sizeof(T)));
    if (!v) throw std::bad_alloc();
    v->root = root;
    v->count = count;
    v->seq = seq;
    return v;
}
}  // namespace

KVStore::KVStore() : current_(alloc_version<Version>(0, 0, 0)), batch_(64) {}

KVStore::~KVStore() {
    Version* v = current_.load();
    free_tree(v->root);
    std::free(v);
    while (gc_head_) {
        Garbage* g = gc_head_;
        gc_head_ = g->next;
        for (uint32_t i = 0; i < g->n; i++) std::free(g->p[i]);
        std::free(g);
    }
    for (void* p : pending_) std::free(p);
}

void KVStore::set_reclaim_batch(int n) { batch_.store(n, std::memory_order_relaxed); }

KVStore::ReclaimStats KVStore::reclaim_stats() const {
    return {retired_.load(), freed_.load(), backlog_peak_.load(), passes_.load(),
            max_pass_freed_.load(), max_pass_ns_.load()};
}

bool KVStore::get(long key, void* value_out, size_t value_out_len) const {
    ReadGuard<Version> guard(current_);
    const Version* v = guard.v;
    const Val* val = find(v->root, key);
    if (!val) return false;
    copy_out(*val, value_out, value_out_len);
    return true;
}

size_t KVStore::size() const {
    ReadGuard<Version> guard(current_);
    return static_cast<size_t>(guard.v->count);
}

bool KVStore::put(long key, const void* value, size_t value_len) {
    Val v;
    if (!make_val(v, value, value_len)) return false;
    std::lock_guard<std::mutex> g(write_mu_);
    Version* old = current_.load(std::memory_order_relaxed);
    Ctx c{++build_seq_, pending_};
    bool added = false;
    uintptr_t root = insert(old->root, mix(key), 0, key, v, c, added);
    publish(old, root, added ? 1 : 0);
    return true;
}

bool KVStore::erase(long key) {
    std::lock_guard<std::mutex> g(write_mu_);
    Version* old = current_.load(std::memory_order_relaxed);
    if (!find(old->root, key)) return false;
    Ctx c{++build_seq_, pending_};
    uintptr_t root = remove(old->root, mix(key), 0, key, c);
    publish(old, root, -1);
    return true;
}

// One atomic store makes the new version visible. The replaced objects are
// stamped with the epoch of the swap; a reader that could still hold the old
// version announced an epoch no later than that stamp.
void KVStore::publish(Version* old, uintptr_t root, long delta) {
    Version* nv = alloc_version<Version>(root, old->count + delta, old->seq + 1);
    current_.store(nv, std::memory_order_seq_cst);
    pending_.push_back(old);

    Garbage* g = static_cast<Garbage*>(std::malloc(sizeof(Garbage) + pending_.size() * sizeof(void*)));
    if (!g) throw std::bad_alloc();
    g->next = nullptr;
    g->epoch = old->seq;
    g->n = static_cast<uint32_t>(pending_.size());
    std::memcpy(g->p, pending_.data(), pending_.size() * sizeof(void*));
    pending_.clear();
    if (gc_tail_) gc_tail_->next = g; else gc_head_ = g;
    gc_tail_ = g;

    uint64_t retired = retired_.fetch_add(1, std::memory_order_relaxed) + 1;
    uint64_t backlog = retired - freed_.load(std::memory_order_relaxed);
    if (backlog > backlog_peak_.load(std::memory_order_relaxed))
        backlog_peak_.store(backlog, std::memory_order_relaxed);
    reclaim_pass();
}

// Frees at most batch_ retired versions, oldest first, and only those that no
// reader can still be using. It never waits: a version that is still pinned
// just stays on the list for a later pass.
void KVStore::reclaim_pass() {
    int batch = batch_.load(std::memory_order_relaxed);
    if (batch <= 0 || !gc_head_) return;
    uint64_t floor = min_protected_seq<Version>();
    if (gc_head_->epoch >= floor) return;

    auto t0 = std::chrono::steady_clock::now();
    uint64_t n = 0;
    while (gc_head_ && gc_head_->epoch < floor && n < static_cast<uint64_t>(batch)) {
        Garbage* g = gc_head_;
        gc_head_ = g->next;
        if (!gc_head_) gc_tail_ = nullptr;
        for (uint32_t i = 0; i < g->n; i++) std::free(g->p[i]);
        std::free(g);
        n++;
    }
    uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now() - t0).count();
    freed_.fetch_add(n, std::memory_order_relaxed);
    passes_.fetch_add(1, std::memory_order_relaxed);
    if (n > max_pass_freed_.load(std::memory_order_relaxed)) max_pass_freed_.store(n, std::memory_order_relaxed);
    if (ns > max_pass_ns_.load(std::memory_order_relaxed)) max_pass_ns_.store(ns, std::memory_order_relaxed);
}

bool KVStore::create_index(size_t, size_t) { return false; }

int KVStore::index_get(const void*, size_t, long*, int) const { return 0; }
