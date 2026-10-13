#!/bin/bash
# E5: 崩溃丢失的近似观察(本环境能力边界探测)。
# 1) sudo -n 探测:不可用则放弃 drop_caches 方案,如实记录
# 2) 可行观察链: 写 64MiB 不 sync -> Dirty 高企 -> 文件却完全可读(页缓存)
#               -> sync -> Dirty 归零 -> 文件仍可读(此刻起才敢说"在盘上")
set -u
cd "$(dirname "$0")"
BIN=/home/charliechen/l03_scratch/e5.bin

dirty() { awk '/^Dirty:/{print $2}' /proc/meminfo; }

echo "# 环境: $(uname -r) | $(lscpu | sed -n 's/^Model name: *//p') | 数据盘 $(df -T /home/charliechen | tail -1 | tr -s ' ')"
echo
echo "== 步骤 0: sudo / drop_caches 可用性 =="
if sudo -n true 2>/dev/null; then
  echo "# sudo -n true: 可用"
  echo "# (本机实测:不可用,走下面降级方案;此行仅当你在有免密 sudo 的机器上复跑时生效)"
else
  echo "# sudo -n true: 不可用 —— 提示: sudo: a password is required"
  echo "# 结论: /proc/sys/vm/drop_caches 需要 root,本环境无法做 drop_caches 观察;"
  echo "#       真崩溃丢失(掉电/宿主机死机)更无法安全演示,文章如实交代即可。"
fi
echo
echo "== 步骤 1: sync 压基线 =="
sync
sleep 2
echo "# 基线 Dirty=$(dirty) kB"
echo
echo "== 步骤 2: dd 写 64MiB,不 sync,立刻观察 =="
rm -f "$BIN"
dd if=/dev/zero of="$BIN" bs=1M count=64 2>&1 | tail -1 | sed 's/^/#   /'
D1=$(dirty)
SZ1=$(stat -c %s "$BIN")
HEAD1=$(od -An -tx1 -N8 "$BIN" | tr -d ' \n')
echo "# dd 返回后: Dirty=${D1} kB(≈64MiB 脏页滞留), 文件大小 ${SZ1} 字节, 前 8 字节 ${HEAD1}"
echo "#            —— 文件此刻\"完全可读\",但这 64MiB 只在内存里,盘上一个字节都还没有"
echo
echo "== 步骤 3: sync 之后 =="
sync
sleep 2
D2=$(dirty)
SZ2=$(stat -c %s "$BIN")
HEAD2=$(od -An -tx1 -N8 "$BIN" | tr -d ' \n')
echo "# sync 返回后: Dirty=${D2} kB, 文件大小 ${SZ2} 字节, 前 8 字节 ${HEAD2}"
echo "#            —— sync 返回 = 内核已把这批脏页写回存储;从这一刻起,断电才不丢"
rm -f "$BIN"
