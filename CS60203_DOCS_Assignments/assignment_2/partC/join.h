// join.h: index-nested-loop join of trades (outer, by primary key) with
// instruments (inner, through the secondary index on symbol).
//
// One Snapshot is acquired at the start and handed to every keys(),
// index_get() and get() call, so the whole join reads one unchanging version
// of each table however many versions the writer publishes meanwhile.
#pragma once
#include "kvstore.h"
#include "schema.h"

#include <vector>

struct JoinCounts {
    long outer_rows = 0;   // trades read
    long matches = 0;      // (trade, instrument) pairs emitted
};

// emit(const Trade&, const Instrument&) is called once per match. max_outer
// caps how many trade keys keys() is asked for.
template <class Emit>
JoinCounts index_nested_loop_join(const KVStore& trades, const KVStore& instruments,
                                  size_t max_outer, Emit&& emit) {
    KVStore::Snapshot snap = trades.acquire_snapshot();

    std::vector<long> outer(max_outer);
    size_t n = trades.keys(snap, outer.data(), outer.size());

    JoinCounts out;
    for (size_t i = 0; i < n; i++) {
        Trade t;
        if (!trades.get(snap, outer[i], &t, sizeof t)) continue;
        out.outer_rows++;

        long ids[8];
        int m = instruments.index_get(snap, t.symbol, sizeof t.symbol, ids, 8);
        for (int j = 0; j < m; j++) {
            Instrument ins;
            if (!instruments.get(snap, ids[j], &ins, sizeof ins)) continue;
            out.matches++;
            emit(t, ins);
        }
    }
    return out;
}
