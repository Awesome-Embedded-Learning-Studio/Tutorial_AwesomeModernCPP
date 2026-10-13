#!/bin/bash
# E1: 观察页缓存脏页滞留与后台写回。
# 写 256 MiB(只 write,不 fsync),writer _exit 后每 2s 采样 /proc/meminfo 的
# Dirty / Writeback,看脏页何时被内核的 flusher 线程写回。
set -u
cd "$(dirname "$0")"

dirty() { awk '/^Dirty:/{print $2}' /proc/meminfo; }
wback() { awk '/^Writeback:/{print $2}' /proc/meminfo; }

echo "# 环境: $(uname -r) | $(lscpu | sed -n 's/^Model name: *//p') | 数据盘 $(df -T /home/charliechen | tail -1 | tr -s ' ')"
echo "# vm 口径: dirty_background_ratio=$(cat /proc/sys/vm/dirty_background_ratio) dirty_ratio=$(cat /proc/sys/vm/dirty_ratio) dirty_expire_centisecs=$(cat /proc/sys/vm/dirty_expire_centisecs) dirty_writeback_centisecs=$(cat /proc/sys/vm/dirty_writeback_centisecs)"
echo "# MemTotal: $(awk '/^MemTotal:/{print $2, "kB"}' /proc/meminfo) (决定上面 ratio 阈值的分母)"

echo
echo "# 压基线: sync,等 Dirty 落回低位"
sync
BASE=""
for i in $(seq 1 60); do
  BASE=$(dirty)
  [ "$BASE" -lt 4096 ] && break
  sleep 1
done
echo "# 基线 Dirty=${BASE} kB Writeback=$(wback) kB"

echo
echo "# 起 writer: 256 MiB,只 write(),随后 _exit(0)"
./e1_dirty

echo
echo "# writer 已退出。开始采样(相对秒  Dirty[kB]  Writeback[kB]):"
T0=$(date +%s)
for i in $(seq 0 40); do
  D=$(dirty); W=$(wback)
  echo "# $(( $(date +%s) - T0 ))  ${D}  ${W}"
  [ "$i" -lt 40 ] && sleep 2
done

exit 0
