// kvstore.h (Part B)
//
// RCU version of the store. The table is an immutable radix trie (64-way
// inner nodes, small leaves holding a few inline entries). A write copies the
// nodes on the path to the changed leaf, links them into a new Version, and
// publishes it with one atomic store of current_. A read announces itself in
// the epoch table, loads current_ once, and works on that version only.
// Replaced nodes go on a retire list and are freed by bounded reclamation
// passes once no reader can still be inside an older epoch.
//
// There is no lock on the read path. Writers are serialised by a mutex that
// readers never touch; the design target is a single writer thread.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class KVStore {
public:
    KVStore();
    ~KVStore();
    KVStore(const KVStore&) = delete;
    KVStore& operator=(const KVStore&) = delete;

    bool put(long key, const void* value, size_t value_len);
    bool get(long key, void* value_out, size_t value_out_len) const;
    bool erase(long key);

    // Most retired versions one reclamation pass may free. 0 turns
    // reclamation off (nothing is ever freed until the store is destroyed).
    void set_reclaim_batch(int n);

    struct ReclaimStats {
        uint64_t retired;         // versions retired so far
        uint64_t freed;           // versions freed so far
        uint64_t backlog_peak;    // largest retired - freed seen
        uint64_t passes;          // passes that freed something
        uint64_t max_pass_freed;  // most versions freed by a single pass
        uint64_t max_pass_ns;     // longest single pass
    };
    ReclaimStats reclaim_stats() const;

    size_t size() const;

    // The workload generator is written against the full API, so these exist
    // here to let it link. Secondary indexes arrive in Part C.
    bool create_index(size_t field_offset, size_t field_len);
    int index_get(const void* field_value, size_t field_len, long* keys_out, int max_keys) const;

private:
    struct Version;
    struct Garbage;

    void publish(Version* old, uintptr_t root, long delta);
    void reclaim_pass();

    std::atomic<Version*> current_;
    std::mutex write_mu_;

    // Writer-side state, guarded by write_mu_.
    std::vector<void*> pending_;
    Garbage* gc_head_ = nullptr;
    Garbage* gc_tail_ = nullptr;
    uint64_t build_seq_ = 0;

    std::atomic<int> batch_;
    std::atomic<uint64_t> retired_{0}, freed_{0}, backlog_peak_{0};
    std::atomic<uint64_t> passes_{0}, max_pass_freed_{0}, max_pass_ns_{0};
};

// In Part B a transaction still just batches puts and applies them in order
// on commit. It becomes atomic towards readers in Part C.
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
