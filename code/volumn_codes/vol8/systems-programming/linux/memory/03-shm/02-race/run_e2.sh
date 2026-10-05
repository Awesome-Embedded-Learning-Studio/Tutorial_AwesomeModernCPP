#!/bin/bash
# E2 驱动:三版各 3 轮 + nosync 循环体汇编佐证(load/add/store,确认不是寄存器累加)
set -u
cd "$(dirname "$0")"
echo "== E2 跨进程计数竞态:三版各 3 轮(每人 1,000,000 次,期望 2,000,000)=="
echo
for v in nosync mutex sem; do
  echo "--- $v ---"
  for r in 1 2 3; do ./e2_race "$v"; done
  echo
done
echo "== 附:nosync 计数循环的汇编(确认每轮都是回内存的读改写,不是寄存器累加)=="
objdump -d e2_race | grep -E 'add.*\$0x1,0x[0-9a-f]+\(%' | head -5
echo "    ↑ 计数是 addq \$0x1,内存 的非原子读改写(无 lock 前缀);对比 ready.fetch_add 编出的是 lock addl"
