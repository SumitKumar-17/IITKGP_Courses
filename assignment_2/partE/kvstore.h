// kvstore.h (Part E)
//
// Same RCU store as Part B, extended with
//   * Transaction: staged writes published as ONE new version,
//   * a secondary index (a second trie in the same Version, so a commit
//     publishes table and index together),
//   * Snapshot: a move-only handle that pins versions for many calls,
//   * keys(): enumerate the primary keys of a snapshot.
//
// A version is {primary trie, index trie, count}. Writers copy the path they
// change in either trie, then publish with one atomic store of current_.
// Readers announce an epoch, load current_ once and never take a lock.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "wal.h"

namespace nova_detail { struct EpochSlot; }

struct StagedWrite {
    long key;
    size_t offset;   // into the transaction's byte buffer
    size_t len;
};

class alignas(64) KVStore {
public:
    // Appendix A interface: put, get, erase, set_reclaim_batch, create_index, index_get, Snapshot,
    // acquire_snapshot and the snapshot overloads of get, index_get and keys. Methods marked "extra"
    // are additions: the statistics and reclaim_all() for the tests and benchmarks, open_wal() for Part E.
    KVStore();
    ~KVStore();
    KVStore(const KVStore&) = delete;
    KVStore& operator=(const KVStore&) = delete;

    bool put(long key, const void* value, size_t value_len);
    bool get(long key, void* value_out, size_t value_out_len) const;
    bool erase(long key);

    // Most retired versions one reclamation pass may free; 0 turns it off.
    void set_reclaim_batch(int n);

    struct ReclaimStats {
        uint64_t retired;
        uint64_t freed;
        uint64_t backlog_peak;
        uint64_t passes;
        uint64_t max_pass_freed;
        uint64_t max_pass_ns;
    };
    // extra, not in Appendix A
    ReclaimStats reclaim_stats() const;
    // Frees every retired version no reader can still need, with no bound. Meant for
    // tests and for the stop-the-world comparison in Part D; the normal path is the
    // bounded pass. Returns how many versions it freed.
    // extra, not in Appendix A
    size_t reclaim_all();

    // Turns on the write-ahead log. An existing log is replayed into the store first
    // (so call this on an empty store), then every commit is logged before it is
    // published. Returns false and fills *error if the log cannot be used. Reads
    // never touch the log.
    // extra, not in Appendix A
    bool open_wal(const std::string& path, const WalOptions& options = WalOptions(),
                  WalReplayInfo* replay = nullptr, std::string* error = nullptr);
    // extra, not in Appendix A
    WalStats wal_stats() const;

    // extra, not in Appendix A
    size_t size() const;

    // Builds a hash index over the bytes [field_offset, field_offset + field_len)
    // of every stored value, and keeps it up to date from then on. One index per
    // store, field_len at most 40. Returns false if one exists already.
    bool create_index(size_t field_offset, size_t field_len);
    // Primary keys whose indexed field equals the given bytes (up to max_keys).
    int index_get(const void* field_value, size_t field_len, long* keys_out, int max_keys) const;

    struct Version;

    // A handle on published versions. While it is alive the versions it has
    // seen stay pinned against reclamation, across any number of calls.
    //
    // It pins the store it came from at acquire time. Used with another store,
    // it pins that store's current version the first time it is used there and
    // keeps that one from then on (a join over two tables uses one handle for
    // both). Up to four stores per handle. Use one handle from one thread.
    class Snapshot {
    public:
        Snapshot(Snapshot&&) noexcept;
        Snapshot& operator=(Snapshot&&) noexcept;
        ~Snapshot();
        Snapshot(const Snapshot&) = delete;
        Snapshot& operator=(const Snapshot&) = delete;

    private:
        friend class KVStore;
        Snapshot() = default;
        void release();

        static constexpr int kMaxStores = 4;
        struct Binding { const KVStore* store; const Version* version; };

        nova_detail::EpochSlot* slot_ = nullptr;
        mutable Binding bound_[kMaxStores] = {};
        mutable int nbound_ = 0;
    };

    Snapshot acquire_snapshot() const;
    bool get(const Snapshot& snap, long key, void* value_out, size_t value_out_len) const;
    int index_get(const Snapshot& snap, const void* field_value, size_t field_len,
                  long* keys_out, int max_keys) const;
    size_t keys(const Snapshot& snap, long* keys_out, size_t max_keys) const;

private:
    friend class Transaction;
    struct Garbage;
    struct Build;

    const Version* bound(const Snapshot& snap) const;
    void apply_put(Build& b, long key, const void* value, size_t len);
    void apply_erase(Build& b, long key);
    void build_index(Build& b, size_t field_offset, size_t field_len);
    bool commit_writes(const std::vector<StagedWrite>& writes, const std::vector<char>& data,
                       uint64_t start_seq);
    void publish(Version* old, const Build& b);
    size_t free_records(uint64_t limit);
    void reclaim_pass();

    // Layout: readers only ever touch current_ (and index_len_, written once), so
    // they get a cache line to themselves. Everything the writer changes on each
    // publish (lock, retire list, counters) lives on other lines; otherwise every
    // put would also invalidate the line readers load the version pointer from.
    alignas(64) std::atomic<Version*> current_;
    std::atomic<size_t> index_len_{0};   // read by index_get without the writer lock

    alignas(64) std::mutex write_mu_;

    // Writer-side state, guarded by write_mu_.
    std::vector<void*> pending_;
    Garbage* gc_head_ = nullptr;
    Garbage* gc_tail_ = nullptr;
    uint64_t build_seq_ = 0;
    size_t index_off_ = 0;
    bool has_index_ = false;

    std::atomic<uint64_t> commit_seq_{0};   // sequence number of the newest published version
    std::unique_ptr<Wal> wal_;              // null until open_wal()

    std::atomic<int> batch_;
    std::atomic<uint64_t> retired_{0}, freed_{0}, backlog_peak_{0};
    std::atomic<uint64_t> passes_{0}, max_pass_freed_{0}, max_pass_ns_{0};
};

// put() only stages; commit() applies every staged write to one new version
// and publishes it with a single pointer swap, so a reader sees all of them or
// none. A Transaction destroyed without commit() drops what it staged.
//
// A transaction starts at a commit sequence number: the newest version when it
// was created, or the version of the Snapshot passed in (use that form when the
// writes depend on what was read through the snapshot). commit() applies the
// first-committer-wins rule: if any key it writes was committed by someone else
// after that start, nothing is published and commit() returns false.
class Transaction {
public:
    explicit Transaction(KVStore& store);
    Transaction(KVStore& store, const KVStore::Snapshot& start);
    ~Transaction() = default;
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void put(long key, const void* value, size_t value_len);
    bool commit();

private:
    KVStore& store_;
    std::vector<StagedWrite> writes_;
    std::vector<char> data_;
    uint64_t start_seq_;
};
