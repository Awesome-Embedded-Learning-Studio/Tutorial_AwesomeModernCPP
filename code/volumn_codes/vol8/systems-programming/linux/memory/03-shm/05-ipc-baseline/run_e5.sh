#!/bin/bash
# E5 驱动:shm vs pipe 各 3 轮
set -u
cd "$(dirname "$0")"
echo "== E5 同一 1 MiB:共享内存环形队列 vs pipe 分 1024 次写读,各 3 轮 =="
echo
for r in 1 2 3; do ./e5_ipc shm; done
echo
for r in 1 2 3; do ./e5_ipc pipe; done
