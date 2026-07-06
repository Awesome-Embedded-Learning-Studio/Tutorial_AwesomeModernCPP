# vol6 ch07 · 编译器优化边界与体积评估 — 代码示例

对应文章:`documents/vol6-performance/ch07-compiler-and-size/`

四个程序 + 若干「对照编译」脚本,对应 ch07 四篇。**核心精神:编译器是性能队友,你的工作是「别挡路 + 让它看得见」;体积↔速度常反向取舍。**

## 构建(推荐用对照脚本,逐 flag 对比)

```bash
# 07-01 -O 级别 + 别名 blocker:同一文件多编几遍
for opt in O0 O2 O3; do
  g++ -$opt -std=c++17 opt_levels_blockers.cpp -o ob_$opt
  echo -n "  -$opt 别名版:   "; taskset -c 0 ./ob_$opt a
  echo -n "  -$opt restrict版:"; taskset -c 0 ./ob_$opt r
done

# 07-02 LTO 跨 TU
g++ -O2 -std=c++17          lto_main.cpp lto_helper.cpp -o lto_nolto
g++ -O2 -std=c++17 -flto    lto_main.cpp lto_helper.cpp -o lto_lto
echo -n "无 LTO: "; taskset -c 0 ./lto_nolto
echo -n "有 LTO: "; taskset -c 0 ./lto_lto
# PGO 复用 ch04/pgo.sh

# 07-04 体积优化:看二进制大小
g++ -O2 -std=c++17 size_demo.cpp -o size_o2
g++ -Os -std=c++17 size_demo.cpp -o size_os
g++ -Oz -std=c++17 size_demo.cpp -o size_oz
g++ -O2 -std=c++17 -ffunction-sections -fdata-sections size_demo.cpp -o size_gc -Wl,--gc-sections
for f in size_o2 size_os size_oz size_gc; do printf "  %-10s %s\n" "$f" "$(stat -c%s $f)"; done

# 或 cmake -B build && cmake --build build
```

## 程序对照

| 程序 | 文章 | 核心数字 |
|---|---|---|
| `opt_levels_blockers` | 07-01 | -O0→-O2 **~4×**;**-O3 比 -O2 反慢**(7.4 vs 4.9);__restrict 在 -O3 有用 |
| `lto_main`+`lto_helper` | 07-02 | LTO 跨 TU 内联 **3.9×**(178.6→46.2 ms);PGO 微基准无收益(复用 ch04)|
| `size_demo` | 07-04 | --gc-sections 省 200B;-Os/-Oz 比 -O2 省几百 B(小 demo;大项目 5-15%)|

## 诚实点

- **-O3 不总比 -O2 快**(本机这个带 volatile 的循环上,-O3 反慢)——「更激进优化=更快」是误解。
- **LTO 的 3.9×** 是跨 TU 内联的干净收益;大项目上 LTO 是 release 标配。
- **PGO 对微基准无收益**(那个一度的 4× 是仪器化开销,不是 PGO),价值在大代码库——见 ch04/pgo.sh 的诚实讨论。
- **体积差异在小 demo 上很小**(几百字节),大项目才有 5-15%。
