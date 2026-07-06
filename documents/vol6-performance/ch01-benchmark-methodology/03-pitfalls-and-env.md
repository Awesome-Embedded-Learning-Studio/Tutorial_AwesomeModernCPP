---
chapter: 1
cpp_standard:
- 11
- 17
description: ch01-01 骗术三(噪声)的具体对策——把 16 个会让性能数字失真的环境陷阱按频率/缓存/调度/工具分组,逐条给「失真原因 + 规避命令」,附
  perf-env-check.sh 一键体检脚本
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 怎么写一个可信的 microbenchmark
reading_time_minutes: 6
related:
- 为什么 microbenchmark 会骗你
- 统计与报告
tags:
- host
- cpp-modern
- intermediate
- 优化
- 测试
title: 测量陷阱与环境就绪:16 条 checklist
---
# 测量陷阱与环境就绪:16 条 checklist

## 为什么需要一份 checklist

ch01-01 讲了 microbenchmark 第三类欺骗:系统噪声淹没信号。那篇给的是「为什么会噪声」,这一篇给「具体怎么关」。下面 16 条是 Linux 上做可信 microbenchmark 最常踩的环境陷阱,每条都按「**陷阱 → 为什么失真 → 怎么规避**」给出,大多还配一条可以直接抄的命令。

但开讲之前,先重复一遍 ch01-01 那条最重要的边界:**这些消除噪声的手段,只在「做相对 A/B 比较」时该用。** 如果你要评估的是「用户实际感受到多快」,反而要**复刻**真实环境(保留噪声、保留 DFS、保留邻居进程),然后用统计方法处理它,那是 ch01-05 生产测量的活。这一页是给「我想干净地比较两个实现」的 microbenchmark 场景用的。

## 16 条陷阱

为了好记,按性质分四组。

### 第一组:频率与功耗(数字的最大波动源)

| # | 陷阱 | 为什么失真 | 规避 |
|---|---|---|---|
| 1 | **CPU 调频(DVFS)** | governor=ondemand 时频率随风浮动,GBench 启动还会打 `***WARNING*** CPU scaling enabled` | `sudo cpupower frequency-set -g performance` 锁到最高频 |
| 2 | **Turbo Boost** | 单核突发高频,冷启动和稳态不同,温度一上来就降 | BIOS 关 Turbo;或锁频;测稳态先热够（warmup说的是这个） |

这两条不解决,同一份代码两次跑差 10% 都正常。笔记本上尤其严重(散热有限,Turbo 频繁进出)。

### 第二组:缓存、内存与地址翻译

| # | 陷阱 | 为什么失真 | 规避 |
|---|---|---|---|
| 3 | **冷启动与稳态** | 首次访问未命中缓存(走 DRAM),之后命中缓存,差 10–100 倍 | 框架的估测阶段已经预热过;想测冷启动,用 `posix_fadvise(fd, POSIX_FADV_DONTNEED)` 把页丢弃 |
| 4 | **缺页(page fault)** | 首次碰页触发软缺页(微秒级),放大单次操作几十倍 | `mlockall(MCL_CURRENT \| MCL_FUTURE)` 锁页;或先把每一页都触碰一遍 |
| 9 | **NUMA** | 多 socket 机器跨节点访存延迟翻 2–4 倍,「内存带宽」测成了「互联带宽」 | `numactl --cpunodebind=0 --membind=0 ./bench` 把线程和内存绑同一节点 |
| 15 | **ASLR / 代码布局** | PIE 基址不同,指令缓存(icache)和分支预测器对齐抖动 10–20%;还影响「内存布局偏置」(Mytkowicz 2009) | 微架构精细测时加 `-no-pie`;想消除布局偏置用随机交错(random interleaving) |

### 第三组:调度与干扰

| # | 陷阱 | 为什么失真 | 规避 |
|---|---|---|---|
| 5 | **上下文切换 / 中断** | 被调度走,样本长尾离群 | `taskset -c <核>` 绑核;统计用中位数(别用均值) |
| 8 | **绑核(CPU pinning)** | 线程跨核迁移,缓存每次冷掉 | `taskset -c 3 ./bench`(挑一个核,别让 OS 晃) |
| 10 | **SMT / 超线程争用** | 同物理核另一线程吃执行单元 | BIOS 关超线程;或 taskset 只绑物理核(每两个兄弟核用一个) |
| 11 | **定时器分辨率** | `clock()` 测纳秒级全是噪声(分辨率不够) | `std::chrono::steady_clock`(见 ch00-02);或 `perf stat` 看 cycle |

### 第四组:工具姿势与统计

| # | 陷阱 | 为什么失真 | 规避 |
|---|---|---|---|
| 6 | **死代码被优化掉** | 结果没用 → DCE 消掉循环(见 ch01-01 的 `foo()`) | `DoNotOptimize` / `doNotOptimizeAway`;注意它**不防表达式自身被算掉**(ch01-02) |
| 7 | **没开 release + 调试信息** | `-O0` 的性能数字无意义;纯 `-O2` 没 `-g` 又做不了源码标注 | 统一用 `RelWithDebInfo`(`-O2 -g`);profiling 再加 `-fno-omit-frame-pointer`(否则栈断,火焰图炸) |
| 12 | **均值与中位数** | 微基准右偏(长尾),均值被拉高 | 报中位数 + IQR;GBench `Repetitions` + `ReportAggregatesOnly`(ch01-02) |
| 13 | **样本太少** | 置信区间宽到分不清 A/B | ≥30 个样本;报 95% CI;A/B 用 Mann-Whitney U 检验判显著性(ch01-04) |
| 14 | **多次运行不稳** | 环境没固定,跨次运行漂移 | ≥3 次取最稳;`perf stat -r 5` 自带重复 |
| 16 | **PEBS 滑步(skid)** | 采样事件会「滑」几条指令才落到真正的指令上 | 用带 `:pp` / `:ppp`(精确 IP)后缀的事件,如 `MEM_LOAD_RETIRED.L3_MISS:ppp` |

16 条看着多,核心就一句:**把能控的全控住(频率、核、内存布局),把结果该消费的真消费(`DoNotOptimize`),把数字当分布看(中位数 + 多轮重复)。** 剩下的看场景:做 micro 就尽量全做,做生产评估就别做(复刻真实)。

## 一键体检:`perf-env-check.sh`

每次开测前手动查一遍这些项很烦,我们把它压成一个脚本。它**只检查、不修改**(改 governor、关 Turbo 那种要 sudo 的操作,留给你自己决定),把发现的问题打印出来:

```bash
#!/usr/bin/env bash
# perf-env-check.sh —— 可信 microbenchmark 环境体检(只查不改)
set -u

ok()   { printf "  ✓ %s\n" "$1"; }
warn() { printf "  ⚠ %s\n" "$1"; }

echo "=== CPU governor(应=performance)==="
g=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)
[ "$g" = performance ] && ok "governor=performance" || warn "governor=$g(DVFS 会浮动)。修:sudo cpupower frequency-set -g performance"

echo "=== Turbo Boost(Intel)==="
if [ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
  nt=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)
  [ "$nt" = 1 ] && ok "Turbo 已关" || warn "Turbo 开着(no_turbo=$nt),冷热启动数字会差"
else
  echo "  · 非 intel_pstate 或无该接口,跳过(可在 BIOS 设)"
fi

echo "=== perf_event_paranoid(<=1 才好采样)==="
p=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null)
[ "${p:-3}" -le 1 ] && ok "perf_event_paranoid=$p" || warn "=$p(perf 受限)。修:sudo sysctl -w kernel.perf_event_paranoid=1"

echo "=== NUMA 拓扑(多 socket 才在意)==="
command -v numactl >/dev/null && numactl --hardware 2>/dev/null | grep -E "^available|node [0-9]+ cpus" | head -4 || warn "无 numactl"

echo "=== CPU 亲和性(应明确绑一个核,别让 OS 晃)==="
cpu=$(grep Cpus_allowed_list /proc/self/status 2>/dev/null | awk '{print $2}')
n=$(nproc 2>/dev/null)
echo "  Cpus_allowed_list=$cpu (nproc=$n) → 没绑核就 taskset -c <某个核> ./bench(别挑 0 号核,常被系统中断占用)"

echo "=== ASLR(微架构精细测时应关)==="
aslr=$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null)
echo "  randomize_va_space=$aslr(2=全开;精细 icache/分支测时可 sudo sysctl -w kernel.randomize_va_space=0)"
```

把它存成 `perf-env-check.sh`,跑一下 `bash perf-env-check.sh` 就知道环境还差什么。完整的脚本也在 `code/volumn_codes/vol6-performance/ch01/` 下。

## 哪些场景该做哪些

| 场景 | 该做的(消除噪声) | 不该做的 |
|---|---|---|
| **microbenchmark 做 A/B 比较** | 1/2/4/5/8/9/10/15,尽量全做,你要的是干净信噪比 | 别把结论直接推给生产 |
| **评估生产性能** | **几乎都不做**,复刻真实环境(保留 DFS、邻居、ASLR) | 别关噪声源,否则测的不是用户会经历的 |
| **profiling 找热点** | 7(`-fno-omit-frame-pointer`)、16(`:pp`) | 找热点本来就是在真实负载下采 |

这张表是 ch01-01 那条「让 micro 干净的,正是让它骗人的」的具体落地:**同一组手段,在 micro 场景是解药,在生产场景是毒药。** 拿捏这个分寸,比记住 16 条命令更重要。

## 参考资源

- easyperf.net:*How to get consistent results when benchmarking on Linux*(这份 checklist 的直接来源之一)
- Brendan Gregg:[Linux Performance](https://www.brendangregg.com/linuxperf.html)(perf / 任务放置 / NUMA)
- Bakhvalov, D. 《Performance Analysis and Tuning on Modern CPUs》§2.1 *Noise In Modern Systems*
- 本卷 ch01-01(噪声的分类)、ch01-02(`DoNotOptimize` / `Repetitions` / `UseRealTime`)
