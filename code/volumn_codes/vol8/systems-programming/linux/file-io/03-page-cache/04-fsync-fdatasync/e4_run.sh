#!/bin/bash
# E4: fsync vs fdatasync,三种脏法(fixed/append/touch)× 两种 sync × 3 轮。
# 每次测量前 wait_clean 压基线。
set -u
cd "$(dirname "$0")"
ITERS=${1:-200}
BIN=/home/charliechen/l03_scratch/e4.bin

dirty() { awk '/^Dirty:/{print $2}' /proc/meminfo; }

wait_clean() {
  sync
  local i d
  for i in $(seq 1 180); do
    d=$(dirty)
    if [ "$d" -lt 4096 ]; then echo "$d"; return 0; fi
    sleep 1
  done
  echo "$d"
}

echo "# 环境: $(uname -r) | $(lscpu | sed -n 's/^Model name: *//p') | 数据盘 $(df -T /home/charliechen | tail -1 | tr -s ' ')"
echo "# 口径: 每轮 = ${ITERS} 次 (改动 + sync),报告每次 sync 的中位/最大耗时与整轮墙钟"
echo

declare -A RES
for scen in fixed append touch; do
  for sm in fsync fdatasync; do
    TIMES=()
    for r in 1 2 3; do
      PRE=$(wait_clean)
      OUT=$(./e4_metadata "$scen" "$sm" "$ITERS" "$BIN")
      US=$(echo "$OUT" | sed -n 's/.*per_op_median_us=\([0-9.]*\).*/\1/p')
      TIMES+=("$US")
      echo "# round ${r} scen=${scen} sync=${sm} pre_dirty=${PRE}kB  ${OUT}"
    done
    RES[$scen,$sm]=${TIMES[*]}
  done
done

echo
echo "# ============ 汇总: 每次 sync 的中位耗时(µs),三轮原始值 + 中位数 ============"
printf '%-8s %-12s %10s %10s %10s %12s\n' scenario sync r1_us r2_us r3_us median_us
for scen in fixed append touch; do
  for sm in fsync fdatasync; do
    set -- ${RES[$scen,$sm]}
    MED=$(echo "${RES[$scen,$sm]}" | tr ' ' '\n' | sort -n | sed -n '2p')
    printf '%-8s %-12s %10s %10s %10s %12s\n' "$scen" "$sm" "$1" "$2" "$3" "$MED"
  done
done
rm -f "$BIN"
