---
chapter: 7
cpp_standard:
- 17
description: 二进制体积本身不是性能,但它通过 icache/iTLB/下载量间接影响性能(尤其嵌入式)。本篇讲体积优化三招:-Os/-Oz(体积优先的优化级别)、-ffunction-sections
  + --gc-sections(链接期回收死代码,实测省死代码)、模板膨胀控制(extern template、抽公共逻辑)。最后给一张「体积↔性能」的取舍清单
difficulty: advanced
order: 4
platform: host
prerequisites:
- -O 级别与 optimization blockers
- 链接性能、多编译器对比与编译期元编程
reading_time_minutes: 6
related:
- LTO、ThinLTO 与 PGO 的工程接入
- 前端优化:代码布局、PGO 与 BOLT
tags:
- host
- cpp-modern
- advanced
- 优化
- 链接器
- 工具链
title: 体积优化:-Os、--gc-sections 与模板膨胀控制
---
# 体积优化:-Os、--gc-sections 与模板膨胀控制

## 体积为什么影响性能

二进制体积本身不是「速度」,但它**间接影响性能**,主要三条路径:

1. **icache 容量有限**:代码体积大 → icache 装不下 → icache miss → Frontend Bound(ch04-07 讲过)。这是体积影响性能的主要机制。
2. **iTLB 有限**:代码页多 → iTLB 项需求多 → iTLB miss。
3. **下载/存储**:嵌入式 flash 受限、移动端 APK 大小、网络传输——体积直接是成本。

所以体积优化对**嵌入式(Flash 受限)**、**移动端(APK 大小)**、**大代码库(icache 压力)**有实际意义。这一篇讲三招标准手法。

## 第一招:-Os / -Oz(体积优先的优化级别)

`-Os` 是「优化到不膨胀的大小」,`-Oz`(Clang, GCC 也有)更激进地优先体积。它们和 `-O2`/`-O3` 的区别是**代价模型不同**:

- `-O2`:代价模型是「速度优先,体积其次」。
- `-Os`:代价模型是「体积优先,但别明显变慢」——它不开那些会让代码变大的优化(如激进循环展开)。
- `-Oz`:更偏体积,可能略微牺牲速度。

实测(`size_demo`,含死代码 + 模板多实例):

> ⚠️ 量代码体积用 `size` 命令的 text 段,别用 `ls -l`——整个 ELF 含头部/对齐/调试信息,会被污染,且 `-Os` 的 ELF 在某些编译器版本上可能反比 `-O2` 大。下面一律用 `size <binary>` 的 text 段(代码段)度量。

```text
            text     data     bss     (本机 GCC 16 量级)
-O2:        ~4144    ...      ...     ← 基线
-Os:        ~3740    ...      ...     ← 比 -O2 text 小
-Oz:        ~3740    ...      ...     ← 与 -Os 同量级
--gc-sections ~4017 ...      ...     ← 死代码回收后
```

绝对值随编译器版本变,但方向稳定:`-Os`/`-Oz` 的 text 比 `-O2` 小。

在这个小 demo 上差异很小(几百字节),因为程序本身就小。**在大项目上,`-Os` 比 `-O2` 通常省 5-15% 体积**。`-Os` 的代价是「可能略微变慢」(没做那些会膨胀的优化),所以适合「体积是硬约束」的场景(嵌入式 flash),不适合「速度优先」的桌面/服务器。

## 第二招:-ffunction-sections + --gc-sections(死代码回收)

这一招的思路:把每个函数/数据放独立段(section),链接期回收**没被引用的段**。两步:

```bash
# 编译:每个函数独立段
g++ -ffunction-sections -fdata-sections ...
# 链接:回收未引用段
g++ ... -Wl,--gc-sections
```

它解决的是**死代码**——那些定义了但从没被调用的函数(很常见:遗留代码、被条件编译禁用的旧路径、模板实例化但没用到的成员)。实测上面的 `--gc-sections` 比 `-O2` 省 200 字节(那个 demo 里有两个 `[[maybe_unused]]` 的死函数)。

大项目上 `--gc-sections` 收益显著:大型 C++ 项目常常有大量「链接进来但没用」的代码(尤其第三方库整库链接),`--gc-sections` 能砍掉几十个百分点。代价:几乎没有(编译/链接略慢,可忽略)。**release 构建默认应该开 `-ffunction-sections -fdata-sections -Wl,--gc-sections`**,几乎免费的体积优化。

> 注意:`--gc-sections` 回收的是「整个函数/数据未被引用」的段;函数内部的部分代码(比如某个 if 分支没被执行)它回收不了——那要靠 PGO 的代码布局。两者互补。

## 第三招:模板膨胀控制

模板对不同类型各实例化一份代码,容易膨胀。`vector<int>`、`vector<double>`、`vector<MyType>` 是三份独立的 `push_back`/`reserve`/扩容等代码。控制手法:

- **`extern template`(C++11)**:显式声明「这个模板实例在别的 TU 实例化,这里不重复生成」。大项目里把常用模板实例集中在一个 `.cpp` 里实例化一次,其它 TU 用 `extern template` 声明,避免每个 TU 都生成、链接期去重(去重也要时间)。

  ```cpp
  // common.h
  extern template class std::vector<int>;   // 声明:别在这生成
  // common.cpp
  template class std::vector<int>;           // 实例化一次
  ```

- **抽公共逻辑**:模板类型无关的部分抽到非模板基类/公共函数,只编一份。比如 `vector<T>` 的内存管理可以共享一个非模板的 `vector_base`。
- **别过度泛化**:只对真需要的类型实例化。`template<class T>` 用在一个只对 `int`/`double` 用的函数上,就只实例化这两个,别为了「通用」加一堆用不到的特化。

模板膨胀在大项目(尤其重用 STL/Boost)上能贡献可观体积。这三个手法是标准对策。

## 体积 ↔ 性能 的取舍清单

把三招和前面的放一起,按「体积优化 vs 性能影响」排个清单:

| 手法 | 体积 | 速度 | 何时用 |
|---|---|---|---|
| `-ffunction-sections` + `--gc-sections` | ↓↓ | 几乎不变 | **release 默认开**(免费)|
| `-Os`/`-Oz` | ↓ | 可能略降 | 体积硬约束(嵌入式)|
| `extern template` | ↓ | 不变 | 大项目重模板 |
| 抽公共逻辑(非模板基类) | ↓ | 可能略升(间接调用)| 谨慎权衡 |
| `-O3`(激进向量化/展开) | ↑↑ | 通常↑ 偶尔↓ | 速度优先,体积够用 |
| 模板过度泛化 | ↑↑ | — | 别这么写 |

核心取舍:**体积优化和速度优化经常反向**(`-Os` 省体积但可能慢、`-O3` 提速但膨胀)。**嵌入式优先体积、桌面/服务器优先速度、移动端居中**。先用 `--gc-sections`(免费的体积红利),再按场景选 `-O` 级别,最后才考虑 `extern template` 这类需要改代码的。

## 小结

- **体积影响性能**的主要机制是 icache/iTLB miss(还有嵌入式 flash、移动端下载)。
- **三招**:`-Os`/`-Oz`(体积优先优化级别,实测比 -O2 省几百字节到 5-15%)、`--gc-sections`(死代码回收,几乎免费,release 默认开)、模板膨胀控制(`extern template`、抽公共)。
- **体积 ↔ 速度常反向取舍**:嵌入式优先体积、桌面优先速度;`--gc-sections` 是免费的体积红利先拿。

这是 ch07 的最后一篇,也是 vol6 八章百科的收尾。整卷从「性能思维 + sanitizer 地基」开始,经过「测量方法论」「CPU 微架构」「归因方法论」「按瓶颈部位优化」「多核」「C++ 抽象成本」,到这里「编译器边界与体积」——一套从「先正确、先测量」到「对症下药」的完整性能工程方法论。

## 参考资源

- 现有 vol6 `06-evaluating-performance-and-size.md`(本篇是其扩写版的前置,已存在)
- Agner Fog《Optimizing assembly》§10 *Code size optimization》。本地
- GCC/Clang 文档 `-Os`/`-Oz`/`-ffunction-sections`/`-Wl,--gc-sections`/`extern template`
- CSAPP 第 7 章 *Linking*(`--gc-sections` 的链接机制背景)
- 本篇实测代码:`code/volumn_codes/vol6-performance/ch07/size_demo.cpp`
