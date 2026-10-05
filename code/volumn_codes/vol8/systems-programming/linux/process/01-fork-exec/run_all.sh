#!/bin/bash
# 一键复现:编译全部实验并逐个运行,把原始输出写回各自的 .out(会覆盖存档)。
# 数据文件路径烧死在源码里,统一落在 ~/lp01_scratch;脚本会先确保它存在。
# e2d 需要 strace;其余只依赖 g++ 和 procfs。
set -eu
cd "$(dirname "$0")"
mkdir -p ~/lp01_scratch build

CXX="g++ -std=c++20 -Wall -Wextra -O2"

build() { $CXX -o "build/$1" "$2"; }
build e1a_fork_twice      e1-fork-ledger/e1a_fork_twice.cpp
build e1b_cow             e1-fork-ledger/e1b_cow.cpp
build e1c_fork_cost       e1-fork-ledger/e1c_fork_cost.cpp
build e1d_shared_offset   e1-fork-ledger/e1d_shared_offset.cpp
build e2a_exec_variants   e2-exec-family/e2a_exec_variants.cpp
build e2b_exec_fd         e2-exec-family/e2b_exec_fd.cpp
build e2c_exec_fail       e2-exec-family/e2c_exec_fail.cpp
build e2d_strace_target   e2-exec-family/e2d_strace_target.cpp
build e3a_zombie_lifecycle e3-zombie-reap/e3a_zombie_lifecycle.cpp
build e3b_reap_strategies e3-zombie-reap/e3b_reap_strategies.cpp
build e3c_exit_status     e3-zombie-reap/e3c_exit_status.cpp
build e4_orphan           e4-orphan/e4_orphan.cpp
build e5a_spawn_compare   e5-posix-spawn/e5a_spawn_compare.cpp
build e5b_spawn_actions   e5-posix-spawn/e5b_spawn_actions.cpp
build e6_child_process    e6-raii-child/e6_child_process.cpp

echo "# 环境: $(uname -r) | $(lscpu | sed -n 's/^Model name: *//p') | PID1=$(cat /proc/1/comm)"
echo

run() { echo "== $1 =="; ./build/"$1" > "$2" 2>&1; echo "   输出已写入 $2"; }

run e1a_fork_twice       e1-fork-ledger/e1a.out
run e1b_cow              e1-fork-ledger/e1b.out
run e1c_fork_cost        e1-fork-ledger/e1c.out
run e1d_shared_offset    e1-fork-ledger/e1d.out
run e2a_exec_variants    e2-exec-family/e2a.out
run e2b_exec_fd          e2-exec-family/e2b.out
run e2c_exec_fail        e2-exec-family/e2c.out
run e3a_zombie_lifecycle e3-zombie-reap/e3a.out
run e3b_reap_strategies  e3-zombie-reap/e3b.out
run e3c_exit_status      e3-zombie-reap/e3c.out
run e4_orphan            e4-orphan/e4.out
run e5a_spawn_compare    e5-posix-spawn/e5a.out
run e5b_spawn_actions    e5-posix-spawn/e5b.out
run e6_child_process     e6-raii-child/e6.out

# e2d 单独走 strace:程序输出进 e2d.out,strace 系统调用日志进 e2d.strace
echo "== e2d_strace_target (strace) =="
strace -f -e trace=execve,execveat,clone,clone3 -o e2-exec-family/e2d.strace \
    ./build/e2d_strace_target > e2-exec-family/e2d.out 2>&1
echo "   输出已写入 e2-exec-family/e2d.out + e2d.strace"

echo
echo "全部完成。注意:e1c 的计时、e4 的收养者 pid、各 .out 里的进程号每轮都会变,"
echo "数量级与结论应当复现。"
