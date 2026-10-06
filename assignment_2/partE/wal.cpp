// wal.cpp (Part E)
#include "wal.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>

#if defined(__SSE4_2__)
#include <nmmintrin.h>
#endif

namespace {

constexpr uint32_t kMagic = 0x4C415741;          // "AWAL"
constexpr char kFileMagic[8] = {'N', 'O', 'V', 'A', 'W', 'A', 'L', '1'};
constexpr size_t kFileHeader = 16;
constexpr size_t kRecHeader = 24;
constexpr uint32_t kMaxPayload = 1u << 30;

#if defined(__SSE4_2__)
uint32_t crc32c(uint32_t crc, const void* data, size_t n) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    uint64_t c = crc;
    while (n >= 8) { uint64_t v; std::memcpy(&v, p, 8); c = _mm_crc32_u64(c, v); p += 8; n -= 8; }
    uint32_t c32 = static_cast<uint32_t>(c);
    while (n--) c32 = _mm_crc32_u8(c32, *p++);
    return c32;
}
#else
uint32_t crc32c(uint32_t crc, const void* data, size_t n) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0x82F63B78u & (0u - (c & 1)));
            table[i] = c;
        }
        ready = true;
    }
    const unsigned char* p = static_cast<const unsigned char*>(data);
    while (n--) crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return crc;
}
#endif

struct RecHeader {
    uint32_t magic;
    uint32_t payload_len;
    uint64_t lsn;
    uint32_t nops;
    uint32_t crc;
};
static_assert(sizeof(RecHeader) == kRecHeader, "header must be 24 bytes");

uint32_t record_crc(RecHeader h, const char* payload) {
    h.crc = 0;
    uint32_t c = crc32c(~0u, &h, sizeof h);
    return ~crc32c(c, payload, h.payload_len);
}

uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void put_bytes(std::vector<char>& b, const void* p, size_t n) {
    const char* c = static_cast<const char*>(p);
    b.insert(b.end(), c, c + n);
}

bool all_zero(const char* p, size_t n) {
    for (size_t i = 0; i < n; i++) if (p[i]) return false;
    return true;
}

// Parses the ops of one record and checks them against the header.
bool parse_ops(const char* payload, uint32_t len, uint32_t nops, std::vector<WalOp>& out) {
    out.clear();
    size_t pos = 0;
    for (uint32_t i = 0; i < nops; i++) {
        if (pos + 13 > len) return false;
        WalOp op{};
        op.type = static_cast<uint8_t>(payload[pos]);
        int64_t key;
        uint32_t vlen;
        std::memcpy(&key, payload + pos + 1, 8);
        std::memcpy(&vlen, payload + pos + 9, 4);
        pos += 13;
        op.key = static_cast<long>(key);
        op.len = vlen;
        if (op.type == 1) {
            if (pos + vlen > len) return false;
            op.value = payload + pos;
            pos += vlen;
        } else if (op.type == 2) {
            if (vlen != 0) return false;
            op.value = nullptr;
        } else if (op.type == 3) {
            op.value = nullptr;
        } else {
            return false;
        }
        out.push_back(op);
    }
    return pos == len;
}

// Counts complete, checksummed records that start somewhere in [from, size). Used only
// after replay has stopped at damage, to tell a torn tail from damage in the middle.
uint64_t count_valid_records_after(const char* base, uint64_t from, uint64_t size) {
    uint64_t found = 0;
    std::vector<WalOp> scratch;
    uint64_t q = from;
    while (q + kRecHeader <= size) {
        const void* hit = memchr(base + q, static_cast<int>(kMagic & 0xff), size - q - kRecHeader + 1);
        if (!hit) break;
        q = static_cast<uint64_t>(static_cast<const char*>(hit) - base);
        RecHeader h;
        std::memcpy(&h, base + q, sizeof h);
        if (h.magic == kMagic && h.payload_len <= kMaxPayload && q + kRecHeader + h.payload_len <= size &&
            record_crc(h, base + q + kRecHeader) == h.crc &&
            parse_ops(base + q + kRecHeader, h.payload_len, h.nops, scratch)) {
            found++;
            q += kRecHeader + h.payload_len;
        } else {
            q++;
        }
    }
    return found;
}

}  // namespace

std::unique_ptr<Wal> Wal::open(const std::string& path, const WalOptions& opt, const ReplayFn& apply,
                               WalReplayInfo& info, std::string& err) {
    std::unique_ptr<Wal> w(new Wal());
    w->opt_ = opt;
    w->path_ = path;
    w->fd_ = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (w->fd_ < 0) { err = std::string("open: ") + std::strerror(errno); return nullptr; }

    struct stat st;
    if (fstat(w->fd_, &st) != 0) { err = std::string("fstat: ") + std::strerror(errno); return nullptr; }
    uint64_t size = static_cast<uint64_t>(st.st_size);
    info = WalReplayInfo{};
    uint64_t pos = kFileHeader;
    uint64_t expected = 1;

    if (size == 0) {
        // A new file: make its directory entry durable too, or a power cut could lose the
        // file itself however carefully its contents are synced.
        std::string dirpath = path.find('/') == std::string::npos ? "." : path.substr(0, path.rfind('/'));
        int dfd = ::open(dirpath.c_str(), O_RDONLY | O_DIRECTORY);
        if (dfd >= 0) { fsync(dfd); ::close(dfd); }
        char hdr[kFileHeader] = {};
        std::memcpy(hdr, kFileMagic, 8);
        if (pwrite(w->fd_, hdr, sizeof hdr, 0) != static_cast<ssize_t>(sizeof hdr)) {
            err = std::string("write header: ") + std::strerror(errno);
            return nullptr;
        }
        w->allocated_ = kFileHeader;
    } else {
        if (size < kFileHeader) { err = "file too short to be a log"; return nullptr; }
        void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, w->fd_, 0);
        if (map == MAP_FAILED) { err = std::string("mmap: ") + std::strerror(errno); return nullptr; }
        const char* base = static_cast<const char*>(map);
        if (std::memcmp(base, kFileMagic, 8) != 0) {
            munmap(map, size);
            err = "not a NOVA-KV log (bad file header)";
            return nullptr;
        }
        // Pass one only validates, so that a refusal happens before anything has been applied.
        std::vector<WalOp> ops;
        while (true) {
            if (pos + kRecHeader > size) {
                if (!all_zero(base + pos, size - pos)) {
                    info.clean_end = false;
                    info.stopped = "truncated record header";
                }
                break;
            }
            RecHeader h;
            std::memcpy(&h, base + pos, sizeof h);
            if (all_zero(base + pos, kRecHeader)) break;       // preallocated tail: the log ends here
            if (h.magic != kMagic) { info.clean_end = false; info.stopped = "bad record magic"; break; }
            if (h.payload_len > kMaxPayload || pos + kRecHeader + h.payload_len > size) {
                info.clean_end = false; info.stopped = "record runs past the end of the file"; break;
            }
            const char* payload = base + pos + kRecHeader;
            if (record_crc(h, payload) != h.crc) { info.clean_end = false; info.stopped = "checksum mismatch"; break; }
            if (h.lsn != expected) {
                info.clean_end = false;
                info.stopped = "sequence number " + std::to_string(h.lsn) + ", expected " + std::to_string(expected);
                break;
            }
            if (!parse_ops(payload, h.payload_len, h.nops, ops)) {
                info.clean_end = false; info.stopped = "malformed operations"; break;
            }
            info.records++;
            info.ops += h.nops;
            pos += kRecHeader + h.payload_len;
            expected++;
        }
        if (!info.clean_end) {
            uint64_t last = size;
            while (last > pos && base[last - 1] == 0) last--;
            info.discarded_bytes = last - pos;
            info.records_after_damage = count_valid_records_after(base, pos + 1, size);
            if (info.records_after_damage > 0 && !opt.repair) {
                munmap(map, size);
                err = "damage in the middle of the log at byte " + std::to_string(pos) + " (" + info.stopped + "): " +
                      std::to_string(info.records_after_damage) + " valid records follow it and would be discarded; " +
                      "the file was left untouched (set WalOptions::repair to discard them)";
                return nullptr;
            }
        }
        // Pass two applies the records that passed.
        {
            uint64_t at = kFileHeader;
            for (uint64_t r = 0; r < info.records; r++) {
                RecHeader h;
                std::memcpy(&h, base + at, sizeof h);
                parse_ops(base + at + kRecHeader, h.payload_len, h.nops, ops);
                apply(ops.data(), ops.size());
                at += kRecHeader + h.payload_len;
            }
        }
        munmap(map, size);
        w->allocated_ = size;
        if (!info.clean_end) {
            // Drop the damaged tail. Space beyond is preallocated again on the next write.
            if (ftruncate(w->fd_, static_cast<off_t>(pos)) != 0) {
                err = std::string("ftruncate: ") + std::strerror(errno);
                return nullptr;
            }
            w->allocated_ = pos;
        }
    }
    info.valid_bytes = pos;
    w->file_end_ = pos;
    w->next_lsn_ = expected;
    w->appended_ = expected - 1;
    w->durable_ = expected - 1;
    w->last_synced_appended_ = expected - 1;
    w->synced_end_ = pos;
    if (fdatasync(w->fd_) != 0) { err = std::string("fdatasync: ") + std::strerror(errno); return nullptr; }

    bool need_bg = opt.durability == Durability::Async ||
                   (opt.durability == Durability::Write && opt.flush_interval_ms > 0);
    w->ready_ = true;
    if (need_bg) w->bg_ = std::thread(&Wal::background, w.get());
    return w;
}

Wal::~Wal() {
    stop_ = true;
    bg_cv_.notify_all();
    if (bg_.joinable()) bg_.join();
    if (fd_ >= 0) {
        if (ready_ && !failed_.load()) {
            flush_buffer();
            std::lock_guard<std::mutex> g(io_mu_);
            if (ftruncate(fd_, static_cast<off_t>(file_end_)) != 0) { /* the tail is zeros anyway */ }
            fdatasync(fd_);
        }
        ::close(fd_);
    }
}

bool Wal::write_out(const char* data, size_t n) {
    std::lock_guard<std::mutex> g(io_mu_);
    if (n == 0) return true;
    if (file_end_ + n > allocated_) {
        size_t grow = std::max(opt_.prealloc_bytes, n);
        if (posix_fallocate(fd_, static_cast<off_t>(allocated_), static_cast<off_t>(grow)) != 0) {
            failed_ = true;
            return false;
        }
        allocated_ += grow;
    }
    size_t done = 0;
    while (done < n) {
        ssize_t r = pwrite(fd_, data + done, n - done, static_cast<off_t>(file_end_ + done));
        if (r < 0) {
            if (errno == EINTR) continue;
            failed_ = true;
            return false;
        }
        done += static_cast<size_t>(r);
    }
    file_end_ += n;
    write_calls_++;
    return true;
}

void Wal::sync_file() {
    // No lock here: fdatasync only has to cover writes that finished before it started, and
    // holding the file lock would make every commit's write() wait behind it.
    uint64_t covered;
    {
        std::lock_guard<std::mutex> g(io_mu_);
        covered = file_end_;           // everything written so far is covered by the sync below
    }
    uint64_t t0 = now_ns();
    int rc = fdatasync(fd_);
    uint64_t d = now_ns() - t0;
    if (rc == 0) {
        uint64_t prev = synced_end_.load();
        while (covered > prev && !synced_end_.compare_exchange_weak(prev, covered)) {}
    }
    if (rc != 0) failed_ = true;
    syncs_++;
    sync_ns_total_ += d;
    uint64_t prev = sync_ns_max_.load();
    while (d > prev && !sync_ns_max_.compare_exchange_weak(prev, d)) {}
}

uint64_t Wal::append(const WalOp* ops, size_t n) {
    if (failed_.load()) return 0;
    std::lock_guard<std::mutex> g(buf_mu_);
    size_t start = active_.size();
    active_.resize(start + kRecHeader);
    for (size_t i = 0; i < n; i++) {
        char t = static_cast<char>(ops[i].type);
        int64_t key = ops[i].key;
        uint32_t vlen = static_cast<uint32_t>(ops[i].len);
        put_bytes(active_, &t, 1);
        put_bytes(active_, &key, 8);
        put_bytes(active_, &vlen, 4);
        if (ops[i].type == 1) put_bytes(active_, ops[i].value, ops[i].len);
    }
    RecHeader h;
    h.magic = kMagic;
    h.payload_len = static_cast<uint32_t>(active_.size() - start - kRecHeader);
    h.lsn = next_lsn_++;
    h.nops = static_cast<uint32_t>(n);
    h.crc = 0;
    h.crc = record_crc(h, active_.data() + start + kRecHeader);
    std::memcpy(active_.data() + start, &h, sizeof h);
    records_++;
    bytes_ += kRecHeader + h.payload_len;

    if (opt_.durability == Durability::Write) {
        if (!write_out(active_.data(), active_.size())) return 0;
        active_.clear();
    }
    appended_ = h.lsn;                 // counted only once it is in the file (Write) or the buffer
    return h.lsn;
}

// Moves everything appended so far to the file and, unless the level is Write,
// makes it durable. Only one thread at a time may be in here (the leader, the
// background thread, or the destructor).
void Wal::flush_buffer() {
    uint64_t upto;
    {
        std::lock_guard<std::mutex> g(buf_mu_);
        flushing_buf_.swap(active_);
        upto = appended_;
    }
    write_out(flushing_buf_.data(), flushing_buf_.size());
    flushing_buf_.clear();
    sync_file();
    uint64_t group = upto - last_synced_appended_;
    last_synced_appended_ = upto;
    uint64_t prev = per_sync_max_.load();
    while (group > prev && !per_sync_max_.compare_exchange_weak(prev, group)) {}
    {
        std::lock_guard<std::mutex> g(dur_mu_);
        durable_ = upto;
    }
    dur_cv_.notify_all();
}

void Wal::wait_durable(uint64_t lsn) {
    if (opt_.durability != Durability::Sync || lsn == 0) return;
    std::unique_lock<std::mutex> lk(dur_mu_);
    while (durable_ < lsn && !failed_.load()) {
        if (flushing_) {
            dur_cv_.wait(lk);
            continue;
        }
        flushing_ = true;                              // become the leader for this group
        if (opt_.group_delay_us > 0)
            dur_cv_.wait_for(lk, std::chrono::microseconds(opt_.group_delay_us));
        lk.unlock();
        flush_buffer();
        lk.lock();
        flushing_ = false;
        dur_cv_.notify_all();
    }
}

void Wal::background() {
    while (!stop_.load()) {
        {
            std::unique_lock<std::mutex> lk(bg_mu_);
            bg_cv_.wait_for(lk, std::chrono::milliseconds(std::max(1, opt_.flush_interval_ms)),
                            [&] { return stop_.load(); });
        }
        if (stop_.load() || failed_.load()) break;
        if (opt_.durability == Durability::Async) {
            flush_buffer();
        } else {
            uint64_t upto;
            {
                std::lock_guard<std::mutex> g(buf_mu_);
                upto = appended_;
            }
            if (upto == last_synced_appended_) continue;
            sync_file();
            last_synced_appended_ = upto;
            std::lock_guard<std::mutex> g(dur_mu_);
            durable_ = upto;
        }
    }
}

WalStats Wal::stats() const {
    WalStats s;
    s.records = records_.load();
    s.bytes = bytes_.load();
    s.write_calls = write_calls_.load();
    s.syncs = syncs_.load();
    s.sync_ns_total = sync_ns_total_.load();
    s.sync_ns_max = sync_ns_max_.load();
    s.records_per_sync_max = per_sync_max_.load();
    s.synced_bytes = synced_end_.load();
    return s;
}
