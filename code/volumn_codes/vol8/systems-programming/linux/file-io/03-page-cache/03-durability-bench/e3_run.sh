#!/bin/bash
# E3: 同一份数据(SIZE_MIB,默认 256MiB,1MiB 块)走四条持久化路径,各 3 轮取中位数:
#   plain / fsync / fdatasync / osync(O_SYNC)
# 每次测量前把 Dirty 压回低位,避免上一轮残留脏页污染下一轮的 sync 计时。
set -u
cd "$(dirname "$0")"
SIZE_MIB=${1:-256}
CHUNK_KIB=${2:-1024}
ROUNDS=${3:-3}
BIN=/home/charliechen/l03_scratch/e3.bin

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
echo "# 口径: size=${SIZE_MIB}MiB chunk=${CHUNK_KIB}KiB rounds=${ROUNDS} 计时=clock_gettime(CLOCK_MONOTONIC),含 sync 调用本身"
echo

MODES="plain fsync fdatasync osync"
declare -A RES
for mode in $MODES; do
  TIMES=()
  for r in $(seq 1 "$ROUNDS"); do
    PRE=$(wait_clean)
    rm -f "$BIN"
    OUT=$(./e3_bench "$mode" "$BIN" "$SIZE_MIB" "$CHUNK_KIB")
    MS=$(echo "$OUT" | sed -n 's/.*elapsed_ms=\([0-9.]*\).*/\1/p')
    TIMES+=("$MS")
    echo "# round ${r} mode=${mode} pre_dirty=${PRE}kB  ${OUT}"
  done
  RES[$mode]=${TIMES[*]}
done

echo
echo "# ===================== 汇总(原始轮次 + 中位数) ====================="
printf '%-10s %12s %12s %12s %14s %12s\n' mode r1_ms r2_ms r3_ms median_ms median_MiB_s
for mode in $MODES; do
  set -- ${RES[$mode]}
  MED=$(echo "${RES[$mode]}" | tr ' ' '\n' | sort -n | sed -n "$(( (ROUNDS + 1) / 2 ))p")
  TP=$(awk -v m="$MED" -v s="$SIZE_MIB" 'BEGIN { printf "%.0f", s / (m / 1000.0) }')
  printf '%-10s %12s %12s %12s %14s %12s\n' "$mode" "$1" "$2" "$3" "$MED" "$TP"
done

# 附加观察: O_SYNC 配 4KiB 小块(64MiB 口径) —— 模拟"每次写都持久化"的数据库式负载
echo
echo "# ===================== 附加: O_SYNC + 4KiB 小块(64MiB 口径) ====================="
TIMES4=()
for r in $(seq 1 "$ROUNDS"); do
  PRE=$(wait_clean)
  rm -f "$BIN"
  OUT=$(./e3_bench osync "$BIN" 64 4)
  echo "# round ${r} mode=osync4k pre_dirty=${PRE}kB  ${OUT}"
  TIMES4+=("$(echo "$OUT" | sed -n 's/.*elapsed_ms=\([0-9.]*\).*/\1/p')")
done
set -- ${TIMES4[*]}
MED=$(echo "${TIMES4[*]}" | tr ' ' '\n' | sort -n | sed -n "$(( (ROUNDS + 1) / 2 ))p")
TP=$(awk -v m="$MED" 'BEGIN { printf "%.0f", 64 / (m / 1000.0) }')
printf '%-10s %12s %12s %12s %14s %12s\n' osync_4k "$1" "$2" "$3" "$MED" "$TP"
rm -f "$BIN"
