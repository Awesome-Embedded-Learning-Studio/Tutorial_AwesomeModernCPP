#!/bin/bash
# E2: 32MiB 四条持久化路径,各 5 轮取中位数(对齐 Linux 侧 03-durability-bench 方法论):
#   plain / flush / wt(FILE_FLAG_WRITE_THROUGH) / wt_flush
# 口径注记:Windows 没有 /proc/meminfo Dirty 可观测的干净窗,轮次隔离只能靠
# "每轮程序内删文件 + 上一轮 FlushFileBuffers 已落盘",轮间干扰无法像 Linux 侧
# 那样显式压回;诚实起见原始轮次全贴,汇总取中位数。
set -u
cd "$(dirname "$0")"
GXX=/mnt/c/msys64/ucrt64/bin/g++.exe
$GXX -std=c++20 -Wall -Wextra e2_flush_bench.cpp -o e2_flush_bench.exe || exit 1
chmod +x e2_flush_bench.exe

SIZE_MIB=${1:-32}
CHUNK_KIB=${2:-1024}
ROUNDS=${3:-5}
RAW=$(mktemp)

echo "# 环境: Win11 26200 | MSYS2 UCRT64 g++ 16.1.0 | 目标卷 = %TEMP% 所在 NTFS 系统盘(NVMe,WD_BLACK SN7100)"
echo "# 口径: size=${SIZE_MIB}MiB chunk=${CHUNK_KIB}KiB rounds=${ROUNDS} 计时=QueryPerformanceCounter;write_ms 与 flush_ms 分列,flush 含在 total 里"
echo

MODES="plain flush wt wt_flush"
for mode in $MODES; do
  echo "# ---- mode=${mode} ----"
  for r in $(seq 1 "$ROUNDS"); do
    out=$(./e2_flush_bench.exe "$mode" "$SIZE_MIB" "$CHUNK_KIB")
    echo "round${r} ${out}"
    echo "$out" >> "$RAW"
  done
done

echo
echo "# ===================== 汇总(${ROUNDS} 轮中位数) ====================="
printf '%-9s %10s %10s %10s %12s %12s\n' mode write_ms flush_ms total_ms write_MiB_s total_MiB_s
for mode in $MODES; do
  w=$(grep "mode=$mode " "$RAW" | sed -n 's/.*write_ms=\([0-9.]*\).*/\1/p' | sort -n | sed -n "$(( (ROUNDS + 1) / 2 ))p")
  f=$(grep "mode=$mode " "$RAW" | sed -n 's/.*flush_ms=\([0-9.]*\).*/\1/p' | sort -n | sed -n "$(( (ROUNDS + 1) / 2 ))p")
  t=$(grep "mode=$mode " "$RAW" | sed -n 's/.*total_ms=\([0-9.]*\).*/\1/p' | sort -n | sed -n "$(( (ROUNDS + 1) / 2 ))p")
  tw=$(awk -v w="$w" -v s="$SIZE_MIB" 'BEGIN { printf "%.0f", s / (w / 1000.0) }')
  tt=$(awk -v v="$t" -v s="$SIZE_MIB" 'BEGIN { printf "%.0f", s / (v / 1000.0) }')
  printf '%-9s %10s %10s %10s %12s %12s\n' "$mode" "$w" "$f" "$t" "$tw" "$tt"
done

# 附加: wt 配 4KiB 小块(32MiB 口径) —— "每次写都穿缓存"的数据库式负载
echo
echo "# ===================== 附加: wt + 4KiB 小块(32MiB 口径) ====================="
for r in $(seq 1 "$ROUNDS"); do
  out=$(./e2_flush_bench.exe wt "$SIZE_MIB" 4)
  echo "round${r} ${out}"
  echo "$out" >> "$RAW"
done
m=$(grep 'mode=wt .*chunkKiB=4 ' "$RAW" | sed -n 's/.*total_ms=\([0-9.]*\).*/\1/p' | sort -n | sed -n "$(( (ROUNDS + 1) / 2 ))p")
awk -v v="$m" -v s="$SIZE_MIB" 'BEGIN { printf "%-9s %10s %10s %10s %12s %12s\n", "wt_4k", "-", "-", v, "-", s / (v / 1000.0) }'
rm -f "$RAW"
