#!/usr/bin/env bash
# perf-env-check.sh —— vol6 ch01-03 可信 microbenchmark 环境体检(只查不改)
#
# 用法:bash perf-env-check.sh
# 它不修改任何东西(改 governor / 关 Turbo 要 sudo,留给你自己决定),只把发现的问题打印出来。
# 对应文章:documents/vol6-performance/ch01-benchmark-methodology/03-pitfalls-and-env.md
set -u

ok()   { printf "  ✓ %s\n" "$1"; }
warn() { printf "  ⚠ %s — %s\n" "$1" "$2"; }

echo "=== CPU governor(应=performance;否则 DVFS 让数字浮动)==="
if [ -f /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]; then
  g=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)
  [ "$g" = performance ] && ok "governor=performance" \
    || warn "governor=$g" "sudo cpupower frequency-set -g performance"
else
  echo "  · 无 cpufreq 接口(可能已锁频或虚拟化屏蔽),跳过"
fi

echo "=== Turbo Boost(Intel pstate)==="
if [ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
  nt=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)
  [ "$nt" = 1 ] && ok "Turbo 已关" \
    || warn "Turbo 开着(no_turbo=$nt)" "冷热启动数字会差,BIOS 或这里关"
else
  echo "  · 非 intel_pstate,跳过(可在 BIOS 设)"
fi

echo "=== perf_event_paranoid(<=1 才好采样)==="
p=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 3)
[ "$p" -le 1 ] 2>/dev/null && ok "perf_event_paranoid=$p" \
  || warn "perf_event_paranoid=$p" "sudo sysctl -w kernel.perf_event_paranoid=1"

echo "=== NUMA 拓扑(多 socket 才在意)==="
if command -v numactl >/dev/null 2>&1; then
  numactl --hardware 2>/dev/null | grep -E "^available|node [0-9]+ (cpus|size)" | head -6
else
  warn "无 numactl" "apt install numactl / pacman -S numactl;多 socket 机器必装"
fi

echo "=== CPU 亲和性(应明确绑一个核,别让 OS 晃)==="
cpu=$(grep Cpus_allowed_list /proc/self/status 2>/dev/null | awk '{print $2}')
n=$(nproc 2>/dev/null)
echo "  Cpus_allowed_list=$cpu (nproc=$n)"
echo "  → 想绑核:taskset -c <某个核> ./bench   (别挑 0 号核,常被系统中断占用)"

echo "=== ASLR(微架构精细测时应关)==="
aslr=$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null || echo "?")
echo "  randomize_va_space=$aslr (2=全开;精细 icache/分支测时: sudo sysctl -w kernel.randomize_va_space=0)"

echo ""
echo "体检完毕。microbenchmark A/B 场景:把上面 ⚠ 尽量清掉;"
echo "评估生产性能时:这些噪声源反而要保留(复刻真实),见 ch01-05。"
