#!/bin/sh
# E4b 驱动:跑 rlimit_cpu 并补记退出码(被 SIGKILL 处决这件事,进程自己打印不了)
cd "$(dirname "$0")"
./e4_cpu
st=$?
echo
echo "驱动观察: 退出码 $st —— 137 = 128+9,即被 SIGKILL 处决(硬限不可协商)"
