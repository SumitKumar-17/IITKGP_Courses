// kvstore.cpp (Part A)
#include "kvstore.h"

#include <cstdlib>
#include <cstring>
#include <new>

namespace {

inline uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

constexpr size_t kInitialCap = 1 << 12;

}  // namespace

#if NOVA_SYNC == 0
#define READ_GUARD
#define WRITE_GUARD
#else
#define READ_GUARD  ReadGuard guard(mu_)
#define WRITE_GUARD WriteGuard guard(mu_)
#endif

KVStore::KVStore() : cap_(kInitialCap), size_(0) {
    slots_ = static_cast<Slot*>(std::aligned_alloc(64, cap_ * sizeof(Slot)));
    if (!slots_) throw std::bad_alloc();
    std::memset(slots_, 0, cap_ * sizeof(Slot));
}

KVStore::~KVStore() {
    for (size_t i = 0; i < cap_; i++) release(slots_[i]);
    std::free(slots_);
}

size_t KVStore::home(long key) const { return mix(static_cast<uint64_t>(key)) & (cap_ - 1); }

KVStore::Slot* KVStore::find(long key) const {
    for (size_t i = home(key);; i = (i + 1) & (cap_ - 1)) {
        Slot& s = slots_[i];
        if (s.state == kEmpty) return nullptr;
        if (s.key == key) return &s;
    }
}

void KVStore::release(Slot& s) {
    if (s.state == kHeapVal) std::free(s.heap);
    s.state = kEmpty;
}

bool KVStore::store(Slot& s, const void* value, size_t len) {
    if (len <= kInline) {
        if (s.state == kHeapVal) std::free(s.heap);
        std::memcpy(s.inl, value, len);
        s.state = kInlineVal;
    } else {
        char* buf = s.state == kHeapVal && s.len >= len
                        ? s.heap
                        : static_cast<char*>(std::malloc(len));
        if (!buf) return false;
        if (s.state == kHeapVal && buf != s.heap) std::free(s.heap);
        std::memcpy(buf, value, len);
        s.heap = buf;
        s.state = kHeapVal;
    }
    s.len = static_cast<uint32_t>(len);
    return true;
}

bool KVStore::grow() {
    size_t old_cap = cap_;
    Slot* old = slots_;
    size_t new_cap = old_cap * 2;
    Slot* fresh = static_cast<Slot*>(std::aligned_alloc(64, new_cap * sizeof(Slot)));
    if (!fresh) return false;
    std::memset(fresh, 0, new_cap * sizeof(Slot));
    slots_ = fresh;
    cap_ = new_cap;
    for (size_t i = 0; i < old_cap; i++) {
        if (old[i].state == kEmpty) continue;
        size_t j = home(old[i].key);
        while (slots_[j].state != kEmpty) j = (j + 1) & (cap_ - 1);
        slots_[j] = old[i];
    }
    std::free(old);
    return true;
}

bool KVStore::put(long key, const void* value, size_t value_len) {
    WRITE_GUARD;
    if (Slot* s = find(key)) return store(*s, value, value_len);
    if ((size_ + 1) * 2 > cap_ && !grow()) return false;
    size_t i = home(key);
    while (slots_[i].state != kEmpty) i = (i + 1) & (cap_ - 1);
    Slot& s = slots_[i];
    s.key = key;
    if (!store(s, value, value_len)) return false;
    size_++;
    return true;
}

bool KVStore::get(long key, void* value_out, size_t value_out_len) const {
    READ_GUARD;
    const Slot* s = find(key);
    if (!s) return false;
    size_t n = s->len < value_out_len ? s->len : value_out_len;
    std::memcpy(value_out, s->state == kHeapVal ? s->heap : s->inl, n);
    return true;
}

// Backward-shift deletion keeps probe chains intact without tombstones.
bool KVStore::erase(long key) {
    WRITE_GUARD;
    Slot* hit = find(key);
    if (!hit) return false;
    size_t hole = hit - slots_;
    release(slots_[hole]);
    size_t i = hole;
    for (;;) {
        i = (i + 1) & (cap_ - 1);
        if (slots_[i].state == kEmpty) break;
        size_t h = home(slots_[i].key);
        bool movable = i > hole ? (h <= hole || h > i) : (h <= hole && h > i);
        if (!movable) continue;
        slots_[hole] = slots_[i];
        slots_[i].state = kEmpty;
        hole = i;
    }
    size_--;
    return true;
}

bool KVStore::create_index(size_t, size_t) { return false; }

int KVStore::index_get(const void*, size_t, long*, int) const { return 0; }
