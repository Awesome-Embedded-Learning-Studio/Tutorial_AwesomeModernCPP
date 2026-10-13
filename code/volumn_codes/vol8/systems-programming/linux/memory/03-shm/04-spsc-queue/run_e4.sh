#!/bin/bash
# E4 驱动:SPSC 两版各 3 轮,100 万条
set -u
cd "$(dirname "$0")"
echo "== E4 SPSC 无锁环形队列:两进程 1,000,000 条 × 32 B,各 3 轮 =="
echo
for v in spin yield; do
  echo "--- $v(满/空时的等待策略:$([ $v = spin ] && echo 'CPU 自旋 + pause' || echo 'sched_yield 让出'))---"
  for r in 1 2 3; do ./e4_spsc "$v"; done
  echo
done
