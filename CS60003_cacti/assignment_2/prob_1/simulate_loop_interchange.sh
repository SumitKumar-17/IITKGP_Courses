#!/usr/bin/bash

LOG=simulation_loop_interchange.log
PERF=/usr/bin/perf

# Remove old log file
rm -f $LOG

for N in 1024 2048 4096 8192; do
    echo "# ============================================================" >> $LOG
    echo "# Results for N = $N" >> $LOG
    echo "# ============================================================" >> $LOG

    # Compile
    gcc -O0 -o rmajor_${N}_O0 row_major_${N}.c >> $LOG 2>&1
    gcc -O0 -o cmajor_${N}_O0 column_major_${N}.c >> $LOG 2>&1

    gcc -O3 -march=native -fopt-info-vec -o rmajor_${N}_O3 row_major_${N}.c >> $LOG 2>&1
    gcc -O3 -march=native -fopt-info-vec -o cmajor_${N}_O3 column_major_${N}.c >> $LOG 2>&1

    # Run with perf
    echo "# Row major results (-O0), N=$N" >> $LOG
    $PERF stat -e cache-references,cache-misses ./rmajor_${N}_O0 >> $LOG 2>&1

    echo "# Row major results (-O3), N=$N" >> $LOG
    $PERF stat -e cache-references,cache-misses ./rmajor_${N}_O3 >> $LOG 2>&1

    echo "# Column major results (-O0), N=$N" >> $LOG
    $PERF stat -e cache-references,cache-misses ./cmajor_${N}_O0 >> $LOG 2>&1

    echo "# Column major results (-O3), N=$N" >> $LOG
    $PERF stat -e cache-references,cache-misses ./cmajor_${N}_O3 >> $LOG 2>&1

done

echo -e "\nSimulation results present in $LOG\n"
exit
