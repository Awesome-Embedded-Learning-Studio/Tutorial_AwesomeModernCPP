# 目录

这里是《面向嵌入式教程学习的现代C++教程》目录，点击我直接跳转到对应章节即可

## Chapter 0 - 前言与基础准备

- [前言](./chapter-00-introduction/00-preface.md)
- [嵌入式的资源与实时约束](./chapter-00-introduction/01-resource-and-realtime-constraints.md)
- [急速C语言速通复习](./chapter-00-introduction/02-c-language-crash-course.md)
- [快速过一下C++98的一些基础C++知识](./chapter-00-introduction/03-cpp98-basics.md)
- [何时用 C++、用哪些 C++ 特性（折中与禁用项）](./chapter-00-introduction/04-when-to-use-cpp.md)
- [语言选择原则：性能 vs 可维护性的真实取舍](./chapter-00-introduction/05-language-choice-performance-vs-maintainability.md)
- [C++一定导致代码膨胀嘛？](./chapter-00-introduction/06-evaluating-performance-and-size.md)

## Chapter 1 - 构建工具链

- [随意聊下交叉编译和CMake简单指南](./chapter-01-design-constraints/01-cross-compilation-and-cmake.md)
- [常见编译器选项指南](./chapter-01-design-constraints/02-compiler-options.md)
- [链接器与链接器脚本](./chapter-01-design-constraints/03-linker-and-linker-scripts.md)

## Chapter 2 - 零开销抽象

- [零开销抽象](./chapter-02-zero-overhead/01-zero-overhead-abstraction.md)
- [内联与编译器优化](./chapter-02-zero-overhead/02-inline-and-compiler-optimization.md)
- [constexpr](./chapter-02-zero-overhead/03-constexpr.md)
- [CRTP VS 运行时多态，你们知道吗？](./chapter-02-zero-overhead/04-crtp-vs-runtime-polymorphism.md)

## Chapter 3 - 内存与对象管理

- [初始化列表](./chapter-03-types-containers/01-initializer-lists.md)
- [移动语义](./chapter-03-types-containers/02-move-semantics.md)
- [RVO, NRVO](./chapter-03-types-containers/03-rvo-nrvo.md)
- [空基类优化（EBO）](./chapter-03-types-containers/04-empty-base-optimization.md)
- [对象大小，平凡类型](./chapter-03-types-containers/05-object-size-and-trivial-types.md)

## Chapter 4 - 编译期计算

- [constexpr 与设计技巧](./chapter-04-compile-time/01-constexpr-and-design-techniques.md)
- [consteval、constinit](./chapter-04-compile-time/02-consteval-constinit.md)
- [编译期实战](./chapter-04-compile-time/03-compile-time-in-practice.md)
- [if_constexpr](./chapter-04-compile-time/04-if-constexpr.md)

## Chapter 5 - 内存管理策略

- [动态分配问题](./chapter-05-memory-management/01-dynamic-allocation-issues.md)
- [静态存储与栈上分配策略](./chapter-05-memory-management/02-static-and-stack-allocation.md)
- [对象池模式](./chapter-05-memory-management/03-object-pool-pattern.md)
- [禁用 heap 或限制 heap 时的替代策略：放置new（Placement New）的使用](./chapter-05-memory-management/04-placement-new.md)
- [固定池分配](./chapter-05-memory-management/05-fixed-pool-allocation.md)
- [array vs 一般数组，你们知道嘛？](./chapter-05-memory-management/06-array-vs-raw-arrays.md)

## Chapter 6 - RAII与智能指针

- [RAII在外设管理的作用](./chapter-06-ownership-raii/01-raii-in-peripheral-management.md)
- [unique_ptr](./chapter-06-ownership-raii/02-unique-ptr.md)
- [shared_ptr](./chapter-06-ownership-raii/03-shared-ptr.md)
- [unique_ptr、shared_ptr 的嵌入式取舍](./chapter-06-ownership-raii/04-smart-ptr-embedded-tradeoffs.md)
- [intrusive 智能指针与引用计数（非堆实现）](./chapter-06-ownership-raii/05-intrusive-ptr-and-ref-counting.md)
- [自定义Deleter](./chapter-06-ownership-raii/06-custom-deleter.md)
- [引用计数](./chapter-06-ownership-raii/07-reference-counting.md)
- [Scope Guard](./chapter-06-ownership-raii/08-scope-guard.md)

## Chapter 7 - 容器与数据结构

- [array](./chapter-07-containers-spans/01-array.md)
- [span](./chapter-07-containers-spans/02-span.md)
- [循环缓冲区](./chapter-07-containers-spans/03-circular-buffer.md)
- [侵入式容器设计](./chapter-07-containers-spans/04-intrusive-containers.md)
- [ETL](./chapter-07-containers-spans/05-etl.md)
- [自定义的分配器](./chapter-07-containers-spans/06-custom-allocators.md)

## Chapter 8 - 类型安全与工具类型

- [enum class](./chapter-08-type-safety/01-enum-class.md)
- [类型安全的寄存器访问](./chapter-08-type-safety/02-type-safe-register-access.md)
- [variant](./chapter-08-type-safety/03-variant.md)
- [optional](./chapter-08-type-safety/04-optional.md)
- [expected](./chapter-08-type-safety/05-expected.md)
- [类型别名与using声明](./chapter-08-type-safety/06-type-aliases-and-using.md)
- [字面量运算符与自定义单位](./chapter-08-type-safety/07-literal-operators-and-custom-units.md)

## Chapter 9 - 函数式编程特性

- [Lambda表达式基础](./chapter-09-lambdas-functional/01-lambda-basics.md)
- [Lambda捕获与性能影响](./chapter-09-lambdas-functional/02-lambda-capture-and-performance.md)
- [std function vs 函数指针](./chapter-09-lambdas-functional/03-std-function-vs-function-ptr.md)
- [回调机制的零开销实现](./chapter-09-lambdas-functional/04-zero-overhead-callbacks.md)
- [std invoke与可调用对象](./chapter-09-lambdas-functional/05-std-invoke-and-callables.md)
- [函数式错误处理模式](./chapter-09-lambdas-functional/06-functional-error-handling.md)
- [C++20范围库基础与视图](./chapter-09-lambdas-functional/07-ranges-basics-and-views.md)
- [管道操作与Ranges实战](./chapter-09-lambdas-functional/08-ranges-pipeline-in-practice.md)

## Chapter 10 - 并发与原子操作

- [atomic](./chapter-10-concurrency/01-atomic.md)
- [memory_order](./chapter-10-concurrency/02-memory-order.md)
- [无锁数据结构设计](./chapter-10-concurrency/03-lock-free-data-structures.md)
- [mutex与RAII守卫](./chapter-10-concurrency/04-mutex-and-raii-guards.md)
- [中断安全的代码编写](./chapter-10-concurrency/05-interrupt-safe-coding.md)
- [临界区保护技术](./chapter-10-concurrency/06-critical-section-protection.md)

## Chapter 11 - 现代C++特性速览

- [auto与decltype](./chapter-11-modern-features/01-auto-and-decltype.md)
- [结构化绑定](./chapter-11-modern-features/02-structured-bindings.md)
- [范围for循环优化](./chapter-11-modern-features/03-range-based-for-optimization.md)
- [属性](./chapter-11-modern-features/04-attributes.md)
- [三路比较运算符](./chapter-11-modern-features/05-spaceship-operator.md)
- [指定初始化器](./chapter-11-modern-features/06-designated-initializers.md)
- [用户定义字面量](./chapter-11-modern-features/07-user-defined-literals.md)

## Chapter 12 - 模板基础

- [模板入门概述](./chapter-12-templates/00-template-overview.md)
- [函数模板详解](./chapter-12-templates/01-function-templates.md)
- [类模板详解](./chapter-12-templates/02-class-templates.md)
- [模板特化与偏特化](./chapter-12-templates/03-template-specialization.md)
- [非类型模板参数](./chapter-12-templates/04-non-type-template-params.md)
- [模板参数依赖与名字查找](./chapter-12-templates/05-template-args-and-name-lookup.md)
- [模板友元与Barton-Nackman技巧](./chapter-12-templates/06-template-friends-and-barton-nackman.md)
- [模板别名与Using声明](./chapter-12-templates/07-template-aliases-and-using.md)
- [模板与继承CRTP](./chapter-12-templates/08-templates-and-inheritance-crtp.md)
