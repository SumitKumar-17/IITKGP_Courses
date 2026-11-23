// kvstore.cpp (Part C)
#include "kvstore.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace nova_detail {

constexpr uint64_t kIdle = ~uint64_t(0);

struct alignas(64) EpochSlot {
    std::atomic<uint64_t> epoch{kIdle};
    std::atomic<bool> used{false};
};

}  // namespace nova_detail

namespace {

using nova_detail::EpochSlot;
using nova_detail::kIdle;

constexpr int kFanBits = 6;
constexpr int kFan = 1 << kFanBits;
constexpr uint32_t kLeafCap = 8;
constexpr int kMaxDepth = 9;       // 10 chunks of 6 bits fit in a 64-bit hash
constexpr size_t kInline = 40;
constexpr int kIndexPrefixDepth = 5;                    // 5 chunks = 30 bits of field hash
constexpr uint64_t kPkMask = (uint64_t(1) << 34) - 1;   // low 34 bits come from the primary key

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

inline const char* bytes(const Val& v) { return v.len > kInline ? v.heap : v.d; }

// -------------------------------------------------------- how entries route
//
// The primary trie is routed by a hash of the key. The index trie holds one
// entry per row, keyed by the row's primary key but routed by the indexed
// field: the top 30 bits come from a hash of the field bytes, so every row
// with the same field value sits under the same depth-5 node and a lookup is
// one short descent plus a walk of that subtree.

enum class Kind { Primary, Index };

uint64_t hash_bytes(const void* p, size_t n) {
    const unsigned char* b = static_cast<const unsigned char*>(p);
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
    return mix(h);
}

inline uint64_t field_prefix(const void* field, size_t len) {
    return (hash_bytes(field, len) >> 34) << 34;
}

inline uint64_t route(Kind kind, long key, const Val& v) {
    if (kind == Kind::Primary) return mix(static_cast<uint64_t>(key));
    return field_prefix(bytes(v), v.len) | (mix(static_cast<uint64_t>(key)) & kPkMask);
}

// ------------------------------------------------------------- epoch table
//
// One slot per reader thread, plus one per live Snapshot. A slot holds the
// global epoch the reader saw when it started, or kIdle. The table is shared
// by every store in the process, which is conservative but keeps a reader to
// a single slot.

constexpr int kMaxSlots = 1024;

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
    std::fprintf(stderr, "kvstore: more than %d concurrent readers and snapshots\n", kMaxSlots);
    std::abort();
}

void release_slot(EpochSlot* s) {
    s->epoch.store(kIdle, std::memory_order_release);
    s->used.store(false);
}

struct SlotHolder {
    EpochSlot* slot = nullptr;
    ~SlotHolder() { if (slot) release_slot(slot); }
};

EpochSlot* my_slot() {
    thread_local SlotHolder h;
    if (!h.slot) h.slot = claim_slot();
    return h.slot;
}

// The exchange is a full barrier, so the announcement is globally visible
// before the caller loads the version pointer.
inline void announce(EpochSlot* s) {
    s->epoch.exchange(g_epoch.load(std::memory_order_relaxed), std::memory_order_seq_cst);
}

struct ReadGuard {
    EpochSlot* s;
    ReadGuard() : s(my_slot()) { announce(s); }
    ~ReadGuard() { s->epoch.store(kIdle, std::memory_order_release); }
};

uint64_t min_active_epoch() {
#ifdef NOVA_BROKEN_RECLAIM
    return kIdle;                  // test-only: ignore readers, to prove the stress test notices
#endif
    std::atomic_thread_fence(std::memory_order_seq_cst);
    uint64_t m = kIdle;
    int hwm = g_slot_hwm.load(std::memory_order_acquire);
    for (int i = 0; i < hwm; i++) {
        uint64_t e = g_slots[i].epoch.load(std::memory_order_acquire);
        if (e < m) m = e;
    }
    return m;
}

// -------------------------------------------------------------- read side

const Val* find(uintptr_t root, long key) {
    uint64_t h = mix(static_cast<uint64_t>(key));
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
    std::memcpy(out, bytes(v), v.len < cap ? v.len : cap);
}

struct Collector {
    long* out;
    size_t max;
    size_t n = 0;
    bool full() const { return n >= max; }
};

void collect_keys(uintptr_t p, Collector& c) {
    if (!p || c.full()) return;
    if (is_leaf(p)) {
        const Leaf* l = as_leaf(p);
        for (uint32_t i = 0; i < l->n && !c.full(); i++) c.out[c.n++] = keys(l)[i];
        return;
    }
    const Node* n = as_node(p);
    for (int i = 0; i < kFan && !c.full(); i++) collect_keys(n->c[i], c);
}

void collect_postings(uintptr_t p, const void* field, size_t len, Collector& c) {
    if (!p || c.full()) return;
    if (is_leaf(p)) {
        const Leaf* l = as_leaf(p);
        for (uint32_t i = 0; i < l->n && !c.full(); i++) {
            const Val& v = vals(l)[i];
            if (v.len == len && std::memcmp(v.d, field, len) == 0) c.out[c.n++] = keys(l)[i];
        }
        return;
    }
    const Node* n = as_node(p);
    for (int i = 0; i < kFan && !c.full(); i++) collect_postings(n->c[i], field, len, c);
}

size_t lookup_postings(uintptr_t iroot, const void* field, size_t len, long* out, size_t max) {
    uint64_t h = field_prefix(field, len);
    uintptr_t p = iroot;
    for (int d = 0; p && !is_leaf(p) && d < kIndexPrefixDepth; d++) p = as_node(p)->c[chunk(h, d)];
    Collector c{out, max};
    collect_postings(p, field, len, c);
    return c.n;
}

// ------------------------------------------------------------- write side

struct Ctx {
    uint64_t build;                // identifies nodes created by this write
    std::vector<void*>& retired;   // replaced objects, freed later
    Kind kind;
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
// changed in place and freed immediately. That is also what lets several
// writes in one transaction share one copied path. Anything else is copied
// and the original retired.
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
        uint64_t hh = route(c.kind, keys(o)[i], vals(o)[i]);
        int s = chunk(hh, d);
        n->c[s] = insert(n->c[s], hh, d + 1, keys(o)[i], vals(o)[i], c, unused);
    }
    release(o, c);
    int s = chunk(h, d);
    bool dummy = false;
    n->c[s] = insert(n->c[s], h, d + 1, key, v, c, dummy);
    return reinterpret_cast<uintptr_t>(n);
}

// The caller has already checked that the entry is present.
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

template <class F>
void for_each_entry(uintptr_t p, F&& f) {
    if (!p) return;
    if (is_leaf(p)) {
        const Leaf* l = as_leaf(p);
        for (uint32_t i = 0; i < l->n; i++) f(keys(l)[i], vals(l)[i]);
        return;
    }
    const Node* n = as_node(p);
    for (int i = 0; i < kFan; i++) for_each_entry(n->c[i], f);
}

template <class T>
T* alloc_version(uintptr_t root, uintptr_t iroot, long count, uint64_t seq) {
    T* v = static_cast<T*>(std::malloc(sizeof(T)));
    if (!v) throw std::bad_alloc();
    v->root = root;
    v->iroot = iroot;
    v->count = count;
    v->seq = seq;
    return v;
}

}  // namespace

struct KVStore::Version {
    uintptr_t root;
    uintptr_t iroot;
    long count;
    uint64_t seq;          // commit sequence number, 0 for the empty store
};

// Everything the writer replaced while making one version.
struct KVStore::Garbage {
    Garbage* next;
    uint64_t epoch;
    uint32_t n;
    void* p[];
};

// A version under construction: the roots as they stand after the writes so
// far. Nothing in it is visible to readers until publish().
struct KVStore::Build {
    uintptr_t root;
    uintptr_t iroot;
    long delta = 0;
    uint32_t stamp;                // commit sequence (low 32 bits) written into every row of this build
    Ctx pc, ic;
    Build(const Version* base, uint64_t id, std::vector<void*>& retired)
        : root(base->root), iroot(base->iroot), stamp(static_cast<uint32_t>(base->seq + 1)),
          pc{id, retired, Kind::Primary}, ic{id, retired, Kind::Index} {}
};

KVStore::KVStore() : current_(alloc_version<Version>(0, 0, 0, 0)), batch_(64) {}

KVStore::~KVStore() {
    Version* v = current_.load();
    free_tree(v->root);
    free_tree(v->iroot);
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

// -------------------------------------------------------------- readers

bool KVStore::get(long key, void* value_out, size_t value_out_len) const {
    ReadGuard guard;
    const Version* v = current_.load(std::memory_order_seq_cst);
    const Val* val = find(v->root, key);
    if (!val) return false;
    copy_out(*val, value_out, value_out_len);
    return true;
}

size_t KVStore::size() const {
    ReadGuard guard;
    return static_cast<size_t>(current_.load(std::memory_order_seq_cst)->count);
}

int KVStore::index_get(const void* field_value, size_t field_len, long* keys_out, int max_keys) const {
    if (max_keys <= 0) return 0;
    ReadGuard guard;
    const Version* v = current_.load(std::memory_order_seq_cst);
    if (!v->iroot || field_len != index_len_.load(std::memory_order_relaxed)) return 0;
    return static_cast<int>(lookup_postings(v->iroot, field_value, field_len, keys_out, max_keys));
}

// ------------------------------------------------------------- snapshots

KVStore::Snapshot::Snapshot(Snapshot&& o) noexcept
    : slot_(o.slot_), nbound_(o.nbound_) {
    for (int i = 0; i < kMaxStores; i++) bound_[i] = o.bound_[i];
    o.slot_ = nullptr;
    o.nbound_ = 0;
}

KVStore::Snapshot& KVStore::Snapshot::operator=(Snapshot&& o) noexcept {
    if (this == &o) return *this;
    release();
    slot_ = o.slot_;
    nbound_ = o.nbound_;
    for (int i = 0; i < kMaxStores; i++) bound_[i] = o.bound_[i];
    o.slot_ = nullptr;
    o.nbound_ = 0;
    return *this;
}

KVStore::Snapshot::~Snapshot() { release(); }

void KVStore::Snapshot::release() {
    if (slot_) release_slot(slot_);
    slot_ = nullptr;
    nbound_ = 0;
}

KVStore::Snapshot KVStore::acquire_snapshot() const {
    Snapshot s;
    s.slot_ = claim_slot();
    announce(s.slot_);
    s.bound_[0] = {this, current_.load(std::memory_order_seq_cst)};
    s.nbound_ = 1;
    return s;
}

// The slot was announced when the snapshot was made, so any version that is
// current now is retired no earlier than that epoch and stays pinned.
const KVStore::Version* KVStore::bound(const Snapshot& snap) const {
    for (int i = 0; i < snap.nbound_; i++)
        if (snap.bound_[i].store == this) return snap.bound_[i].version;
    if (snap.nbound_ == Snapshot::kMaxStores) {
        std::fprintf(stderr, "kvstore: a Snapshot can span at most %d stores\n", Snapshot::kMaxStores);
        std::abort();
    }
    const Version* v = current_.load(std::memory_order_seq_cst);
    snap.bound_[snap.nbound_++] = {this, v};
    return v;
}

bool KVStore::get(const Snapshot& snap, long key, void* value_out, size_t value_out_len) const {
    const Val* val = find(bound(snap)->root, key);
    if (!val) return false;
    copy_out(*val, value_out, value_out_len);
    return true;
}

int KVStore::index_get(const Snapshot& snap, const void* field_value, size_t field_len,
                       long* keys_out, int max_keys) const {
    if (max_keys <= 0) return 0;
    const Version* v = bound(snap);
    if (!v->iroot || field_len != index_len_.load(std::memory_order_relaxed)) return 0;
    return static_cast<int>(lookup_postings(v->iroot, field_value, field_len, keys_out, max_keys));
}

size_t KVStore::keys(const Snapshot& snap, long* keys_out, size_t max_keys) const {
    Collector c{keys_out, max_keys};
    collect_keys(bound(snap)->root, c);
    return c.n;
}

// ---------------------------------------------------------------- writers

// Applies one write to the build. The index entry for the row, if the store
// has an index, moves along with it.
void KVStore::apply_put(Build& b, long key, const void* value, size_t len) {
    Val nv;
    if (!make_val(nv, value, len)) throw std::bad_alloc();
    nv.pad = b.stamp;

    char old_field[kInline];
    bool had_posting = false;
    if (has_index_) {
        const Val* old = find(b.root, key);
        if (old && old->len >= index_off_ + index_len_) {
            std::memcpy(old_field, bytes(*old) + index_off_, index_len_);
            had_posting = true;
        }
    }

    bool added = false;
    b.root = insert(b.root, mix(static_cast<uint64_t>(key)), 0, key, nv, b.pc, added);
    if (added) b.delta++;
    if (!has_index_) return;

    bool has_new = len >= index_off_ + index_len_;
    const char* new_field = static_cast<const char*>(value) + index_off_;
    bool same = had_posting && has_new && std::memcmp(old_field, new_field, index_len_) == 0;
    if (same) return;

    if (had_posting) {
        Val ov;
        ov.len = static_cast<uint32_t>(index_len_);
        std::memcpy(ov.d, old_field, index_len_);
        b.iroot = remove(b.iroot, route(Kind::Index, key, ov), 0, key, b.ic);
    }
    if (has_new) {
        Val fv;
        fv.len = static_cast<uint32_t>(index_len_);
        fv.pad = 0;
        std::memcpy(fv.d, new_field, index_len_);
        bool dummy = false;
        b.iroot = insert(b.iroot, route(Kind::Index, key, fv), 0, key, fv, b.ic, dummy);
    }
}

void KVStore::apply_erase(Build& b, long key) {
    const Val* old = find(b.root, key);
    if (!old) return;
    if (has_index_ && old->len >= index_off_ + index_len_) {
        Val ov;
        ov.len = static_cast<uint32_t>(index_len_);
        std::memcpy(ov.d, bytes(*old) + index_off_, index_len_);
        b.iroot = remove(b.iroot, route(Kind::Index, key, ov), 0, key, b.ic);
    }
    b.root = remove(b.root, mix(static_cast<uint64_t>(key)), 0, key, b.pc);
    b.delta--;
}

bool KVStore::put(long key, const void* value, size_t value_len) {
    if (value_len > UINT32_MAX) return false;
    std::lock_guard<std::mutex> g(write_mu_);
    Version* old = current_.load(std::memory_order_relaxed);
    Build b(old, ++build_seq_, pending_);
    try { apply_put(b, key, value, value_len); } catch (const std::bad_alloc&) { return false; }
    publish(old, b);
    return true;
}

bool KVStore::erase(long key) {
    std::lock_guard<std::mutex> g(write_mu_);
    Version* old = current_.load(std::memory_order_relaxed);
    if (!find(old->root, key)) return false;
    Build b(old, ++build_seq_, pending_);
    apply_erase(b, key);
    publish(old, b);
    return true;
}

bool KVStore::commit_writes(const std::vector<StagedWrite>& writes, const std::vector<char>& data,
                            uint64_t start_seq) {
    if (writes.empty()) return true;
    std::lock_guard<std::mutex> g(write_mu_);
    Version* old = current_.load(std::memory_order_relaxed);

    // First committer wins: a row stamped after our start was written by a
    // transaction that committed first. The difference is taken in 32 bits so
    // the stamp may wrap.
    for (const StagedWrite& w : writes) {
        const Val* cur = find(old->root, w.key);
        if (cur && static_cast<int32_t>(cur->pad - static_cast<uint32_t>(start_seq)) > 0) return false;
    }

    Build b(old, ++build_seq_, pending_);
    try {
        for (const StagedWrite& w : writes) apply_put(b, w.key, data.data() + w.offset, w.len);
    } catch (const std::bad_alloc&) {
        return false;
    }
    publish(old, b);
    return true;
}

bool KVStore::create_index(size_t field_offset, size_t field_len) {
    if (field_len == 0 || field_len > kInline) return false;
    std::lock_guard<std::mutex> g(write_mu_);
    if (has_index_) return false;
    Version* old = current_.load(std::memory_order_relaxed);
    Build b(old, ++build_seq_, pending_);
    index_off_ = field_offset;
    index_len_.store(field_len, std::memory_order_relaxed);
    for_each_entry(old->root, [&](long key, const Val& v) {
        if (v.len < field_offset + field_len) return;
        Val fv;
        fv.len = static_cast<uint32_t>(field_len);
        fv.pad = 0;
        std::memcpy(fv.d, bytes(v) + field_offset, field_len);
        bool dummy = false;
        b.iroot = insert(b.iroot, route(Kind::Index, key, fv), 0, key, fv, b.ic, dummy);
    });
    has_index_ = true;
    publish(old, b);
    return true;
}

// One atomic store makes the new version visible. The replaced objects are
// stamped with the epoch of the swap; a reader that could still hold the old
// version announced an epoch no later than that stamp.
void KVStore::publish(Version* old, const Build& b) {
    Version* nv = alloc_version<Version>(b.root, b.iroot, old->count + b.delta, old->seq + 1);
    current_.store(nv, std::memory_order_seq_cst);
    commit_seq_.store(nv->seq, std::memory_order_release);
    pending_.push_back(old);

    Garbage* g = static_cast<Garbage*>(std::malloc(sizeof(Garbage) + pending_.size() * sizeof(void*)));
    if (!g) throw std::bad_alloc();
    g->next = nullptr;
    g->epoch = g_epoch.load(std::memory_order_relaxed);
    g->n = static_cast<uint32_t>(pending_.size());
    std::memcpy(g->p, pending_.data(), pending_.size() * sizeof(void*));
    pending_.clear();
    if (gc_tail_) gc_tail_->next = g; else gc_head_ = g;
    gc_tail_ = g;
    g_epoch.fetch_add(1, std::memory_order_acq_rel);

    uint64_t retired = retired_.fetch_add(1, std::memory_order_relaxed) + 1;
    uint64_t backlog = retired - freed_.load(std::memory_order_relaxed);
    if (backlog > backlog_peak_.load(std::memory_order_relaxed))
        backlog_peak_.store(backlog, std::memory_order_relaxed);
    reclaim_pass();
}

// Frees up to limit retired versions, oldest first, and only those that no
// reader can still be using. It never waits: a version that is still pinned
// just stays on the list for a later pass.
size_t KVStore::free_records(uint64_t limit) {
    if (!gc_head_) return 0;
    uint64_t floor = min_active_epoch();
    if (gc_head_->epoch >= floor) return 0;

    auto t0 = std::chrono::steady_clock::now();
    uint64_t n = 0;
    while (gc_head_ && gc_head_->epoch < floor && n < limit) {
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
    return n;
}

void KVStore::reclaim_pass() {
    int batch = batch_.load(std::memory_order_relaxed);
    if (batch <= 0) return;
    free_records(static_cast<uint64_t>(batch));
}

size_t KVStore::reclaim_all() {
    std::lock_guard<std::mutex> g(write_mu_);
    return free_records(~uint64_t(0));
}

// ----------------------------------------------------------- transactions

Transaction::Transaction(KVStore& store)
    : store_(store), start_seq_(store.commit_seq_.load(std::memory_order_acquire)) {}

Transaction::Transaction(KVStore& store, const KVStore::Snapshot& start)
    : store_(store), start_seq_(store.bound(start)->seq) {}

void Transaction::put(long key, const void* value, size_t value_len) {
    writes_.push_back({key, data_.size(), value_len});
    const char* p = static_cast<const char*>(value);
    data_.insert(data_.end(), p, p + value_len);
}

bool Transaction::commit() {
    bool ok = store_.commit_writes(writes_, data_, start_seq_);
    writes_.clear();
    data_.clear();
    start_seq_ = store_.commit_seq_.load(std::memory_order_acquire);
    return ok;
}
