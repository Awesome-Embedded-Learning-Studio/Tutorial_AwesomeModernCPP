---
title: "卷四 Lab 实验参考"
description: "卷四 Lab（编译期加工厂）的实验参考：五个步骤加 L5 挑战的逐步解答，每步标注知识点链接回教材章节，所有输出在 WSL Arch（g++ 16.1.1）真实运行得到；CRTP 汇编为真实 objdump 输出，报错对比为真实节选。"
chapter: 4
order: 4
tags:
  - host
  - advanced
  - cpp-modern
  - 模板
  - 模板元编程
  - concepts
  - CRTP
  - coroutine
difficulty: advanced
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 7
prerequisites: []
related: []
---

# 卷四 Lab 实验参考

> 所有输出在 WSL Arch（g++ 16.1.1）真实运行得到。建议卡住时先看「思路」逐步对照，实在写不出来再抄代码。

## 步骤 1：模板配方与四种实体 {#lab-1}

**思路**：四种实体各管一类「被参数化的东西」；函数模板实例化后是两个**地址不同**的真实函数。

1. 函数模板 `scale`、类模板 `Box`、变量模板 `golden`（C++14）、别名模板 `Vec`（C++11）各用一个实例。→ 知识点：[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)「C++ 里有四种模板实体」
2. `&scale<int>` 与 `&scale<double>` 是不同类型的函数指针（`int(*)(int)` vs `double(*)(double)`），**不能直接比较**，先转 `void*` 再比——地址不同，实锤「模板本身不生成代码、实例化才各抄一份」。→ 知识点：[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)「模板到底是什么：一份带占位符的代码配方」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra lab1.cpp -o lab1 && ./lab1
scale(3)   = 6
scale(2.5) = 5
box.value  = 7
golden<double> = 1.61803
Vec<int> size  = 3
scale<int> == scale<double> ? false
scale<int>    at 0x6491162a39c7
scale<double> at 0x6491162a39d5
```

两个地址相差 14 字节——`scale<int>` 和 `scale<double>` 是两份被独立编译的函数，模板本身只是配方。

## 步骤 2：手写 type_traits {#lab-2}

**思路**：主模板给默认值、偏特化针对类型「形状」覆盖——标准库 `<type_traits>` 的整套骨架。

1. `IsPointer<T*>` 同时匹配 `int*`（T=int）和 `int**`（T=int*），偏特化的 `T` 可以绑定任何类型、包括指针自己。→ 知识点：[模板特化与偏特化](../vol1-basics-cpp11-14/04-specialization-partial.md)「经典应用二：type_traits 的整套套路」
2. `RemoveRef<const int&&>` 的 `T` 绑定 `const int`（引用剥掉、cv 保留），所以结果是 `const int`——`static_assert` 实锤。→ 知识点：[模板特化与偏特化](../vol1-basics-cpp11-14/04-specialization-partial.md)「偏特化能匹配哪些模式」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra lab2.cpp -o lab2 && ./lab2
IsPointer<int*>      : true
IsPointer<int>       : false
IsPointer<int**>     : true
IsSame<int,long>     : false
RemoveRef<int&> is int : true
```

## 步骤 3：concepts 四成分与报错对比 {#lab-3}

**思路**：四种成分各查一件事；`EventB` 卡在嵌套要求（`long double` 16 字节超过 8）；两份报错一个讲 SFINAE 内部、一个讲你的约束。

1. `EventA` 四关全过；`EventB` 的 `id_type` 是 `long double`（本机 sizeof 16），嵌套要求 `sizeof <= 8` 不满足。→ 知识点：[Requires 表达式深度解析](../vol3-metaprogramming-cpp20-23/03-requires-expressions.md)「requires 表达式的四种成分」
2. enable_if 版报错的核心在「substitution of enable_if_t」——讲的是 SFINAE 的内部展开；concept 版点名 `constraints not satisfied` + `Arithmetic<T> [with T = ...basic_string...]`——讲的是你的约束。→ 知识点：[Concepts:把模板约束写进签名](../vol3-metaprogramming-cpp20-23/01-concepts.md)「报错信息的对比」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra lab3.cpp -o lab3 && ./lab3
EventA : true
EventB : false
int    : false
sizeof(long double) = 16
$ g++ -std=c++20 lab3_ei.cpp -o l3ei
lab3_ei.cpp:13:21: error: no matching function for call to 'process(std::string&)'
  • candidate 1: 'template<class T, class> T process(T)'
      • template argument deduction/substitution failed:
$ g++ -std=c++20 lab3_c.cpp -o l3c
lab3_c.cpp:16:21: error: no matching function for call to 'process(std::string&)'
  • candidate 1: 'template<class T>  requires  Arithmetic<T> T process(T)'
      • template argument deduction/substitution failed:
        • constraints not satisfied
          • ... in substitution of ... [with T = std::__cxx11::basic_string<char>]:
```

（报错节选，已截断。差别一目了然：enable_if 在讲自己的内部机制，concept 在点名你的约束和失败类型。）

## 步骤 4：CRTP 的汇编实证 {#lab-4}

**思路**：CRTP 的 `static_cast<Derived*>(this)->compute_impl()` 在实例化时钉死目标，-O2 全部内联；虚函数的目标运行时才定，绕不开 vtable。

1. CRTP 版两条指令：`mov $0x2a,%eax`（42 直接进返回值）+ `ret`——零内存访问。→ 知识点：[CRTP](../vol1-basics-cpp11-14/09-crtp.md)「零开销的汇编证据」
2. 虚函数版：`mov (%rdi),%rax` 取 vtable 指针、`mov (%rax),%rax` 取函数指针、比较（推测去虚化）、跳转——两次内存解引用加条件跳转。→ 知识点：[CRTP](../vol1-basics-cpp11-14/09-crtp.md)「虚函数版的要长得多」

**验证输出**：

```text
$ g++ -std=c++20 -O2 -c lab4_crtp.cpp -o lab4_crtp.o && objdump -d lab4_crtp.o
0000000000000000 <_Z8use_crtpv>:
   0:   b8 2a 00 00 00        mov    $0x2a,%eax
   5:   c3                    ret
$ g++ -std=c++20 -O2 -c lab4_virt.cpp -o lab4_virt.o && objdump -d lab4_virt.o
0000000000000000 <_Z11use_virtualR5BaseV>:
   0:   48 8b 07              mov    (%rdi),%rax
   3:   48 8d 15 00 00 00 00  lea    0x0(%rip),%rdx
   a:   48 8b 00              mov    (%rax),%rax
   d:   48 39 d0              cmp    %rdx,%rax
  10:   75 0e                 jne    20 <_Z11use_virtualR5BaseV+0x20>
  12:   b8 2a 00 00 00        mov    $0x2a,%eax
  17:   c3                    ret
  20:   ff e0                 jmp    *%rax
```

## 步骤 5：协程惰性序列 {#lab-5}

**思路**：`initial_suspend` 挂起 = 协程创建后一步不跑；`co_yield` 存值、挂起、把控制权交还——「无限」序列因此变成「按需」序列。

1. `counter()` 是 `while (true)` 的无限协程，但每次 `next()` 只 `resume` 一步：跑到 `co_yield` 就挂住，返回给调用方，下一次 `next()` 从挂起点继续。只取 5 个就是只算 5 步。→ 知识点：[协程基础](../01-coroutine-basics.md)「initial_suspend：初始暂停点」、[迭代器模式](../vol4-generics-patterns/18-iterator.md)「更进一步：用协程把遍历写成看起来像递归的样子」
2. `fibs()` 同理，前 10 项 `0 1 1 2 3 5 8 13 21 34`。`final_suspend` 返回 `suspend_always`，跑完也不自杀，由 `Generator` 析构 `handle_.destroy()` 收尸。→ 知识点：[协程基础](../01-coroutine-basics.md)「final_suspend：最终暂停点」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra lab5.cpp -o lab5 && ./lab5
counter: 0 1 2 3 4
fibs: 0 1 1 2 3 5 8 13 21 34
```

## 附加挑战（L5）：编译期 typelist 去重与求和 {#lab-l5}

**思路**：`UniqueImpl` 遍历列表时「没见过就压进累加器」——压头自然反序，所以最后要 `Reverse` 一次恢复原顺序；`SumSizes` 是逐项 `sizeof` 相加的编译期常量。

1. `Contains<H, Acc>` 用 `is_same` 逐个比对，`UniqueImpl` 按「是否已见」决定插不插；`Reverse` 尾递归 + 累加器是 typelist 的标配操作。→ 知识点：[模板特化与偏特化](../vol1-basics-cpp11-14/04-specialization-partial.md)（递归特化）、[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)「编译期图灵完备」
2. `SumSizes<L>` 的 18 是 `sizeof(char)+sizeof(int)+sizeof(char)+sizeof(int)+sizeof(double) = 1+4+1+4+8`——每一步都在模板实例化时算好，运行期打印的只是常量。→ 知识点：[非类型模板参数](../vol1-basics-cpp11-14/05-non-type-parameters.md)（`sizeof` 是编译期常量）、[模板元编程](../vol1-basics-cpp11-14/01-templates-introduction.md)

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra lab6.cpp -o lab6 && ./lab6
original : 1 4 1 4 8
unique   : 1 4 8
SumSizes<L> = 18
SumSizes<U> = 13
```

（来源标注：typelist 技术源自 Andrei Alexandrescu《Modern C++ Design》第 3 章（Loki 库）与 Boost.MPL 思路，本题按「只用编译期手段实现类型列表去重与求和」的竞赛挑战规格重出；L5 档位口径见[练习总览](../exercises/index.md)。）
