// kvstore_test.cpp: basic checks for insert, lookup, overwrite, erase, growth.
#include "kvstore.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

struct Rec { long a; double b; char tag[16]; };

int main() {
    KVStore kv;
    Rec r{7, 1.5, "alpha"}, out{};

    CHECK(!kv.get(1, &out, sizeof out));
    CHECK(kv.put(1, &r, sizeof r));
    CHECK(kv.get(1, &out, sizeof out));
    CHECK(std::memcmp(&r, &out, sizeof r) == 0);

    r.a = 99;
    CHECK(kv.put(1, &r, sizeof r));
    CHECK(kv.get(1, &out, sizeof out) && out.a == 99);
    CHECK(kv.size() == 1);

    CHECK(kv.erase(1));
    CHECK(!kv.erase(1));
    CHECK(!kv.get(1, &out, sizeof out));
    CHECK(kv.size() == 0);

    std::string big(500, 'x');
    char buf[500];
    CHECK(kv.put(2, big.data(), big.size()));
    CHECK(kv.get(2, buf, sizeof buf) && std::memcmp(buf, big.data(), 500) == 0);
    std::string small = "tiny";
    CHECK(kv.put(2, small.data(), small.size()));
    CHECK(kv.get(2, buf, sizeof buf) && std::memcmp(buf, "tiny", 4) == 0);
    CHECK(kv.erase(2));

    const long N = 200000;
    for (long k = 0; k < N; k++) { long v = k * 3; CHECK(kv.put(k * 7919, &v, sizeof v)); }
    CHECK(kv.size() == (size_t)N);
    bool all = true;
    for (long k = 0; k < N; k++) { long v = 0; all &= kv.get(k * 7919, &v, sizeof v) && v == k * 3; }
    CHECK(all);

    for (long k = 0; k < N; k += 2) all &= kv.erase(k * 7919);
    CHECK(all);
    for (long k = 0; k < N; k++) {
        long v = 0;
        bool found = kv.get(k * 7919, &v, sizeof v);
        if (k % 2 == 0) all &= !found; else all &= found && v == k * 3;
    }
    CHECK(all);
    CHECK(kv.size() == (size_t)N / 2);

    std::printf(failures ? "kvstore_test: %d failure(s)\n" : "kvstore_test: all passed\n", failures);
    return failures != 0;
}
