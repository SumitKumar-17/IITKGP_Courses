# NOVA-KV Part C: atomic multi-key writes, snapshots, index join

Sumit Kumar (22CS30056), Jatin Mahawar (22CS30032)

Everything here is built on the Part B mechanism, in the same `KVStore` class. `PART_C_DIFF.patch`
is the diff against the Part B `kvstore.h` and `kvstore.cpp`. **There is still no durability.**

What was added: `Transaction` (real, atomic), `KVStore::Snapshot`, `acquire_snapshot()`, the
snapshot-scoped `get`, `index_get` and `keys`, and the working `create_index` and `index_get`. `join.h` has the
index-nested-loop join. The version record is now `{primary trie, index trie, count, commit seq}`,
so one pointer swap publishes the table and its index together.

---

## C.1 Atomic, isolated multi-key writes

### How a transaction commits

`Transaction::put` only copies the key and bytes into a private buffer. `commit()` takes the writer
mutex, opens **one** build (the same path-copying machinery as a single `put`), applies every staged
write into it, and publishes the result with **one atomic store of `current_`**. Writes in the same
commit that land in the same leaf or the same inner node reuse the copy made by the first one, since
a node created by the running build has not been published and can be changed in place. A
transaction that is destroyed without `commit()` has only touched its own buffer, so there is
nothing to undo.

### Atomicity

A reader gets a version through a single load of `current_`, and a commit produces exactly one new
version. No version exists that holds some of a transaction's writes and not the others, so a reader
sees all of them or none. This is what a reader sees *through a snapshot* (or any one call): reading
several keys with separate plain `get()` calls can straddle two commits, because each call takes the
current version again. To read several keys together, take a `Snapshot` and use it for all of them.

Test: a writer commits 8-key transactions (every key gets the same new number) while 4 readers each
take a snapshot, read the 8 keys and require them to be equal. **1.9 to 2.3 million transactions,
0 torn snapshots.** The control uses the same readers against a writer that makes 8 separate `put()`
calls: it produces **30 million torn snapshots**, so the check does fail when atomicity is missing.

### Isolation, and which level this is

Every read through a `Snapshot` sees one immutable version for as long as the handle lives, however
many versions the writer publishes meanwhile, and the writer never waits for a reader. Test: a held
snapshot read 105 million times (and `keys()` re-enumerated) while the writer made 1.3 million
commits and erased keys: **0 changes seen**, and the writer made progress the whole time.

**The level is snapshot isolation, not serializability.** Precisely:

- Reads: all reads through one snapshot see the database as of one commit. No dirty reads, no
  non-repeatable reads, no phantoms.
- Writes: a transaction starts at a commit sequence number (the newest version when it is created, or
  the version of a `Snapshot` passed to its constructor). Every row carries the sequence of the commit
  that wrote it. At commit the store checks the keys the transaction writes; if any was committed
  after the start, the transaction is refused (`commit()` returns false and publishes nothing). This
  is first-committer-wins, so **lost updates are prevented**.
- The check covers write-write conflicts only. **Nothing validates what the transaction read.**

That last point is **write skew**. Take two doctors on call, with the rule that at least one must
stay on call. Transaction A reads both rows from its snapshot, sees that Bob is still on call, and
takes Alice off. Transaction B, started from the same state, reads both rows, sees that Alice is
still on call, and takes Bob off. Each is correct on its own. They write different rows, so there is
no write-write conflict and first-committer-wins lets both through. Afterwards nobody is on call,
and no serial order of A and B would have produced that, because whichever went second would have
seen the other doctor already off. `txn_test` reproduces exactly this: both commits succeed and the
count on call is 0. Serializability would refuse the second, which needs the reads to be tracked
and validated at commit (serializable snapshot isolation), or all update transactions to run on the
one writer thread one after another. We do neither.

A note on how likely this is here: with the single writer thread the assignment assumes, running its
transactions one at a time and acquiring each transaction's snapshot just before it, writes are
effectively serial and skew cannot arise. It appears when decisions are made from a snapshot that
later commits have overtaken, or when several threads commit.

Limits of the conflict check: a row erased after the transaction started is not noticed; the stamp
is 32 bits and compared with wrap-around, which is correct as long as fewer than 2^31 commits happen
while one transaction is open; a plain `put()` outside a transaction is not checked itself but does
stamp the row, so a transaction that started earlier will see it as a conflict.

### What we give up by not being durable

Committed means visible to readers and nothing more: it lives only in memory. If the process dies
right now, **every committed transaction is gone, including ones readers already saw and acted on**
(a trading decision may have used data that no longer exists after the restart). An in-flight
transaction (staged but not committed) simply vanishes, which is harmless. Atomicity across a crash
holds trivially, because after a restart there is no state at all, not a partial one. Part E (the
optional write-ahead log) is what would give committed transactions a life after a crash.

---

## C.2 Secondary index and the index-nested-loop join

### The index

`create_index(offset, len)` indexes `len` bytes (at most 40) starting at `offset` of every stored
value, one index per store, and builds it from the rows already there. From then on every `put`,
`erase` and transaction commit updates it inside the same build, so the index in a version always
describes exactly the rows in that version. Rows shorter than `offset + len` have no entry.

We chose a path-copying trie rather than a hash table of posting lists. A posting list for a popular
value is long (2,000 trades per symbol in the benchmark), and copy-on-write would copy the whole list
for every insert. Instead there is **one index entry per row**, keyed by the primary key but routed
by the field: the top 30 bits of its hash come from a hash of the field bytes and the low bits from
the primary key. All rows with the same field value therefore sit under one depth-5 node, and
`index_get` is a five-step descent followed by a walk of that subtree, comparing the stored field
bytes (so two values whose 30-bit hashes collide are told apart). Adding or removing one row copies
one short path, as for the primary table. When a row's field changes, its old entry is removed and a
new one inserted in the same commit; when only other bytes change, the index is untouched.

### Snapshots

`Snapshot` is move-only and owns a private slot in the epoch table (not the calling thread's own
slot, so ordinary calls made while it is held do not disturb it). While it is alive nothing newer
than its epoch is freed, whatever the reclaim batch. A held snapshot is therefore a memory cost, not a
blocking one: in the isolation test the writer retired about 1.4 million versions while one snapshot
was held, and **the backlog was freed again, 64 versions per write, within 23,000 writes of releasing
it**, with no pass ever freeing more than 64.

The benchmark's two tables are two `KVStore` objects, but the join must use one handle. A snapshot
pins the store it was acquired from. The first time it is passed to another store it pins that
store's current version and keeps it from then on (up to four stores). That is safe because the epoch
is process-wide: any version that is current after the snapshot announced itself is retired at an
epoch no earlier than the announcement, so it cannot be freed while the handle lives. The two stores
are therefore each frozen at a single version, but not frozen at the same instant, because the stores
have no common clock. Use one handle from one thread.

### The join

`join.h` is the loop the assignment describes:

1. `Snapshot snap = trades.acquire_snapshot();` once, at the start;
2. `trades.keys(snap, ...)` enumerates the outer table under that snapshot;
3. for each key, `trades.get(snap, ...)` reads the trade;
4. `instruments.index_get(snap, trade.symbol, ...)` probes the index on the inner table;
5. for each match `instruments.get(snap, id, ...)` fetches the full instrument row to emit.

Once a store has been touched, further calls on it take the handle and nothing else: no epoch
announcement and no `current_` load, because the version is already known.

Tests (`txn_test`, all also run under AddressSanitizer, UBSan and ThreadSanitizer):

| what | result |
|---|---|
| index vs a scan of the table, in the same snapshot, while the writer does puts, erases, field changes and transactions | 0 mismatches in 66,000 comparisons |
| 4 readers joining 5,000 trades with 50 instruments while the writer rewrites instruments in one transaction and changes and deletes trades | 41,000 joins, 0 inconsistent: every pair has equal symbols, one match per trade, one `tick_size` throughout |
| one snapshot, join result hashed, writes made, hashed again | identical; a fresh snapshot hashes differently |
| lost update (two transactions, one key) | second refused, nothing from it visible |
| write skew (doctors) | reproduced, 0 on call |

## Assumptions and limits

One writer at a time (a mutex serialises a second); one index per store, field at most 40 bytes, set
once; the index is not kept consistent under schema changes (not required); `keys()` returns keys in
hash order, not sorted; a `Snapshot` is for one thread; at most 1,024 concurrent readers and
snapshots in the process; no durability.

## Files

`kvstore.h`, `kvstore.cpp`, `join.h`, `schema.h`, `kvstore_test.cpp`, `txn_test.cpp`,
`stress_test.cpp` (carried over from Part B), `rcu.cpp`, `Makefile` (`make test`, `make check`),
`PART_C_DIFF.patch`.
