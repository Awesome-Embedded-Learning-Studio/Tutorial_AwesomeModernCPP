#!/usr/bin/env bash
# pgo.sh — vol6 ch04-07 / ch07-02 的 PGO 三阶段构建(对照用)
#
# 用 pgo_demo.cpp 演示 Profile-Guided Optimization 的完整三阶段流程,
# 并和「纯 -O2 无 PGO」基线对照。
#
# ⚠️ 本机(WSL2)实测结论:PGO 对这个微基准【无收益】(~3.7 vs ~3.9 ms,噪声内)。
# PGO 的价值在大型代码库的代码布局,不在微基准。详见 ch04-07 文章里的诚实讨论。
#
# 跑: bash pgo.sh
set -e
cd "$(dirname "$0")"

echo "===== 阶段 0:纯 -O2 基线(无仪器化、无 profile)====="
g++ -O2 -std=c++17 pgo_demo.cpp -o pgo_plain

echo "===== 阶段 1:仪器化编译(-fprofile-generate)====="
g++ -O2 -std=c++17 -fprofile-generate pgo_demo.cpp -o pgo_demo

echo "===== 阶段 2:跑代表性 workload,生成 .gcda 剖面 ====="
./pgo_demo >/dev/null
ls *.gcda && echo "(剖面已生成)"

echo "===== 阶段 3:用剖面重编(-fprofile-use,【同名构建】让 gcda 名匹配)====="
# ⚠️ 关键:gcda 文件名 = <二进制名>-<源名>.gcda。生成和重编必须用同一个二进制名,
#    否则 -fprofile-use 找不到 profile(会 warning "profile count data file not found")。
g++ -O2 -std=c++17 -fprofile-use pgo_demo.cpp -o pgo_demo 2>&1 | grep -iE "warning|error" || echo "(无 warning,profile 正确应用)"

echo ""
echo "===== 对比(各跑 3 次取稳)====="
for i in 1 2 3; do
  echo "--- 第 $i 轮 ---"
  echo -n "  纯 -O2 基线: "; taskset -c 0 ./pgo_plain
  echo -n "  -O2 + PGO:  "; taskset -c 0 ./pgo_demo
done
echo ""
echo "结论:对这个微基准,两者应基本持平(PGO 收益在大型代码库,不在小函数)。"
