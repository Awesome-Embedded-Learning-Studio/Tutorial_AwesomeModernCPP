#!/bin/bash
# E1 附:权限位跨 uid 实证
# 本机 sudo -n 不可用,但 WSL2 可以 `wsl.exe -u root --` 免密开 root 会话;
# /dev/shm 是整个发行版共享的 tmpfs,root 会话建的对象在 user 会话里同一个实体
# 用法:./e1_perm.sh 2>&1 | tee e1_perm.out(产物已去掉 wsl.exe 带来的 CR)
set -u
D=/home/charliechen/lm03_scratch
WSLROOT="/mnt/c/Windows/system32/wsl.exe -u root --"

echo "== E1 附:权限位跨 uid 实证(root 建对象,user 进程开) =="
echo

echo "[1] root 会话建两个对象:0600 与 0666(umask 0,mode 原样生效)"
$WSLROOT $D/e1_perm create /lm03_perm600 0600
$WSLROOT $D/e1_perm create /lm03_perm666 0666
echo
echo "\$ ls -l /dev/shm"
ls -l /dev/shm
echo

echo "[2] user 进程(uid=$(id -u))分别去开:"
$D/e1_perm open /lm03_perm600
$D/e1_perm open /lm03_perm666
echo "    ↑ 0600(other 无 rw)→ EACCES;0666 → 打开成功并读到 root 写的内容"
echo

echo "[3] 反向:user 建一个 0600 对象,root 去开"
$D/e1_perm create /lm03_user600 0600
$WSLROOT $D/e1_perm open /lm03_user600
echo "    ↑ root 不受 0600 限制:CAP_DAC_OVERRIDE 越过常规权限检查(这是 root 的特权,不是权限失效)"
echo

echo "[4] 清理(root 会话)"
$WSLROOT $D/e1_perm unlink /lm03_perm600
$WSLROOT $D/e1_perm unlink /lm03_perm666
$WSLROOT $D/e1_perm unlink /lm03_user600
echo "done"
