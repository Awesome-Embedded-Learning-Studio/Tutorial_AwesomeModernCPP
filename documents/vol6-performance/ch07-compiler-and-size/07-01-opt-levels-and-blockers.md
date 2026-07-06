---
chapter: 7
cpp_standard:
- 17
description: 本篇讲清两件事:-O0/-O1/-O2/-O3/-Os/-Oz 各级编译器做什么(实测 -O0→-O2 快 4 倍,且 -O3 有时反比
  -O2 慢),以及三类编译器优化不了的「blocker」——跨翻译单元(归 LTO)、指针别名(用 __restrict 解锁)、volatile(强禁优化)。核心:你的工作很大程度是「别挡编译器的路」
difficulty: advanced
order: 1
platform: host
prerequisites:
- inline、去虚拟化与编译器优化全景
- 前端优化:代码布局、PGO 与 BOLT
reading_time_minutes: 6
related:
- LTO、ThinLTO 与 PGO 的工程接入
- 体积优化:-Os、--gc-sections 与模板膨胀控制
tags:
- host
- cpp-modern
- advanced
- 优化
- 工具链
title: -O 级别与 optimization blockers:编译器能做什么、做不了什么
---
# -O 级别与 optimization blockers:编译器能做什么、做不了什么

## 编译器是你的第一个性能队友

写 C++ 性能代码,最重要的事之一是**搞清楚编译器会替你做什么、做不了什么**。它会自动内联、自动向量化、自动消除死代码——这些 ch04-02/04/05 都见过;但它也有三类「优化不了」的硬限制(blocker),需要你配合。这一篇把两边都讲清。

## -O 级别:各级编译器做什么

GCC/Clang 的 `-O` 级别控制优化力度。一张表(精确清单查官方文档):

| 级别 | 大致做什么 | 何时用 |
|---|---|---|
| `-O0` | 不优化,变量可观察、汇编可读 | 调试(**性能数字无意义**)|
| `-O1` | 基本优化(常量折叠、简单内联)| 偶尔调试用 |
| `-O2` | 大多数优化:CSE、LICM、调度、自动内联(同 TU)、基础向量化 | **release 默认甜点** |
| `-O3` | 更激进:**循环向量化、自动展开、更激进内联** | 数值/SIMD 受益;**偶尔反伤** |
| `-Os`/`-Oz` | 优化**体积**(快但更小)| 嵌入式 flash 受限(ch07-04)|

`-O2` 是 release 的默认选择——它已经做了 ch04 讲的循环优化大半、同翻译单元内联、基础调度。`-O3` 比 `-O2` 多的主要是「更激进的向量化 + 展开 + 内联」。

### 实测:-O0→-O2 快 4 倍,-O3 偶尔反比 -O2 慢

我们测同一个函数(带 `volatile` scale 的循环)在不同 -O 级别下:

```text
===== -O 级别(同一个循环函数)=====
  -O0: 18.6 ms   ← 不优化,慢 4 倍于 -O2
  -O2:  4.9 ms   ← release 甜点
  -O3:  7.4 ms   ← 比 -O2 还慢!(见下文:不是向量化反伤)
```

两件事:

**1. `-O0` 性能数字无意义**。18.6 ms vs 4.9 ms,4 倍差距。所以**性能测试绝不能用 -O0**——你测的是「没优化的代码」,不是「你的代码的真实性能」。调试用 -O0,性能数字一律 -O2 起。这是 ch01 反复强调过的纪律。

**2. `-O3` 不总比 `-O2` 快**。这里 -O3(7.4 ms)比 -O2(4.9 ms)慢——但先看汇编再下结论:`g++ -O2 -S` vs `g++ -O3 -S` 这个 `scale_add_alias` 函数,**两个都没向量化**(`volatile` 读不能缓存进寄存器,`-fopt-info-vec-missed` 明确报 `not vectorized: volatile type`;注意这跟别名分析无关——volatile 不参与别名判断),只是寄存器分配/指令调度略有差异。所以 7.4 vs 4.9 **不是「激进向量化反伤」(压根没向量化),更可能是测量噪声 + 调度差异**(单次测量 + WSL2 噪声;多次复现这个差值会变,反过来的结果也见过)。这是个诚实且重要的结果:**「`-O3` 比 `-O2` 更优」是误解**,`-O3` 在数值/SIMD 友好的代码上有用(真向量化拿收益),在不规则、volatile、分支密集的代码上要么没向量化(本例)、要么偶尔调度反伤。所以 release 默认 `-O2`,只在**确认 `-O3` 有收益**的局部(数值热点)开 `-O3` 或 `-ftree-vectorize`。

## optimization blockers:三类编译器优化不了的

讲完「编译器能做的」,讲它「做不了的」。三类 blocker,每一类你都可能无意中触发:

### 1. 跨翻译单元(归 LTO,ch07-02)

编译器编译一个 `.cpp` 时,看不见别的 `.cpp` 里的实现。所以 `int helper(int)` 声明在头文件、实现在另一个 `.cpp` 时,**编译器不敢内联它**(不知道实现)。这是跨 TU blocker。解法:**LTO(链接期优化)**,链接时跨文件内联,ch07-02 实测 LTO 让一个跨 TU 调用快 **3.9 倍**。

### 2. 指针别名(aliasing)

C/C++ 允许两个指针指向同一地址(别名 aliasing)。编译器**默认假设两个指针可能别名**,于是不敢做激进的内存读写重排——因为它怕「写 `a[i]` 影响了 `b[i]`」。这个保守假设让很多本可向量化/重排的循环卡住。

实测(`scale_add`:循环里 `a[i] += b[i] * *scale`,`volatile` scale):

```text
  -O3 别名版(默认):   7.4 ms   ← 编译器不敢假设 a≠b
  -O3 __restrict 版:   5.8 ms   ← 你保证 a/b/scale 不别名,编译器敢优化
```

> ⚠️ 注:这个教学 demo 的归因其实**不干净**——配套代码里 alias 版签名是 `volatile int* scale`、restrict 版签名是 `int* __restrict scale`(scale 非 volatile),即 restrict 版**同时去掉了 `scale` 的 `volatile`**。所以 7.4→5.8 不全是别名分析的功劳,部分是去掉 `volatile` 后 `scale` 能被缓存/外提。干净的别名对照应保持两版 `scale` 类型一致、只动 `a`/`b` 上有没有 `__restrict`。这是教学 demo 的简化,实战对照要注意「只改一个变量」(呼应 ch00-01 的纪律)。

`__restrict`(C99 引入,C++ 是扩展但 GCC/Clang 都支持)是你向编译器承诺「这个指针不和别人别名」:

```cpp
void f(int* __restrict a, int* __restrict b, int* __restrict scale, int n);
```

承诺了,编译器就敢向量化/重排。代价:如果你撒谎(指针其实别名),**是 UB**。所以 `__restrict` 要在「你确信无别名」时用——典型场景是数值计算里独立的多个数组。**别滥用**,但数值热点里它是稳定收益。

> 注:`__restrict` 对引用也行(`const int& __restrict`),但语义稍微妙,查清楚再用。C++ 没有 `restrict` 关键字(那是 C 的),用 `__restrict`。

### 3. volatile

`volatile` 强制编译器**每次都真正读写内存**,不缓存到寄存器、不做任何优化。它本来是给 **MMIO(内存映射 IO)、信号处理、线程间无锁标志**用的——这些场景每次访问都必须真碰到内存(不能缓存)。但 `volatile` **是优化的反义词**——上面的测试里 `volatile scale` 迫使每次循环真 load,直接拖慢了循环。

实战:**别用 `volatile` 做「线程同步」或「性能」**。它不保证原子性、不保证内存序(那是 `std::atomic` 的事,vol5 讲),只保证「不优化」。多数性能代码里 `volatile` 是误用,该换成 `std::atomic` 或干脆去掉。

## 小结:你的工作是「别挡路」

- **`-O2` 是 release 甜点**;`-O3` 在数值热点有用、不规则的代码可能反伤(实测 -O3 比 -O2 慢)。
- **性能测试不用 -O0**(测的是没优化的代码,4 倍失真)。
- **三类 blocker**:跨 TU(用 LTO)、别名(用 `__restrict`)、`volatile`(别误用)。
- 编译器是你的性能队友,**别挡它的路**——让它看得见实现(LTO)、信你的无别名承诺(`__restrict`)、别用 `volatile` 禁它的优化。

## 参考资源

- GCC 手册 *Options That Control Optimization*(`-O0`/`-O1`/`-O2`/`-O3`/`-Os`/`-Oz` 各启用的 pass 清单)
- Agner Fog《Optimizing software in C++》§8 *Different C++ compilers》。本地
- CSAPP 第 5 章 *Optimizing Program Performance》(optimization blockers 的概念定义,别名/内存引用那套)
- 本篇实测代码:`code/volumn_codes/vol6-performance/ch07/opt_levels_blockers.cpp`
