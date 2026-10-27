#!/bin/bash
# Puts back the server settings the measurements changed. After ./syncer.sh push:
#   cat pass.txt | ssh t1000@10.5.31.157 'sudo -S -p "" bash ~/docs_asgn2/tools/restore_server.sh'
for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo powersave > "$g"; done
for e in /sys/devices/system/cpu/cpu*/cpufreq/energy_performance_preference; do echo balance_performance > "$e"; done
for d in /sys/devices/system/cpu/cpu*/cpuidle/state[23]/disable; do echo 0 > "$d"; done
sysctl -w kernel.kptr_restrict=1
