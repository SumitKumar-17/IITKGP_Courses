#!/usr/bin/bash

LOG=simulation_cache_blocking.log
PERF=/usr/bin/perf

# Remove old log file
rm -f $LOG

for N in 1024 2048 4096 8192; do
    echo "# ============================================================" >> $LOG
    echo "# Results for N = $N, B = 32" >> $LOG
    echo "# ============================================================" >> $LOG

    # Compile
    gcc -O0 -o naive_${N}_O0 matmul_naive_${N}.c >> $LOG 2>&1
    gcc -O0 -o blocking_${N}_O0 matmul_blocking_${N}.c >> $LOG 2>&1

    gcc -O3 -march=native -fopt-info-vec -o naive_${N}_O3 matmul_naive_${N}.c >> $LOG 2>&1
    gcc -O3 -march=native -fopt-info-vec -o blocking_${N}_O3 matmul_blocking_${N}.c >> $LOG 2>&1

    # Run with perf
    echo "# Naive results (-O0), N=$N" >> $LOG
    $PERF stat -e cache-references,cache-misses ./naive_${N}_O0 >> $LOG 2>&1

    echo "# Naive results (-O3), N=$N" >> $LOG
    $PERF stat -e cache-references,cache-misses ./naive_${N}_O3 >> $LOG 2>&1

    echo "# Blocking results (-O0), N=$N" >> $LOG
    $PERF stat -e cache-references,cache-misses ./blocking_${N}_O0 >> $LOG 2>&1

    echo "# Blocking results (-O3), N=$N" >> $LOG
    $PERF stat -e cache-references,cache-misses ./blocking_${N}_O3 >> $LOG 2>&1

done

echo -e "\nSimulation results present in $LOG\n"
exit
