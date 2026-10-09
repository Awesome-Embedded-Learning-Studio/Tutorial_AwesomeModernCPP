#!/bin/bash
# E4d 驱动:mq 两种消息规格各 3 轮(与 Lmem03 E5 的 3 轮取中位同口径)
set -u
cd "$(dirname "$0")"
echo "== E4d 同一 1 MiB 走 POSIX 消息队列(maxmsg=10,满仓阻塞),各 3 轮 =="
echo "== 参照 Lmem03 E5 同机数字:shm SPSC 环形中位 7304 MiB/s、pipe 1024B 块中位 2349 MiB/s =="
echo
echo "--- 1024 B × 1024 条(与 Lmem03 的分块完全一致) ---"
for r in 1 2 3; do ./e4_mq_throughput 1024; done
echo
echo "--- 8192 B × 128 条(贴着 msgsize_max=8192 的上限,减少系统调用次数) ---"
for r in 1 2 3; do ./e4_mq_throughput 8192; done
echo
echo "--- 同尺寸对照:pipe 8192 B × 128 块(与 Lmem03 e5 同构,只换块大小) ---"
for r in 1 2 3; do ./e4_mq_throughput pipe8192; done
