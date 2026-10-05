#!/bin/bash
# E6: dd 旁证 —— 默认(走页缓存) vs oflag=direct(绕过页缓存) vs conv=fsync(写完补一次 fsync)。
# 每次测量前压 Dirty 基线,并记录测量结束瞬间的 Dirty:默认模式应高企,direct 应≈基线。
set -u
cd "$(dirname "$0")"
SIZE_MIB=${1:-256}
BIN=/home/charliechen/l03_scratch/e6.bin

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
echo "# 口径: dd if=/dev/zero of=... bs=1M count=${SIZE_MIB},3 轮;吞吐取 dd 自报值"
echo

run_one() { # $1=label $2=dd 附加选项 $3=round
  local label=$1 opts=$2 r=$3 pre post out tp
  pre=$(wait_clean)
  rm -f "$BIN"
  out=$(dd if=/dev/zero of="$BIN" bs=1M count="$SIZE_MIB" $opts 2>&1)
  post=$(dirty)
  tp=$(echo "$out" | tail -1 | sed -n 's/.*, \([0-9.]* [GM]B\/s\)$/\1/p')
  echo "# round ${r} ${label}: pre_dirty=${pre}kB post_dirty=${post}kB dd_report=[${tp}]"
  echo "#   raw: $(echo "$out" | tail -1)"
}

for r in 1 2 3; do
  run_one buffered "" "$r"
  run_one direct "oflag=direct" "$r"
  run_one fsync_only "conv=fsync" "$r"
done
rm -f "$BIN"
