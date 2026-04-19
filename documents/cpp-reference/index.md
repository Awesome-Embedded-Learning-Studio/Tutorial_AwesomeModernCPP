---
title: "C++ 特性参考卡"
description: "C++98 至 C++23 全部主要特性的速查索引，按标准版本和功能类别双视图组织"
chapter: 99
order: 1
tags:
  - host
  - cpp-modern
  - 入门
difficulty: beginner
---

<!-- markdownlint-disable MD051 -->

# C++ 特性参考卡

覆盖 C++98 至 C++23 全部主要特性的结构化速查索引。已有参考卡的特性可直接点击跳转，查看核心 API 签名、最小可编译示例、嵌入式适用性和编译器支持信息。尚未创建参考卡的特性以纯文本列出，后续批次逐步补充。

> 遇到某个特性想快速查语法？来这里。想系统学习？去看对应卷的教程文章。

## 快速导航

**按标准版本：**
[C++98/03](#c9803) | [C++11](#c11) | [C++14](#c14) | [C++17](#c17) | [C++20](#c20) | [C++23](#c23)

**按功能类别：**
[内存管理](#内存管理) | [容器与视图](#容器与视图) | [并发](#并发) | [核心语言特性](#核心语言特性) | [模板与元编程](#模板与元编程)

!!! info "图例"
    - **适用性**：高 = 嵌入式强烈推荐，中 = 按场景选用，低 = 通常不需要
    - 蓝色链接 = 已有参考卡，纯文本 = 参考卡待补充
    - 头文件栏标注"语言特性"表示无需 `#include` 的核心语言机制

=== "按标准版本"

    ### C++98/03

    C++98（ISO/IEC 14882:1998）是第一个 ISO 标准化版本，奠定了 STL 容器/算法/迭代器三大支柱、异常处理、命名空间和模板等核心机制——这些特性至今仍是 C++ 编程的日常基础设施。C++03 是缺陷修复版本，仅澄清了值初始化语义等细节，无重大新特性。

    | 特性 | 头文件 | 简述 | 适用性 |
    |------|--------|------|--------|
    | STL 容器 (vector, list, deque, map, set...) | `<vector>` 等 | 顺序/关联/无序容器族 | **高** |
    | STL 算法 (sort, find, transform...) | `<algorithm>` | 排序/查找/变换等通用算法 | **高** |
    | STL 迭代器 | `<iterator>` | 统一遍历接口 | **高** |
    | std::string | `<string>` | 可变长度字符串 | **高** |
    | iostream | `<iostream>` | 类型安全的 I/O 流 | **中** |
    | RAII (构造/析构/拷贝语义) | 语言特性 | 资源获取即初始化 | **高** |
    | 异常处理 (try/catch/throw) | 语言特性 | 结构化错误处理 | **中** |
    | 命名空间 (namespace) | 语言特性 | 防止名称冲突 | **高** |
    | 类模板 / 函数模板 | 语言特性 | 泛型编程基础 | **高** |
    | 运算符重载 | 语言特性 | 自定义类型运算行为 | **中** |
    | 函数对象 (仿函数) | `<functional>` | 可调用对象与适配器 | **中** |
    | RTTI (dynamic_cast, typeid) | `<typeinfo>` | 运行时类型识别 | **低** |
    | std::complex / std::valarray | `<complex>` | 数值计算支持 | **低** |

    ### C++11

    C++11 是现代 C++ 的起点，带来了 lambda、auto、移动语义、智能指针、并发支持库等革命性特性。从这一版开始，C++ 从"带类的 C"进化为真正的高效抽象语言——这是学习 Modern C++ 的第一站。

    | 特性 | 头文件 | 简述 | 适用性 |
    |------|--------|------|--------|
    | [std::unique_ptr](memory/01-unique-ptr.md) | `<memory>` | 独占所有权智能指针 | **高** |
    | [std::shared_ptr](memory/02-shared-ptr.md) | `<memory>` | 共享所有权智能指针 | **中** |
    | std::weak_ptr | `<memory>` | 打破 shared_ptr 循环引用 | **中** |
    | [lambda 表达式](core-language/02-lambda.md) | 语言特性 | 匿名函数对象 | **高** |
    | [auto](core-language/03-auto-decltype.md) | 语言特性 | 自动类型推导 | **高** |
    | [decltype](core-language/03-auto-decltype.md) | 语言特性 | 表达式类型查询 | **高** |
    | [constexpr](core-language/01-constexpr.md) | 语言特性 | 编译期常量与函数 | **高** |
    | 范围 for (range-for) | 语言特性 | 容器遍历语法糖 | **高** |
    | 移动语义 (右值引用) | 语言特性 | 资源转移而非拷贝 | **高** |
    | std::move / std::forward | `<utility>` | 移动与完美转发工具 | **高** |
    | nullptr | 语言特性 | 类型安全的空指针常量 | **高** |
    | enum class | 语言特性 | 作用域强类型枚举 | **高** |
    | override / final | 语言特性 | 虚函数显式标注 | **高** |
    | static_assert | 语言特性 | 编译期断言 | **高** |
    | 可变参数模板 | 语言特性 | 任意数量模板参数 | **高** |
    | std::initializer_list | `<initializer_list>` | 统一初始化列表 | **高** |
    | std::array | `<array>` | 编译期固定大小数组 | **高** |
    | std::tuple | `<tuple>` | 异构固定大小容器 | **中** |
    | std::unordered_map / set | `<unordered_map>` | 哈希表容器 | **中** |
    | std::function | `<functional>` | 多态函数包装器 | **中** |
    | 用户自定义字面量 | 语言特性 | 自定义字面量后缀 | **中** |
    | 委托/继承构造 | 语言特性 | 构造函数复用 | **中** |
    | alignas / alignof | 语言特性 | 对齐控制与查询 | **中** |
    | std::thread | `<thread>` | 平台无关线程 | **高** |
    | std::mutex / lock_guard | `<mutex>` | 互斥锁与 RAII 锁 | **高** |
    | std::atomic | `<atomic>` | 无锁原子操作 | **高** |
    | std::condition_variable | `<condition_variable>` | 条件变量同步 | **中** |
    | std::future / async | `<future>` | 异步任务与结果获取 | **中** |
    | std::chrono | `<chrono>` | 时间库 | **高** |

    ### C++14

    C++14 是对 C++11 的完善与打磨——放宽 constexpr 限制、引入泛型 lambda 和 std::make_unique。改动不大但实用性强，几乎所有特性在嵌入式开发中都能直接使用。

    | 特性 | 头文件 | 简述 | 适用性 |
    |------|--------|------|--------|
    | std::make_unique | `<memory>` | 异常安全的 unique_ptr 创建 | **高** |
    | 泛型 lambda | 语言特性 | lambda 参数使用 auto | **高** |
    | 返回类型推导 (auto return) | 语言特性 | 函数返回值 auto 推导 | **中** |
    | constexpr 扩展 | 语言特性 | 放宽 constexpr 限制（循环/局部变量） | **高** |
    | decltype(auto) | 语言特性 | 完美转发返回类型推导 | **中** |
    | std::exchange | `<utility>` | 替换并返回旧值 | **中** |
    | std::integer_sequence | `<utility>` | 编译期整数序列 | **中** |
    | 二进制字面量 (0b) | 语言特性 | 0b 前缀二进制整数 | **中** |
    | 数位分隔符 (') | 语言特性 | 撇号分隔数字提升可读性 | **低** |
    | std::shared_timed_mutex | `<shared_mutex>` | 定时共享互斥锁 | **低** |

    ### C++17

    C++17 引入了结构化绑定、if constexpr、string_view、optional、variant 等高频特性，显著提升了日常编码的表达力。CTAD 和保证拷贝消除了大量样板代码，std::filesystem 补齐了文件操作的短板。

    | 特性 | 头文件 | 简述 | 适用性 |
    |------|--------|------|--------|
    | [std::optional](memory/03-optional.md) | `<optional>` | 可选值包装 | **高** |
    | [std::variant](containers/03-variant.md) | `<variant>` | 类型安全的联合体 | **中** |
    | [std::string_view](containers/02-string-view.md) | `<string_view>` | 零拷贝字符串视图 | **高** |
    | std::any | `<any>` | 类型安全的任意值容器 | **低** |
    | std::filesystem | `<filesystem>` | 文件系统操作 | **中** |
    | 结构化绑定 | 语言特性 | 多返回值解构 | **高** |
    | if constexpr | 语言特性 | 编译期条件分支 | **高** |
    | 折叠表达式 | 语言特性 | 参数包展开运算 | **高** |
    | CTAD | 语言特性 | 类模板参数推导 | **高** |
    | 保证拷贝消除 | 语言特性 | 强制消除临时对象拷贝 | **高** |
    | std::invoke | `<functional>` | 统一调用接口 | **中** |
    | std::apply | `<tuple>` | tuple 展开为函数参数 | **中** |
    | 内联变量 | 语言特性 | 头文件中定义全局变量 | **中** |
    | std::byte | `<cstddef>` | 独立字节类型 | **中** |
    | std::pmr 内存资源 | `<memory_resource>` | 多态分配器内存资源 | **中** |
    | std::shared_mutex | `<shared_mutex>` | 读写锁 | **中** |
    | 嵌套命名空间 (A::B::C) | 语言特性 | 命名空间简写 | **低** |
    | if/switch 初始化语句 | 语言特性 | 条件语句内变量声明 | **中** |

    ### C++20

    C++20 是继 C++11 之后最大的更新：Concepts、Ranges、Coroutines、Modules 四大特性彻底改变了模板编程、数据管道、异步流程和代码组织方式。同时 std::format、std::span、三路比较等特性显著改善了日常开发体验。编译器支持要求较高（GCC 10+ / Clang 10+）。

    | 特性 | 头文件 | 简述 | 适用性 |
    |------|--------|------|--------|
    | [Concepts](templates/01-concepts.md) | `<concepts>` | 编译期模板参数约束 | **高** |
    | Ranges | `<ranges>` | 组合式范围与视图 | **高** |
    | [std::span](containers/01-span.md) | `<span>` | 连续序列的非拥有视图 | **高** |
    | std::format | `<format>` | 类型安全的格式化输出 | **高** |
    | std::jthread | `<thread>` | 自动 join 的线程类 | **高** |
    | 三路比较 (<=>) | `<compare>` | 统一比较运算符 | **高** |
    | Coroutines | `<coroutine>` | 无栈协程 | **高** |
    | Modules | 语言特性 | 替代头文件的编译单元 | **高** |
    | consteval | 语言特性 | 强制编译期求值 | **中** |
    | constinit | 语言特性 | 编译期静态变量初始化 | **中** |
    | std::source_location | `<source_location>` | 编译期源码位置信息 | **中** |
    | 指定初始化器 | 语言特性 | 按成员名初始化聚合体 | **中** |
    | std::atomic_ref | `<atomic>` | 原子引用操作 | **中** |
    | std::latch / barrier | `<latch>` | 线程同步原语 | **中** |
    | std::stop_token | `<stop_token>` | 协作式线程取消 | **中** |
    | std::erase / erase_if | `<vector>` 等 | 容器元素删除统一接口 | **中** |
    | std::is_constant_evaluated | `<type_traits>` | 检测编译期求值上下文 | **中** |
    | range-for 初始化语句 | 语言特性 | range-for 带初始化 | **低** |

    ### C++23

    C++23 对 C++20 进行打磨与补齐：std::expected、std::print、std::generator、std::flat_map 等实用库组件填补了关键空白，deducing this 等语言改进精简了成员函数的写法。部分特性编译器支持仍在推进中（GCC 14+ / Clang 18+）。

    | 特性 | 头文件 | 简述 | 适用性 |
    |------|--------|------|--------|
    | std::expected | `<expected>` | 错误处理包装类型 | **中** |
    | std::print / println | `<print>` | 格式化输出到 stdout | **高** |
    | std::generator | `<generator>` | 协程同步生成器 | **中** |
    | std::flat_map / flat_set | `<flat_map>` | 基于连续存储的有序容器 | **中** |
    | std::mdspan | `<mdspan>` | 多维数组非拥有视图 | **中** |
    | std::stacktrace | `<stacktrace>` | 调用栈捕获与打印 | **中** |
    | deducing this | 语言特性 | 显式对象参数推导 | **中** |
    | std::to_underlying | `<utility>` | 枚举转底层类型 | **中** |
    | std::out_ptr / inout_ptr | `<memory>` | 智能指针与 C 指针互操作 | **中** |
    | optional monadic 操作 | `<optional>` | and_then / or_else / transform | **中** |
    | Ranges 新适配器 | `<ranges>` | zip / chunk / slide / enumerate 等 | **中** |
    | if consteval | 语言特性 | 编译期求值条件判断 | **低** |
    | std::unreachable | `<utility>` | 标记不可达代码 | **低** |
    | 多维下标运算符 | 语言特性 | operator[] 接受多参数 | **低** |
    | std::is_scoped_enum | `<type_traits>` | 检测作用域枚举类型 | **低** |

=== "按功能类别"

    ### 内存管理

    智能指针、可选值、错误处理等内存与资源管理相关特性。详见 [内存管理参考卡](memory/index.md)。

    | 特性 | 版本 | 头文件 | 简述 | 适用性 |
    |------|------|--------|------|--------|
    | [std::unique_ptr](memory/01-unique-ptr.md) | C++11 | `<memory>` | 独占所有权智能指针 | **高** |
    | [std::shared_ptr](memory/02-shared-ptr.md) | C++11 | `<memory>` | 共享所有权智能指针 | **中** |
    | std::weak_ptr | C++11 | `<memory>` | 打破 shared_ptr 循环引用 | **中** |
    | std::make_unique | C++14 | `<memory>` | 异常安全的 unique_ptr 创建 | **高** |
    | [std::optional](memory/03-optional.md) | C++17 | `<optional>` | 可选值包装 | **高** |
    | std::pmr 内存资源 | C++17 | `<memory_resource>` | 多态分配器内存资源 | **中** |
    | std::expected | C++23 | `<expected>` | 错误处理包装类型 | **中** |
    | std::out_ptr / inout_ptr | C++23 | `<memory>` | 智能指针与 C 指针互操作 | **中** |
    | optional monadic 操作 | C++23 | `<optional>` | and_then / or_else / transform | **中** |

    ??? note "待补充参考卡"
        以下特性尚未创建参考卡：std::weak_ptr、std::make_unique、std::pmr 内存资源、std::expected、std::out_ptr / inout_ptr、optional monadic 操作

    ### 容器与视图

    标准容器、视图、字符串、格式化、算法等数据组织与操作特性。详见 [容器与视图参考卡](containers/index.md)。

    | 特性 | 版本 | 头文件 | 简述 | 适用性 |
    |------|------|--------|------|--------|
    | STL 容器 (vector, list, deque, map, set...) | C++98 | `<vector>` 等 | 顺序/关联/无序容器族 | **高** |
    | STL 算法 | C++98 | `<algorithm>` | 排序/查找/变换等通用算法 | **高** |
    | std::string | C++98 | `<string>` | 可变长度字符串 | **高** |
    | std::array | C++11 | `<array>` | 编译期固定大小数组 | **高** |
    | std::tuple | C++11 | `<tuple>` | 异构固定大小容器 | **中** |
    | std::unordered_map / set | C++11 | `<unordered_map>` | 哈希表容器 | **中** |
    | std::function | C++11 | `<functional>` | 多态函数包装器 | **中** |
    | [std::string_view](containers/02-string-view.md) | C++17 | `<string_view>` | 零拷贝字符串视图 | **高** |
    | [std::variant](containers/03-variant.md) | C++17 | `<variant>` | 类型安全的联合体 | **中** |
    | std::any | C++17 | `<any>` | 类型安全的任意值容器 | **低** |
    | std::filesystem | C++17 | `<filesystem>` | 文件系统操作 | **中** |
    | Ranges | C++20 | `<ranges>` | 组合式范围与视图 | **高** |
    | [std::span](containers/01-span.md) | C++20 | `<span>` | 连续序列的非拥有视图 | **高** |
    | std::format | C++20 | `<format>` | 类型安全的格式化输出 | **高** |
    | std::erase / erase_if | C++20 | `<vector>` 等 | 容器元素删除统一接口 | **中** |
    | std::flat_map / flat_set | C++23 | `<flat_map>` | 基于连续存储的有序容器 | **中** |
    | std::generator | C++23 | `<generator>` | 协程同步生成器 | **中** |
    | std::print / println | C++23 | `<print>` | 格式化输出到 stdout | **高** |
    | std::mdspan | C++23 | `<mdspan>` | 多维数组非拥有视图 | **中** |
    | Ranges 新适配器 | C++23 | `<ranges>` | zip / chunk / slide / enumerate 等 | **中** |

    ??? note "待补充参考卡"
        以下特性尚未创建参考卡：STL 容器、STL 算法、std::string、std::array、std::tuple、std::unordered_map/set、std::function、std::any、std::filesystem、Ranges、std::format、std::erase/erase_if、std::flat_map/flat_set、std::generator、std::print、std::mdspan、Ranges 新适配器

    ### 并发

    线程、锁、原子操作、同步原语等并发与多线程特性。详见 [并发参考卡](concurrency/index.md)。

    | 特性 | 版本 | 头文件 | 简述 | 适用性 |
    |------|------|--------|------|--------|
    | std::thread | C++11 | `<thread>` | 平台无关线程 | **高** |
    | std::mutex / lock_guard | C++11 | `<mutex>` | 互斥锁与 RAII 锁 | **高** |
    | std::atomic | C++11 | `<atomic>` | 无锁原子操作 | **高** |
    | std::condition_variable | C++11 | `<condition_variable>` | 条件变量同步 | **中** |
    | std::future / async | C++11 | `<future>` | 异步任务与结果获取 | **中** |
    | std::chrono | C++11 | `<chrono>` | 时间库 | **高** |
    | std::shared_timed_mutex | C++14 | `<shared_mutex>` | 定时共享互斥锁 | **低** |
    | std::shared_mutex | C++17 | `<shared_mutex>` | 读写锁 | **中** |
    | std::jthread | C++20 | `<thread>` | 自动 join 的线程类 | **高** |
    | std::atomic_ref | C++20 | `<atomic>` | 原子引用操作 | **中** |
    | std::latch / barrier | C++20 | `<latch>` | 线程同步原语 | **中** |
    | std::stop_token | C++20 | `<stop_token>` | 协作式线程取消 | **中** |

    ??? note "待补充参考卡"
        并发分类下所有特性的参考卡均待补充。第二批实施时将优先覆盖 std::atomic、std::thread、std::mutex/lock_guard。

    ### 核心语言特性

    关键字、语法糖、类型系统、编译期机制等核心语言特性。详见 [核心语言参考卡](core-language/index.md)。

    | 特性 | 版本 | 头文件 | 简述 | 适用性 |
    |------|------|--------|------|--------|
    | RAII (构造/析构/拷贝) | C++98 | 语言特性 | 资源获取即初始化 | **高** |
    | 异常处理 | C++98 | 语言特性 | 结构化错误处理 | **中** |
    | 命名空间 | C++98 | 语言特性 | 防止名称冲突 | **高** |
    | 运算符重载 | C++98 | 语言特性 | 自定义类型运算行为 | **中** |
    | iostream | C++98 | `<iostream>` | 类型安全的 I/O 流 | **中** |
    | [lambda 表达式](core-language/02-lambda.md) | C++11 | 语言特性 | 匿名函数对象 | **高** |
    | [auto](core-language/03-auto-decltype.md) | C++11 | 语言特性 | 自动类型推导 | **高** |
    | [decltype](core-language/03-auto-decltype.md) | C++11 | 语言特性 | 表达式类型查询 | **高** |
    | [constexpr](core-language/01-constexpr.md) | C++11 | 语言特性 | 编译期常量与函数 | **高** |
    | 范围 for | C++11 | 语言特性 | 容器遍历语法糖 | **高** |
    | 移动语义 (右值引用) | C++11 | 语言特性 | 资源转移而非拷贝 | **高** |
    | std::move / std::forward | C++11 | `<utility>` | 移动与完美转发工具 | **高** |
    | nullptr | C++11 | 语言特性 | 类型安全的空指针 | **高** |
    | enum class | C++11 | 语言特性 | 作用域强类型枚举 | **高** |
    | override / final | C++11 | 语言特性 | 虚函数显式标注 | **高** |
    | static_assert | C++11 | 语言特性 | 编译期断言 | **高** |
    | 用户自定义字面量 | C++11 | 语言特性 | 自定义字面量后缀 | **中** |
    | 委托/继承构造 | C++11 | 语言特性 | 构造函数复用 | **中** |
    | alignas / alignof | C++11 | 语言特性 | 对齐控制与查询 | **中** |
    | 泛型 lambda | C++14 | 语言特性 | lambda 参数使用 auto | **高** |
    | 返回类型推导 | C++14 | 语言特性 | 函数返回值 auto | **中** |
    | constexpr 扩展 | C++14 | 语言特性 | 放宽 constexpr 限制 | **高** |
    | decltype(auto) | C++14 | 语言特性 | 完美转发返回类型推导 | **中** |
    | 二进制字面量 | C++14 | 语言特性 | 0b 前缀二进制整数 | **中** |
    | 结构化绑定 | C++17 | 语言特性 | 多返回值解构 | **高** |
    | if constexpr | C++17 | 语言特性 | 编译期条件分支 | **高** |
    | CTAD | C++17 | 语言特性 | 类模板参数推导 | **高** |
    | 保证拷贝消除 | C++17 | 语言特性 | 强制消除临时对象拷贝 | **高** |
    | 内联变量 | C++17 | 语言特性 | 头文件定义全局变量 | **中** |
    | std::byte | C++17 | `<cstddef>` | 独立字节类型 | **中** |
    | 嵌套命名空间 | C++17 | 语言特性 | A::B::C 简写 | **低** |
    | if/switch 初始化语句 | C++17 | 语言特性 | 条件语句内变量声明 | **中** |
    | 三路比较 (<=>) | C++20 | `<compare>` | 统一比较运算符 | **高** |
    | Coroutines | C++20 | `<coroutine>` | 无栈协程 | **高** |
    | Modules | C++20 | 语言特性 | 替代头文件 | **高** |
    | consteval | C++20 | 语言特性 | 强制编译期求值 | **中** |
    | constinit | C++20 | 语言特性 | 编译期静态初始化 | **中** |
    | std::source_location | C++20 | `<source_location>` | 编译期源码位置 | **中** |
    | 指定初始化器 | C++20 | 语言特性 | 按成员名初始化聚合体 | **中** |
    | deducing this | C++23 | 语言特性 | 显式对象参数推导 | **中** |
    | std::to_underlying | C++23 | `<utility>` | 枚举转底层类型 | **中** |
    | std::unreachable | C++23 | `<utility>` | 标记不可达代码 | **低** |
    | if consteval | C++23 | 语言特性 | 编译期求值条件判断 | **低** |
    | 多维下标运算符 | C++23 | 语言特性 | operator[] 多参数 | **低** |
    | std::stacktrace | C++23 | `<stacktrace>` | 调用栈捕获与打印 | **中** |

    ??? note "待补充参考卡"
        以下高频特性将在后续批次优先创建参考卡：移动语义、std::move/std::forward、nullptr、enum class、static_assert、结构化绑定、if constexpr、CTAD、三路比较、Coroutines、Modules

    ### 模板与元编程

    模板、约束、类型特征、编译期计算等泛型编程与元编程特性。详见 [模板与元编程参考卡](templates/index.md)。

    | 特性 | 版本 | 头文件 | 简述 | 适用性 |
    |------|------|--------|------|--------|
    | 类模板 / 函数模板 | C++98 | 语言特性 | 泛型编程基础 | **高** |
    | 可变参数模板 | C++11 | 语言特性 | 任意数量模板参数 | **高** |
    | std::initializer_list | C++11 | `<initializer_list>` | 统一初始化列表 | **高** |
    | std::integer_sequence | C++14 | `<utility>` | 编译期整数序列 | **中** |
    | 折叠表达式 | C++17 | 语言特性 | 参数包展开运算 | **高** |
    | std::invoke | C++17 | `<functional>` | 统一调用接口 | **中** |
    | std::apply | C++17 | `<tuple>` | tuple 展开为函数参数 | **中** |
    | [Concepts](templates/01-concepts.md) | C++20 | `<concepts>` | 编译期模板参数约束 | **高** |
    | std::is_constant_evaluated | C++20 | `<type_traits>` | 检测编译期上下文 | **中** |
    | std::is_scoped_enum | C++23 | `<type_traits>` | 检测作用域枚举 | **低** |

    ??? note "待补充参考卡"
        以下特性尚未创建参考卡：可变参数模板、std::initializer_list、std::integer_sequence、折叠表达式、std::invoke、std::apply、std::is_constant_evaluated、std::is_scoped_enum

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
