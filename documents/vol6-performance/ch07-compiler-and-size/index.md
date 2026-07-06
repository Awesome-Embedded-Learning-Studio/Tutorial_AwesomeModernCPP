---
title: "编译器优化边界与体积评估"
description: "ch07 收尾编译器视角:-O 级别与 optimization blockers(跨 TU/别名/volatile)、LTO 跨 TU 内联(实测 3.9×)与 PGO(微基准无收益、大代码库有)、链接性能与多编译器对比与编译期元编程、体积优化(-Os/--gc-sections/模板膨胀)。整卷的工程收口"
---

# 编译器优化边界与体积评估

这是 vol6 的最后一章,从**编译器和链接器**的视角收尾。前面几章我们讲了怎么写「硬件跑得快的 C++ 代码」,这一章讲**编译器能替你做什么、做不了什么**——以及怎么配合它(让出视野、别挡路)。

四篇:

- **07-01 -O 级别与 blockers**:`-O2` 是 release 甜点(实测 -O0→-O2 快 4 倍),**-O3 偶尔反比 -O2 慢**(诚实);三类 blocker(跨 TU / 别名 / volatile)。
- **07-02 LTO 与 PGO**:LTO 跨 TU 内联实测 **3.9×**;PGO 对微基准无收益(诚实 null,那个一度的 4× 是仪器化开销),价值在大代码库。
- **07-03 链接、多编译器、元编程**:动态链接 PIC 代价(CSAPP ch7)、GCC/Clang/MSVC 差距小、编译期元编程的体积面。
- **07-04 体积优化**:`-Os`/`--gc-sections`/模板膨胀控制;体积↔速度常反向取舍。

贯穿全章的精神和 ch04 一致:**编译器是性能队友,你的工作是「别挡路」**——让它看得见实现(LTO)、信你的无别名承诺(`__restrict`)、别用 `volatile` 禁它的优化。release 默认开 LTO + `--gc-sections`,这是免费午餐;PGO 在大项目上加餐。

> 本机实测覆盖 07-01/02/04;07-03 偏概念(链接机制讲 CSAPP ch7,多编译器讲 Agner 卷1 §8),不重复造实验。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="07-01-opt-levels-and-blockers">-O 级别与 optimization blockers</ChapterLink>
  <ChapterLink href="07-02-lto-pgo">LTO、ThinLTO 与 PGO 的工程接入</ChapterLink>
  <ChapterLink href="07-03-linking-and-compilers">链接性能、多编译器对比与编译期元编程</ChapterLink>
  <ChapterLink href="07-04-size-optimization">体积优化:-Os、--gc-sections 与模板膨胀控制</ChapterLink>
</ChapterNav>
