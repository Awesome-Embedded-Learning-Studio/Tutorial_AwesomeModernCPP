---
title: "CPU 微架构与存储层次"
description: "ch02 把单核硬件地基铺满:存储层次的延迟阶梯、缓存行与局部性、流水线与 ILP 和分支预测、TLB 与 huge page,每篇都配本机实测数据佐证,为 ch04 按瓶颈部位优化提供硬件底座"
---

# CPU 微架构与存储层次

ch00 立下了「先正确、再快」和「先测量、再优化」,ch01 把「先测量」做成了完整方法论。但「先测量」之后、「动手优化」之前,还差一块知识:你优化掉的到底是哪一种成本?把循环展开快了 3 倍,是因为缓存、还是指令级并行、还是分支预测?不知道根因,优化就是瞎碰。

这一章把单核硬件拆成四个层面,每个层面都用本机实测数据讲清「它在硬件上跑时发生了什么导致快/慢」:

- **存储层次**:L1/L2/L3/DRAM 的延迟阶梯逐级差出 100 倍,顺序访问能逼近 L1 吞吐靠的是预取器。
- **缓存行与局部性**:64 字节是 cache 的最小搬运单位,空间局部性决定连续布局快,行优先 vs 列优先遍历差 6 倍。
- **流水线与 ILP / 分支预测**:指令级并行决定执行单元饱不饱(多累加器快 3 倍),不可预测的分支被罚得很重(排序 vs 打乱差 4 倍)。
- **TLB 与 huge page**:虚拟地址翻译是另一道关,huge page 降 TLB 压力,但得先确认环境真给了。

这一章的数字(100 倍、6 倍、3 倍、4 倍)就是 ch04「按瓶颈部位优化」全部建议的物理根据——为什么用连续容器、为什么控热数据集、为什么多累加器、为什么 branchless、为什么除法是瓶颈、为什么前端 PGO 有用……每一条都能回溯到这里。深度上我们讲到「够支撑判断」就停,ROB / 寄存器重命名 / 执行端口调度这些更深的内容,给 Agner 微架构手册和 Wikichip 的指针。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="02-01-memory-hierarchy">存储层次与延迟阶梯:为什么顺序访问快 100 倍</ChapterLink>
  <ChapterLink href="02-02-cacheline-and-locality">缓存行与局部性:64 字节的最小搬运单位</ChapterLink>
  <ChapterLink href="02-03-pipeline-ilp-branch">流水线、ILP 与分支预测</ChapterLink>
  <ChapterLink href="02-04-tlb-hugepage-and-cpu-families">TLB、huge page 与各 CPU 族微架构速查</ChapterLink>
</ChapterNav>
