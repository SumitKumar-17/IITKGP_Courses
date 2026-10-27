#!/bin/bash
# Runs on the server, from the root of an unpacked submission. Builds every part from scratch,
# runs every part's tests and sanitizer checks, and smoke-runs each benchmark program once.
# Prints PASS or FAIL per step and exits non-zero if any step failed. The log of the run that
# was made before submitting is in verification.txt.
cd "$(dirname "$0")/.."
fail=0
step() {   # name, command...
  local name=$1; shift
  local out
  out=$("$@" 2>&1)
  if [ $? -eq 0 ] && ! grep -qE "FAIL|failure\(s\)|ERROR: (Address|Thread|Leak)Sanitizer|runtime error" <<< "$out"; then
    echo "PASS  $name"
  else
    echo "FAIL  $name"; echo "$out" | tail -15; fail=1
  fi
}

for p in A B C E; do
  step "part$p: clean build"   bash -c "make -C part$p clean >/dev/null && make -C part$p -s all"
  step "part$p: tests"         make -C part$p -s test
  step "part$p: sanitizers"    make -C part$p -s check
done
step "partD: build"            bash -c "make -C partD clean >/dev/null && make -C partD -s"
cd partD
step "partD: point mode, RCU"       taskset -c 0-15 build/bench_rcu --mode point --readers 4 --seconds 2
step "partD: point mode, rwlock"    taskset -c 0-15 build/bench_rwlock --mode point --readers 4 --seconds 2
step "partD: join mode"             taskset -c 0-15 build/bench_rcu --mode join --readers 4 --seconds 2 --outer 1000
step "partD: stop-the-world mode"   taskset -c 0-15 build/bench_rcu --mode point --readers 4 --seconds 2 --writer-rate 300000 --hold-ms 50 --hold-period 100 --reclaim stw --trigger 4096
step "partD: big transaction mode"  build/bench_rcu --mode bigtxn --reps 2 --sizes 1,256,100000
step "partD: write-ahead log, sync" taskset -c 0-15 build/bench_wal --mode point --readers 2 --seconds 2 --writer-rate 5000 --wal sync --wal-path verify.wal
rm -f verify.wal
step "partD: replay speed"          build/replay_bench 200000 verify_replay.wal
cd ..
step "provided drivers (generator)" bash -c "partA/build/global_lock --readers 2 --seconds 2 && partA/build/rwlock --readers 2 --seconds 2 && partA/build/single_thread --readers 1 --seconds 2 && partB/build/rcu --readers 2 --seconds 2 && partC/build/rcu --readers 2 --seconds 2 && partE/build/rcu --readers 2 --seconds 2"
[ $fail -eq 0 ] && echo "ALL PASSED" || echo "SOME STEPS FAILED"
exit $fail
