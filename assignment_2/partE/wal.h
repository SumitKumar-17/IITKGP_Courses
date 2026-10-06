// wal.h (Part E): a write-ahead log for the store.
//
// One record per commit (a transaction, a single put or erase, or the creation
// of the index). A record is checksummed as a whole and carries a log sequence
// number, so recovery can tell a complete record from a torn one, and notice a
// record that is missing or repeated. Replay stops at the first record that does
// not check out and drops everything after it.
//
// File layout:   [16-byte file header] [record] [record] ...
// Record:        [24-byte header][ops...]
//   header       magic u32 | payload_len u32 | lsn u64 | nops u32 | crc32c u32
//   op           type u8 | key i64 | vlen u32 | vlen bytes     (type 1 = put, 2 = erase)
//                type 3 = create index: key = field offset, vlen = field length, no bytes
// The crc covers the header (with the crc field zero) and the payload.
// The file is preallocated, so the unused tail reads as zeros; a zero header
// means "end of log", anything else that fails the checks means damage.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class Durability {
    Async,   // commit returns at once; a background thread writes and syncs every flush_interval_ms
    Write,   // commit returns once the record is in the OS page cache: survives a process crash
    Sync,    // commit returns once the record is on stable storage (group commit): survives power loss
};

struct WalOptions {
    Durability durability = Durability::Write;
    int group_delay_us = 0;        // Sync: how long a leader waits for more records before flushing
    int flush_interval_ms = 1;     // Async: write+sync period. Write: background sync period, 0 = never
    size_t prealloc_bytes = 64u << 20;
};

struct WalOp {
    uint8_t type;                  // 1 put, 2 erase, 3 create index
    long key;                      // for type 3: field offset
    const void* value;             // for type 3: unused
    size_t len;                    // for type 3: field length
};

struct WalReplayInfo {
    uint64_t records = 0;          // complete records applied
    uint64_t ops = 0;
    uint64_t valid_bytes = 0;      // length of the good prefix, header included
    uint64_t discarded_bytes = 0;  // bytes after it that were dropped
    bool clean_end = true;         // false if replay stopped at damage rather than at the end of the log
    std::string stopped;           // why it stopped, when it did not reach the end
    // Complete, checksummed records found AFTER the damage. Zero means a torn tail (a crash
    // in the middle of an append). Above zero means damage in the middle of the log: those
    // later records were dropped too, and a production system should refuse to start and
    // repair from a replica instead of carrying on.
    uint64_t records_after_damage = 0;
};

struct WalStats {
    uint64_t records = 0, bytes = 0;
    uint64_t write_calls = 0, syncs = 0;
    uint64_t sync_ns_total = 0, sync_ns_max = 0;
    uint64_t records_per_sync_max = 0;
    uint64_t durable_lsn = 0, appended_lsn = 0;
};

class Wal {
public:
    // Called once per record during replay, with the ops of that record.
    using ReplayFn = std::function<void(const WalOp* ops, size_t n)>;

    // Opens or creates the log, replays it through `apply`, truncates a damaged
    // tail, and starts accepting appends. Returns null and fills `err` on failure.
    static std::unique_ptr<Wal> open(const std::string& path, const WalOptions& opt, const ReplayFn& apply,
                                     WalReplayInfo& info, std::string& err);
    ~Wal();
    Wal(const Wal&) = delete;
    Wal& operator=(const Wal&) = delete;

    // Appends one record. Callers must be serialised (the store holds its writer
    // lock), so records land in the order the versions are published.
    // Returns the record's lsn, or 0 if the log has failed.
    uint64_t append(const WalOp* ops, size_t n);

    // Blocks until the record is as safe as the durability level promises.
    // Called after the writer lock is released, so other commits can proceed and
    // join the same sync.
    void wait_durable(uint64_t lsn);

    WalStats stats() const;
    bool failed() const { return failed_.load(); }

private:
    Wal() = default;
    bool write_out(const char* data, size_t n);
    void sync_file();
    void flush_buffer();
    void background();

    int fd_ = -1;
    WalOptions opt_;
    std::string path_;
    std::atomic<bool> failed_{false};

    std::mutex io_mu_;                  // serialises writes and syncs of the file
    std::mutex buf_mu_;                 // guards active_, written_, next_lsn_, appended_
    std::vector<char> active_;
    uint64_t next_lsn_ = 1;
    uint64_t appended_ = 0;
    uint64_t file_end_ = 0;             // where the next byte goes
    uint64_t allocated_ = 0;

    std::mutex dur_mu_;                 // guards the group-commit state below
    std::condition_variable dur_cv_;
    bool flushing_ = false;
    uint64_t durable_ = 0;
    std::vector<char> flushing_buf_;

    std::thread bg_;
    std::atomic<bool> stop_{false};
    std::condition_variable bg_cv_;
    std::mutex bg_mu_;

    std::atomic<uint64_t> records_{0}, bytes_{0}, write_calls_{0}, syncs_{0};
    std::atomic<uint64_t> sync_ns_total_{0}, sync_ns_max_{0}, per_sync_max_{0};
    uint64_t last_synced_appended_ = 0;
};
