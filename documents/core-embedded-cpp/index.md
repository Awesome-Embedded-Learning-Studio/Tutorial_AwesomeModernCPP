# 目录

这里是《面向嵌入式教程学习的现代C++教程》目录，点击我直接跳转到对应章节即可

## Chapter 0 - 前言与基础准备

- [前言](./chapter-00-introduction/0前言.md)
- [嵌入式的资源与实时约束](./chapter-00-introduction/1嵌入式的资源与实时约束.md)
- [急速C语言速通复习](./chapter-00-introduction/2急速C语言速通复习.md)
- [快速过一下C++98的一些基础C++知识](./chapter-00-introduction/3快速过一下C++98的基本特性.md)
- [何时用 C++、用哪些 C++ 特性（折中与禁用项）](./chapter-00-introduction/4何时用 C++、用哪些 C++ 特性（折中与禁用项）.md)
- [语言选择原则：性能 vs 可维护性的真实取舍](./chapter-00-introduction/5语言选择原则：性能 vs 可维护性的真实取舍.md)
- [C++一定导致代码膨胀嘛？](./chapter-00-introduction/6学习如何评估程序的性能和体积开销.md)

## Chapter 1 - 构建工具链

- [随意聊下交叉编译和CMake简单指南](./chapter-01-design-constraints/1随意聊下交叉编译和CMake简单指南.md)
- [常见编译器选项指南](./chapter-01-design-constraints/2常见编译器选项指南.md)
- [链接器与链接器脚本](./chapter-01-design-constraints/3链接器与链接器脚本.md)

## Chapter 2 - 零开销抽象

- [零开销抽象](./chapter-02-zero-overhead/1零开销抽象.md)
- [内联与编译器优化](./chapter-02-zero-overhead/2内联与编译器优化.md)
- [constexpr](./chapter-02-zero-overhead/3constexpr.md)
- [CRTP VS 运行时多态，你们知道吗？](./chapter-02-zero-overhead/4 CRTP VS 运行时多态，你们知道吗？.md)

## Chapter 3 - 内存与对象管理

- [初始化列表](./chapter-03-types-containers/1 初始化列表.md)
- [移动语义](./chapter-03-types-containers/2 移动语义.md)
- [RVO, NRVO](./chapter-03-types-containers/3 RVO, NRVO.md)
- [空基类优化（EBO）](./chapter-03-types-containers/4 空基类优化（EBO）.md)
- [对象大小，平凡类型](./chapter-03-types-containers/5对象大小，平凡类型.md)

## Chapter 4 - 编译期计算

- [constexpr 与设计技巧](./chapter-04-compile-time/1constexpr 与设计技巧.md)
- [consteval、constinit](./chapter-04-compile-time/2consteval、constinit.md)
- [编译期实战](./chapter-04-compile-time/3编译期实战.md)
- [if_constexpr](./chapter-04-compile-time/4if_constexpr.md)

## Chapter 5 - 内存管理策略

- [动态分配问题](./chapter-05-memory-management/1动态分配问题.md)
- [静态存储与栈上分配策略](./chapter-05-memory-management/2静态存储与栈上分配策略.md)
- [对象池模式](./chapter-05-memory-management/3对象池模式.md)
- [禁用 heap 或限制 heap 时的替代策略：放置new（Placement New）的使用](./chapter-05-memory-management/4禁用 heap 或限制 heap 时的替代策略：放置new（Placement New）的使用.md)
- [固定池分配](./chapter-05-memory-management/5固定池分配.md)
- [array vs 一般数组，你们知道嘛？](./chapter-05-memory-management/6array vs 一般数组，你们知道嘛？.md)

## Chapter 6 - RAII与智能指针

- [RAII在外设管理的作用](./chapter-06-ownership-raii/1 RAII在外设管理的作用.md)
- [unique_ptr](./chapter-06-ownership-raii/2 unique_ptr.md)
- [shared_ptr](./chapter-06-ownership-raii/3 shared_ptr.md)
- [unique_ptr、shared_ptr 的嵌入式取舍](./chapter-06-ownership-raii/4 unique_ptr、shared_ptr 的嵌入式取舍.md)
- [intrusive 智能指针与引用计数（非堆实现）](./chapter-06-ownership-raii/5 intrusive 智能指针与引用计数（非堆实现）.md)
- [自定义Deleter](./chapter-06-ownership-raii/6 自定义Deleter.md)
- [引用计数](./chapter-06-ownership-raii/7 引用计数.md)
- [Scope Guard](./chapter-06-ownership-raii/8 Scope Guard.md)

## Chapter 7 - 容器与数据结构

- [array](./chapter-07-containers-spans/1 array.md)
- [span](./chapter-07-containers-spans/2 span.md)
- [循环缓冲区](./chapter-07-containers-spans/3 循环缓冲区.md)
- [侵入式容器设计](./chapter-07-containers-spans/4 侵入式容器设计.md)
- [ETL](./chapter-07-containers-spans/5 ETL.md)
- [自定义的分配器](./chapter-07-containers-spans/6 自定义的分配器.md)

## Chapter 8 - 类型安全与工具类型

- [enum class](./chapter-08-type-safety/1 enum class.md)
- [类型安全的寄存器访问](./chapter-08-type-safety/2 类型安全的寄存器访问.md)
- [variant](./chapter-08-type-safety/3 variant.md)
- [optional](./chapter-08-type-safety/4 optional.md)
- [expected](./chapter-08-type-safety/5 expected.md)
- [类型别名与using声明](./chapter-08-type-safety/6 类型别名与using声明.md)
- [字面量运算符与自定义单位](./chapter-08-type-safety/7 字面量运算符与自定义单位.md)

## Chapter 9 - 函数式编程特性

- [Lambda表达式基础](./chapter-09-lambdas-functional/1 Lambda表达式基础.md)
- [Lambda捕获与性能影响](./chapter-09-lambdas-functional/2 Lambda捕获与性能影响.md)
- [std function vs 函数指针](./chapter-09-lambdas-functional/3 std function vs 函数指针.md)
- [回调机制的零开销实现](./chapter-09-lambdas-functional/4 回调机制的零开销实现.md)
- [std invoke与可调用对象](./chapter-09-lambdas-functional/5 std invoke与可调用对象.md)
- [函数式错误处理模式](./chapter-09-lambdas-functional/6 函数式错误处理模式.md)
- [C++20范围库基础与视图](./chapter-09-lambdas-functional/7 C++20范围库基础与视图.md)
- [管道操作与Ranges实战](./chapter-09-lambdas-functional/8 管道操作与Ranges实战.md)

## Chapter 10 - 并发与原子操作

- [atomic](./chapter-10-concurrency/1 atomic.md)
- [memory_order](./chapter-10-concurrency/2 memory_order.md)
- [无锁数据结构设计](./chapter-10-concurrency/3 无锁数据结构设计.md)
- [mutex与RAII守卫](./chapter-10-concurrency/4 mutex与RAII守卫.md)
- [中断安全的代码编写](./chapter-10-concurrency/5 中断安全的代码编写.md)
- [临界区保护技术](./chapter-10-concurrency/6 临界区保护技术.md)

## Chapter 11 - 现代C++特性速览

- [auto与decltype](./chapter-11-modern-features/1 auto与decltype.md)
- [结构化绑定](./chapter-11-modern-features/2 结构化绑定.md)
- [范围for循环优化](./chapter-11-modern-features/3 范围for循环优化.md)
- [属性](./chapter-11-modern-features/4 属性.md)
- [三路比较运算符](./chapter-11-modern-features/5 三路比较运算符.md)
- [指定初始化器](./chapter-11-modern-features/6 指定初始化器.md)
- [用户定义字面量](./chapter-11-modern-features/7 用户定义字面量.md)

## Chapter 12 - 模板基础

- [模板入门概述](./chapter-12-templates/0 模板入门概述.md)
- [函数模板详解](./chapter-12-templates/1 函数模板详解.md)
- [类模板详解](./chapter-12-templates/2 类模板详解.md)
- [模板特化与偏特化](./chapter-12-templates/3 模板特化与偏特化.md)
- [非类型模板参数](./chapter-12-templates/4 非类型模板参数.md)
- [模板参数依赖与名字查找](./chapter-12-templates/5 模板参数依赖与名字查找.md)
- [模板友元与Barton-Nackman技巧](./chapter-12-templates/6 模板友元与Barton-Nackman技巧.md)
- [模板别名与Using声明](./chapter-12-templates/7 模板别名与Using声明.md)
- [模板与继承CRTP](./chapter-12-templates/8 模板与继承CRTP.md)
