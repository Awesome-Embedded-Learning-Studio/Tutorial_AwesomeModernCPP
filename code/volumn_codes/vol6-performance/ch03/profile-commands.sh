#!/usr/bin/env bash
# profile-commands.sh — vol6 ch03 profiler 命令速查(参考用,非一键脚本)
#
# ⚠️ 这些命令需要 perf / toplev / FlameGraph 脚本 / bpftrace,本机(WSL2)没装,
# 所以这不是「在这里跑」的脚本,而是「在装好工具的裸机 Linux 上怎么跑」的速查。
# 每段标注对应文章哪一篇、什么场景用。详细解读见 ch03-02 / ch03-03。
#
# 参考:Brendan Gregg(usemethod / FlameGraphs)、easyperf.net、Bakhvalov 书、pmu-tools。

set -e

echo "本脚本是命令速查,不会真的跑全部命令(很多需要目标程序 + 特权)。"
echo "逐段复制到你装好工具的环境用。"
echo

# ===== 1. USE 系统体检(ch03-01)— 最早期排查 =====
echo "## 1. USE 系统体检 —— 排除「问题不在 CPU」"
cat <<'EOF'
vmstat 1          # %us+%sy(CPU)、r(运行队列)、si/so(换页>0 说明内存不够)
free -m           # 内存余量
iostat -xz 1      # 磁盘 %util / avgqu-sz
sar -n DEV 1      # 网络吞吐
EOF
echo

# ===== 2. perf on-CPU 采样 + 火焰图(ch03-03)=====
echo "## 2. perf on-CPU 采样 + 火焰图"
cat <<'EOF'
# 编译时务必加(否则栈断):g++ -O2 -g -fno-omit-frame-pointer ...
perf record -F 99 --call-graph dwarf -- ./your_app
perf script > out.perf
# 用 Brendan Gregg 的 FlameGraph 仓库脚本:
./stackcollapse-perf.pl out.perf > out.folded
./flamegraph.pl out.folded > out.svg
EOF
echo

# ===== 3. TMAM toplev 工作流(ch03-02)=====
echo "## 3. TMAM 四桶下钻(pmu-tools/toplev,需 pip install + perf)"
cat <<'EOF'
toplev -l1 -- ./your_app     # 四桶定主战场
toplev -l2 -- ./your_app     # 下钻(Backend → Memory vs Core)
toplev -l3 -- ./your_app     # 再下钻到 cache 层级

# 用精确事件(:ppp)定位 cache miss 的具体汇编指令
perf record -e MEM_LOAD_RETIRED.L3_MISS:ppp -- ./your_app
perf annotate                # 汇编级看哪条指令在 miss
EOF
echo

# ===== 4. off-CPU / 等延迟(ch03-03,需 bcc/bpftrace)=====
echo "## 4. off-CPU 火焰图(看「在等什么」,需 eBPF)"
cat <<'EOF'
# bcc 工具:
profile -af -p <pid> 10 > offcpu.out   # 采 off-CPU 栈
# 或 bpftrace 一行:
bpftrace -e 'profile:hz:99 { @[ustack] = count(); }'
EOF
echo

# ===== 5. COZ 因果 profiling(ch03-03)=====
echo "## 5. COZ(告诉你优化哪个函数整体收益最大)"
cat <<'EOF'
# 链接 COZ 运行时编译:g++ ... -ldl -lpthread -lcoz
# 跑:COZ_... 环境变量控制,输出 profile 在 coz.out,浏览器看
# 详见 curtsinger.cc/coz
EOF
