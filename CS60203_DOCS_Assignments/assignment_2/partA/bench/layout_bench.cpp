// Point-get cost of a Trade record under three in-memory layouts:
//   NSM: array of structs        DSM: one array per field
//   PAX: pages of P rows, fields column-major inside each page
// Three access patterns: whole-row get (what readers do), symbol-only get (the
// join probe), and a full-table scan of price. Row positions are random and
// each position depends on the previous result, so the number is latency, not
// something the prefetcher or out-of-order core can hide.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

struct Trade { long id; char sym[8]; double price; long ts; };

static inline uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

struct NSM {
    Trade* rows;
    explicit NSM(size_t n) { rows = (Trade*)aligned_alloc(64, n * sizeof(Trade)); }
    void set(size_t i, const Trade& t) { rows[i] = t; }
    Trade get(size_t i) const { return rows[i]; }
    uint64_t sym(size_t i) const { uint64_t s; memcpy(&s, rows[i].sym, 8); return s; }
    double price(size_t i) const { return rows[i].price; }
};

struct DSM {
    long* id; char (*sy)[8]; double* pr; long* ts;
    explicit DSM(size_t n) {
        id = (long*)aligned_alloc(64, n * 8); sy = (char(*)[8])aligned_alloc(64, n * 8);
        pr = (double*)aligned_alloc(64, n * 8); ts = (long*)aligned_alloc(64, n * 8);
    }
    void set(size_t i, const Trade& t) { id[i] = t.id; memcpy(sy[i], t.sym, 8); pr[i] = t.price; ts[i] = t.ts; }
    Trade get(size_t i) const { Trade t; t.id = id[i]; memcpy(t.sym, sy[i], 8); t.price = pr[i]; t.ts = ts[i]; return t; }
    uint64_t sym(size_t i) const { uint64_t s; memcpy(&s, sy[i], 8); return s; }
    double price(size_t i) const { return pr[i]; }
};

template <size_t P>
struct PAX {
    struct Page { long id[P]; char sy[P][8]; double pr[P]; long ts[P]; };
    Page* pages;
    explicit PAX(size_t n) { pages = (Page*)aligned_alloc(64, ((n + P - 1) / P) * sizeof(Page)); }
    void set(size_t i, const Trade& t) {
        Page& p = pages[i / P]; size_t k = i % P;
        p.id[k] = t.id; memcpy(p.sy[k], t.sym, 8); p.pr[k] = t.price; p.ts[k] = t.ts;
    }
    Trade get(size_t i) const {
        const Page& p = pages[i / P]; size_t k = i % P; Trade t;
        t.id = p.id[k]; memcpy(t.sym, p.sy[k], 8); t.price = p.pr[k]; t.ts = p.ts[k]; return t;
    }
    uint64_t sym(size_t i) const { uint64_t s; memcpy(&s, pages[i / P].sy[i % P], 8); return s; }
    double price(size_t i) const { return pages[i / P].pr[i % P]; }
};

using clk = std::chrono::steady_clock;
static double ns_since(clk::time_point t0, size_t ops) {
    return std::chrono::duration<double, std::nano>(clk::now() - t0).count() / ops;
}

template <class L>
void bench(const char* name, size_t n, int threads) {
    L tab(n);
    for (size_t i = 0; i < n; i++) {
        Trade t{}; t.id = i; snprintf(t.sym, 8, "SYM%03zu", i % 50); t.price = 100 + (i % 97) * 0.37; t.ts = i;
        tab.set(i, t);
    }
    const size_t ops = 20000000;

    auto chase = [&](int mode, uint64_t seed) {
        uint64_t acc = seed; size_t idx = mix(seed) % n; volatile uint64_t sink;
        for (size_t k = 0; k < ops; k++) {
            if (mode == 0) { Trade t = tab.get(idx); acc += t.id + (uint64_t)t.price + t.ts + t.sym[3]; }
            else acc += tab.sym(idx);
            idx = mix(acc + k) % n;
        }
        sink = acc; (void)sink;
    };

    auto t0 = clk::now();
    chase(0, 1);
    double row_ns = ns_since(t0, ops);
    t0 = clk::now();
    chase(1, 1);
    double sym_ns = ns_since(t0, ops);

    t0 = clk::now();
    double s = 0;
    for (int r = 0; r < 20; r++) for (size_t i = 0; i < n; i++) s += tab.price(i);
    double scan_ns = ns_since(t0, 20 * n);
    volatile double sink = s; (void)sink;

    std::vector<std::thread> th;
    t0 = clk::now();
    for (int t = 0; t < threads; t++) th.emplace_back([&, t] {
        uint64_t acc = t; size_t idx = mix(t + 7) % n;
        for (size_t k = 0; k < ops / 4; k++) { Trade tr = tab.get(idx); acc += tr.id + (uint64_t)tr.price + tr.ts + tr.sym[3]; idx = mix(acc + k) % n; }
        volatile uint64_t sink2 = acc; (void)sink2;
    });
    for (auto& x : th) x.join();
    double mt_mops = (double)threads * (ops / 4) / (ns_since(t0, 1) / 1e3) ;

    printf("%-8s rows=%-8zu row-get %6.1f ns   symbol-only %6.1f ns   price-scan %5.2f ns/row   %d-thread row-get %7.1f Mops/s\n",
           name, n, row_ns, sym_ns, scan_ns, threads, mt_mops);
}

int main() {
    for (size_t n : {100000UL, 4000000UL}) {
        bench<NSM>("NSM", n, 8);
        bench<DSM>("DSM", n, 8);
        bench<PAX<16>>("PAX-16", n, 8);
        bench<PAX<64>>("PAX-64", n, 8);
        bench<PAX<256>>("PAX-256", n, 8);
    }
}
