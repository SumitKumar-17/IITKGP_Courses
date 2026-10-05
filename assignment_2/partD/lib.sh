# Sourced by the Part D scripts. The benchmark server is shared with other people,
# so every group of runs first checks that logical CPUs 0 to 15 are idle. The check
# reads /proc/stat twice and spins nothing, so it does not warm the cores up or
# otherwise change what the next run sees.
cpu_busy_max() {   # highest busy percentage over CPUs 0-15 during one second
  local a b
  a=$(grep -E '^cpu([0-9]|1[0-5]) ' /proc/stat); sleep 1; b=$(grep -E '^cpu([0-9]|1[0-5]) ' /proc/stat)
  paste <(echo "$a") <(echo "$b") | awk '{
    t1=0; t2=0; for (i=2;i<=11;i++) t1+=$i; for (i=13;i<=22;i++) t2+=$i;
    idle=($16+$17)-($5+$6); tot=t2-t1; busy=(tot>0)?100*(tot-idle)/tot:0;
    if (busy>m) m=busy } END {printf "%.1f", m}'
}

wait_quiet() {
  local tries=0 busy
  while :; do
    busy=$(cpu_busy_max)
    if awk -v b="$busy" 'BEGIN {exit !(b < 5)}'; then
      echo "quiet: busiest of cpus 0-15 was $busy%" >> "${QUIET_LOG:-/dev/null}"
      return 0
    fi
    tries=$((tries + 1))
    echo "busy: busiest of cpus 0-15 was $busy%, waiting (try $tries)" >> "${QUIET_LOG:-/dev/null}"
    [ $tries -ge 10 ] && return 0
    sleep 20
  done
}
