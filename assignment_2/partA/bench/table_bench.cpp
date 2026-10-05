// Single-thread comparison of three hash table layouts for the Part A store.
// Not part of the store itself; it only decides which design kvstore.cpp uses.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

static inline uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// A: chaining, one heap node per entry, value stored after the header.
struct Chained {
    struct Node { Node* next; long key; uint32_t len; char val[]; };
    std::vector<Node*> b;
    size_t n = 0;
    Chained() : b(1 << 12, nullptr) {}
    void grow() {
        std::vector<Node*> nb(b.size() * 2, nullptr);
        for (Node* h : b) while (h) { Node* nx = h->next; size_t i = mix(h->key) & (nb.size() - 1); h->next = nb[i]; nb[i] = h; h = nx; }
        b.swap(nb);
    }
    void put(long k, const void* v, size_t len) {
        size_t i = mix(k) & (b.size() - 1);
        for (Node* p = b[i]; p; p = p->next) if (p->key == k) { memcpy(p->val, v, len); return; }
        Node* nd = (Node*)malloc(sizeof(Node) + len);
        nd->key = k; nd->len = len; memcpy(nd->val, v, len);
        nd->next = b[i]; b[i] = nd;
        if (++n > b.size()) grow();
    }
    bool get(long k, void* out, size_t cap) const {
        for (Node* p = b[mix(k) & (b.size() - 1)]; p; p = p->next)
            if (p->key == k) { memcpy(out, p->val, p->len < cap ? p->len : cap); return true; }
        return false;
    }
};

// B: open addressing, linear probing, 24-byte slots pointing at a heap value.
struct OpenPtr {
    struct Slot { long key; uint32_t len; uint32_t used; char* val; };
    Slot* s; size_t cap, n = 0;
    OpenPtr() : cap(1 << 12) { s = (Slot*)calloc(cap, sizeof(Slot)); }
    void grow() {
        Slot* old = s; size_t oc = cap; cap *= 2; s = (Slot*)calloc(cap, sizeof(Slot));
        for (size_t i = 0; i < oc; i++) if (old[i].used) {
            size_t j = mix(old[i].key) & (cap - 1);
            while (s[j].used) j = (j + 1) & (cap - 1);
            s[j] = old[i];
        }
        free(old);
    }
    void put(long k, const void* v, size_t len) {
        size_t i = mix(k) & (cap - 1);
        while (s[i].used) {
            if (s[i].key == k) { memcpy(s[i].val, v, len); return; }
            i = (i + 1) & (cap - 1);
        }
        s[i].used = 1; s[i].key = k; s[i].len = len;
        s[i].val = (char*)malloc(len); memcpy(s[i].val, v, len);
        if (++n * 2 > cap) grow();
    }
    bool get(long k, void* out, size_t cap_out) const {
        size_t i = mix(k) & (cap - 1);
        while (s[i].used) {
            if (s[i].key == k) { memcpy(out, s[i].val, s[i].len < cap_out ? s[i].len : cap_out); return true; }
            i = (i + 1) & (cap - 1);
        }
        return false;
    }
};

// C: open addressing, 64-byte cache-line slots with the value inline.
struct OpenInline {
    struct alignas(64) Slot { long key; uint32_t len; uint32_t used; char val[48]; };
    Slot* s; size_t cap, n = 0;
    OpenInline() : cap(1 << 12) { s = (Slot*)aligned_alloc(64, cap * sizeof(Slot)); memset(s, 0, cap * sizeof(Slot)); }
    void grow() {
        Slot* old = s; size_t oc = cap; cap *= 2;
        s = (Slot*)aligned_alloc(64, cap * sizeof(Slot)); memset(s, 0, cap * sizeof(Slot));
        for (size_t i = 0; i < oc; i++) if (old[i].used) {
            size_t j = mix(old[i].key) & (cap - 1);
            while (s[j].used) j = (j + 1) & (cap - 1);
            s[j] = old[i];
        }
        free(old);
    }
    void put(long k, const void* v, size_t len) {
        size_t i = mix(k) & (cap - 1);
        while (s[i].used) {
            if (s[i].key == k) { memcpy(s[i].val, v, len); return; }
            i = (i + 1) & (cap - 1);
        }
        s[i].used = 1; s[i].key = k; s[i].len = len; memcpy(s[i].val, v, len);
        if (++n * 2 > cap) grow();
    }
    bool get(long k, void* out, size_t cap_out) const {
        size_t i = mix(k) & (cap - 1);
        while (s[i].used) {
            if (s[i].key == k) { memcpy(out, s[i].val, s[i].len < cap_out ? s[i].len : cap_out); return true; }
            i = (i + 1) & (cap - 1);
        }
        return false;
    }
};

template <class T>
void run(const char* name, long N) {
    T t;
    char v[32] = {1};
    auto t0 = std::chrono::steady_clock::now();
    for (long i = 0; i < N; i++) t.put((i / 100) * 100000 + i % 100, v, sizeof v);
    auto t1 = std::chrono::steady_clock::now();
    std::mt19937_64 rng(42);
    std::vector<long> q(1 << 20);
    for (auto& k : q) { long i = rng() % N; k = (i / 100) * 100000 + i % 100; }
    char out[32]; long hit = 0;
    const int reps = 20;
    auto t2 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; r++) for (long k : q) hit += t.get(k, out, sizeof out);
    auto t3 = std::chrono::steady_clock::now();
    double ins = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;
    double get = std::chrono::duration<double, std::nano>(t3 - t2).count() / (double)(reps * q.size());
    printf("%-12s N=%-8ld insert %6.1f ns/op   get %6.1f ns/op   (hits %ld)\n", name, N, ins, get, hit);
}

int main() {
    for (long N : {100000L, 1000000L}) {
        run<Chained>("chained", N);
        run<OpenPtr>("open+ptr", N);
        run<OpenInline>("open+inline", N);
    }
}
