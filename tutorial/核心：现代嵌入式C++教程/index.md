# 目录

这里是《面向嵌入式教程学习的现代C++教程》目录，点击我直接跳转到对应章节即可

## Chapter 0 - 前言与基础准备

- [前言](./Chapter0/0前言.md)
- [嵌入式的资源与实时约束](./Chapter0/1嵌入式的资源与实时约束.md)
- [急速C语言速通复习](./Chapter0/2急速C语言速通复习.md)
- [快速过一下C++98的一些基础C++知识](./Chapter0/3快速过一下C++98的基本特性.md)
- [何时用 C++、用哪些 C++ 特性（折中与禁用项）](./Chapter0/4何时用 C++、用哪些 C++ 特性（折中与禁用项）.md)
- [语言选择原则：性能 vs 可维护性的真实取舍](./Chapter0/5语言选择原则：性能 vs 可维护性的真实取舍.md)
- [C++一定导致代码膨胀嘛？](./Chapter0/6学习如何评估程序的性能和体积开销.md)

## Chapter 1 - 构建工具链

- [随意聊下交叉编译和CMake简单指南](./Chapter1/1随意聊下交叉编译和CMake简单指南.md)
- [常见编译器选项指南](./Chapter1/2常见编译器选项指南.md)
- [链接器与链接器脚本](./Chapter1/3链接器与链接器脚本.md)

## Chapter 2 - 零开销抽象

- [零开销抽象](./Chapter2/1零开销抽象.md)
- [内联与编译器优化](./Chapter2/2内联与编译器优化.md)
- [constexpr](./Chapter2/3constexpr.md)
- [CRTP VS 运行时多态，你们知道吗？](./Chapter2/4 CRTP VS 运行时多态，你们知道吗？.md)

## Chapter 3 - 内存与对象管理

- [初始化列表](./Chapter3/1 初始化列表.md)
- [移动语义](./Chapter3/2 移动语义.md)
- [RVO, NRVO](./Chapter3/3 RVO, NRVO.md)
- [空基类优化（EBO）](./Chapter3/4 空基类优化（EBO）.md)
- [对象大小，平凡类型](./Chapter3/5对象大小，平凡类型.md)

## Chapter 4 - 编译期计算

- [constexpr 与设计技巧](./Chapter4/1constexpr 与设计技巧.md)
- [consteval、constinit](./Chapter4/2consteval、constinit.md)
- [编译期实战](./Chapter4/3编译期实战.md)
- [if_constexpr](./Chapter4/4if_constexpr.md)

## Chapter 5 - 内存管理策略

- [动态分配问题](./Chapter5/1动态分配问题.md)
- [静态存储与栈上分配策略](./Chapter5/2静态存储与栈上分配策略.md)
- [对象池模式](./Chapter5/3对象池模式.md)
- [禁用 heap 或限制 heap 时的替代策略：放置new（Placement New）的使用](./Chapter5/4禁用 heap 或限制 heap 时的替代策略：放置new（Placement New）的使用.md)
- [固定池分配](./Chapter5/5固定池分配.md)
- [array vs 一般数组，你们知道嘛？](./Chapter5/6array vs 一般数组，你们知道嘛？.md)

## Chapter 6 - RAII与智能指针

- [RAII在外设管理的作用](./Chapter6/1 RAII在外设管理的作用.md)
- [unique_ptr](./Chapter6/2 unique_ptr.md)
- [shared_ptr](./Chapter6/3 shared_ptr.md)
- [unique_ptr、shared_ptr 的嵌入式取舍](./Chapter6/4 unique_ptr、shared_ptr 的嵌入式取舍.md)
- [intrusive 智能指针与引用计数（非堆实现）](./Chapter6/5 intrusive 智能指针与引用计数（非堆实现）.md)
- [自定义Deleter](./Chapter6/6 自定义Deleter.md)
- [引用计数](./Chapter6/7 引用计数.md)
- [Scope Guard](./Chapter6/8 Scope Guard.md)

## Chapter 7 - 容器与数据结构

- [array](./Chapter7/1 array.md)
- [span](./Chapter7/2 span.md)
- [循环缓冲区](./Chapter7/3 循环缓冲区.md)
- [侵入式容器设计](./Chapter7/4 侵入式容器设计.md)
- [ETL](./Chapter7/5 ETL.md)
- [自定义的分配器](./Chapter7/6 自定义的分配器.md)

## Chapter 8 - 类型安全与工具类型

- [enum class](./Chapter8/1 enum class.md)
- [类型安全的寄存器访问](./Chapter8/2 类型安全的寄存器访问.md)
- [variant](./Chapter8/3 variant.md)
- [optional](./Chapter8/4 optional.md)
- [expected](./Chapter8/5 expected.md)
- [类型别名与using声明](./Chapter8/6 类型别名与using声明.md)
- [字面量运算符与自定义单位](./Chapter8/7 字面量运算符与自定义单位.md)

## Chapter 9 - 函数式编程特性

- [Lambda表达式基础](./Chapter9/1 Lambda表达式基础.md)
- [Lambda捕获与性能影响](./Chapter9/2 Lambda捕获与性能影响.md)
- [std function vs 函数指针](./Chapter9/3 std function vs 函数指针.md)
- [回调机制的零开销实现](./Chapter9/4 回调机制的零开销实现.md)
- [std invoke与可调用对象](./Chapter9/5 std invoke与可调用对象.md)
- [函数式错误处理模式](./Chapter9/6 函数式错误处理模式.md)
- [C++20范围库基础与视图](./Chapter9/7 C++20范围库基础与视图.md)
- [管道操作与Ranges实战](./Chapter9/8 管道操作与Ranges实战.md)

## Chapter 10 - 并发与原子操作

- [atomic](./Chapter10/1 atomic.md)
- [memory_order](./Chapter10/2 memory_order.md)
- [无锁数据结构设计](./Chapter10/3 无锁数据结构设计.md)
- [mutex与RAII守卫](./Chapter10/4 mutex与RAII守卫.md)
