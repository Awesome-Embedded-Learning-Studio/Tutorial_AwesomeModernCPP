---
title: "C++ 抽象的性能成本"
description: "ch06 是 vol3「组件为什么这么设计」的性能镜像——用了之后硬件上发生了什么。逐个测:虚函数(去虚化常免费)、异常(零成本模型,正常免费/抛出贵 3400×)、std::function(SBO 与堆分配)、成本速查表(sizeof + 三补充)、RVO/NRVO 与 move(按值返回零成本)。总命题:没有零开销抽象,但先测再优化"
---

# C++ 抽象的性能成本

这一章是 vol3 的**性能镜像**。vol3 讲「vector/string/function 这些组件**为什么这么设计**」(设计动机),vol6 这一章讲「**用了之后,硬件上发生了什么导致快/慢**」。总命题是 Carruth 的 *There Are No Zero-Cost Abstractions*:**没有零开销抽象**,每个 C++ 抽象都对应一个硬件成本。

但这一章有个贯穿的反直觉精神:「有成本」**不等于**「每次都发生」。编译器常替你消除成本,去虚拟化让虚函数变直接调用、零成本模型让异常正常路径无开销、RVO 让按值返回零拷贝。所以这一章的建议反复是「**先测,别提前手写绕开**」。

五篇:

- **06-01 虚函数与去虚拟化**:via 指针的虚调用 0.55ns(2.5× CRTP),但编译器常去虚化。别提前 CRTP 化。
- **06-02 异常的零成本模型**:正常路径 0.25ns(零成本坐实),抛出 857ns(3400×)。异常只用于真异常。
- **06-03 std::function 的 SBO**:调用慢 6×,构造小走 SBO/大堆分配(8.5×)。热路径反复构造 + 大捕获要当心。
- **06-04 成本速查表**:汇总 + 变量存储类型 + 位域 + enum class 零开销 + sizeof。
- **06-05 RVO、NRVO 与 move**:按值返回零拷贝零移动(用 copy/move 计数法验证);`return std::move(局部)` 是反模式。

> 边界:组件**设计机制**(vector 三指针、SSO 实现、EBO)归 vol3/vol4;vol6 只讲「在硬件上跑的成本」。知乎高频问题(虚函数慢吗/异常慢吗/function 堆分配/move 反例/return std::move)融入各篇切入点,不单独立篇。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="06-01-virtual-devirtualization">虚函数与去虚拟化:别急着把虚函数改模板</ChapterLink>
  <ChapterLink href="06-02-exceptions-zero-cost">异常的零成本模型:正常路径免费,异常路径很贵</ChapterLink>
  <ChapterLink href="06-03-std-function-sbo">std::function 的小缓冲区优化:类型擦除的代价</ChapterLink>
  <ChapterLink href="06-04-abstraction-cost-cheatsheet">C++ 抽象的成本速查表</ChapterLink>
  <ChapterLink href="06-05-rvo-move">RVO、NRVO 与 move 的真实成本</ChapterLink>
</ChapterNav>
