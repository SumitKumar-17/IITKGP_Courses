// kvstore.h (Part A)
//
// Open-addressing hash table, linear probing, one 64-byte slot per key with
// the value stored inline (values over 48 bytes spill to the heap).
//
// The synchronisation is chosen at build time with -DNOVA_SYNC=<n>:
//   0  none (single_thread)    1  std::mutex (global_lock)
//   2  std::shared_mutex (rwlock)
// The table code is identical in all three; only the guard type differs.
#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#ifndef NOVA_SYNC
#define NOVA_SYNC 0
#endif

// Cache-line alignment of the store object and its fields. -DNOVA_NO_ALIGN builds the
// unaligned layout, which exists only to reproduce the layout-sensitivity measurement
// (tools/layout_sensitivity.sh).
#ifdef NOVA_NO_ALIGN
#define NOVA_CACHELINE
#else
#define NOVA_CACHELINE alignas(64)
#endif

class NOVA_CACHELINE KVStore {
public:
    // Appendix A interface in this part: put, get, erase. Methods marked "extra" are additions
    // for the tests and the workload generator, which is written against the later parts' API.
    KVStore();
    ~KVStore();
    KVStore(const KVStore&) = delete;
    KVStore& operator=(const KVStore&) = delete;

    // Stores or overwrites key. Returns false only if the allocation fails.
    bool put(long key, const void* value, size_t value_len);
    // Copies min(stored length, value_out_len) bytes. Returns true if found.
    bool get(long key, void* value_out, size_t value_out_len) const;
    // Returns true if the key was present and is now removed.
    bool erase(long key);

    // extra, not in Appendix A
    size_t size() const { return size_; }

    // The workload generator is written against the full API, so these
    // exist here to let it link. Secondary indexes arrive in Part C.
    bool create_index(size_t field_offset, size_t field_len);
    int index_get(const void* field_value, size_t field_len, long* keys_out, int max_keys) const;

private:
    static constexpr size_t kInline = 48;
    enum : uint32_t { kEmpty = 0, kInlineVal = 1, kHeapVal = 2 };

    struct alignas(64) Slot {
        long key;
        uint32_t len;
        uint32_t state;
        union {
            char inl[kInline];
            char* heap;
        };
    };

    size_t home(long key) const;
    Slot* find(long key) const;
    bool store(Slot& s, const void* value, size_t len);
    void release(Slot& s);
    bool grow();

    // Layout matters to the lock benchmarks. The table header (read on every get)
    // and the lock (written on every get) are put on separate cache lines, and the
    // object itself is 64-byte aligned. Without that, where the lock word lands
    // relative to the header depends on the size of the process environment, and
    // the same binary measured anywhere between 8M and 26M reads/s.
    NOVA_CACHELINE Slot* slots_;
    size_t cap_;
    size_t size_;

#if NOVA_SYNC == 1
    NOVA_CACHELINE mutable std::mutex mu_;
    using ReadGuard = std::lock_guard<std::mutex>;
    using WriteGuard = std::lock_guard<std::mutex>;
#elif NOVA_SYNC == 2
    NOVA_CACHELINE mutable std::shared_mutex mu_;
    using ReadGuard = std::shared_lock<std::shared_mutex>;
    using WriteGuard = std::unique_lock<std::shared_mutex>;
#endif
};

// In Part A a transaction only batches puts and applies them in order on
// commit. It is not atomic towards readers; that is added in Part C.
class Transaction {
public:
    explicit Transaction(KVStore& store) : store_(store) {}
    ~Transaction() = default;
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void put(long key, const void* value, size_t value_len) {
        staged_.emplace_back(key, std::string(static_cast<const char*>(value), value_len));
    }
    bool commit() {
        bool ok = true;
        for (auto& w : staged_) ok &= store_.put(w.first, w.second.data(), w.second.size());
        staged_.clear();
        return ok;
    }

private:
    KVStore& store_;
    std::vector<std::pair<long, std::string>> staged_;
};
