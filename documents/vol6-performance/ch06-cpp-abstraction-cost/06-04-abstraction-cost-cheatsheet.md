---
chapter: 6
cpp_standard:
- 20
description: 汇总 vol6 这一章测过的 C++ 抽象成本(虚函数、异常、std::function、optional/variant/span 等的
  sizeof 与调用/构造开销)成一张速查表,加上变量存储类型(register/static/thread_local)、位域 bitfields、enum
  class 零开销三个补充条目,供写代码时案头查阅
difficulty: advanced
order: 4
platform: host
prerequisites:
- 虚函数与去虚拟化
- 异常的零成本模型
- std::function 的小缓冲区优化
reading_time_minutes: 5
related:
- RVO、NRVO 与 move 的真实成本
- C++ 抽象的性能成本(章首页)
tags:
- host
- cpp-modern
- advanced
- 优化
- 字面量
- enum_class
title: C++ 抽象的成本速查表
---
# C++ 抽象的成本速查表

这一篇是 ch06 的参考卡:把前面几篇测过的 C++ 抽象成本**汇总成一张速查表**,再补三个前面没单独立篇的小条目(变量存储类型、位域、enum class)。写代码时遇到「这个抽象贵不贵」可以这里查。

## 前面测过的:成本速查表

| 抽象 | 主要成本 | 实测数字(本机) | 何时在意 |
|---|---|---|---|
| **虚函数**(via 指针)| 查 vtable + 间接跳转 + 阻碍内联 | 0.55 ns,**2.5×** 于 CRTP | 热点虚调用且未被去虚化时 |
| **去虚拟化** | 编译器能证类型时常免费 | 直接对象 0.23 ns ≈ CRTP | 多数情况编译器替你做了 |
| **异常**(正常路径)| 表驱动零成本 | **0.25 ns(和纯函数一样)** | 几乎不用在意 |
| **异常**(抛出路径)| EH 表查 + 栈展开 | **857 ns,~3400×** | 异常路径别进热循环 |
| **`std::function`** 调用 | 类型擦除间接调用 | 1.61 ns,**6×** 于直接 lambda | 每帧百万次调用时 |
| **`std::function`** 构造 | 小走 SBO、大堆分配 | SBO 2.3 ns / 堆 19.6 ns(**8.5×**)| 热路径反复构造 + 大捕获 |
| **RVO/NRVO** | 返回值直接在调用方构造 | **0 次拷贝、0 次移动** | return 局部变量别写 std::move |
| **`return std::move(局部)`** | 禁用 NRVO,强制 move | 0 拷贝 + 1 move(多一次) | **反模式,别写** |

这张表是 ch06-01/02/03/05 的实测汇总,具体机制和实验见各篇。总命题(Carruth *No Zero-Cost Abstractions*):**每个 C++ 抽象都对应一个硬件成本**,但「有成本」不等于「每次都发生」,编译器常替你消除(去虚化、零成本异常、RVO)。**先测,再决定要不要手写绕开**。

## 补充条目

### 1. 变量存储类型:register / static / thread_local

变量的**存储类型**影响它在哪、访问多快(Agner 卷1 §7.1):

- **自动变量(栈)**:默认。访问最快(在 L1 命中的栈上),编译器还能放进寄存器。`register` 关键字在现代编译器已无意义(编译器自己分配寄存器),是 C++17 起的 deprecated/removed 关键字,别用。
- **静态变量(`static`/全局)**:固定地址,有固定初始化(常量初始化零开销;动态初始化有启动成本)。多线程下静态局部变量的初始化是线程安全的(magic statics),但**有线程安全初始化的运行时开销**(首次进入时的原子检查)。
- **`thread_local`**:每线程一份。访问稍贵(要查 TLS 的线程局部存储区,通常几条额外指令),但在多线程下避免共享。对「每线程的上下文对象」有用。

实战:热路径变量尽量是自动变量(让编译器放寄存器);`static` 全局常量免费;`thread_local` 用于 per-thread 上下文(它的初始化与销毁成本要算进线程生命周期)。

### 2. 位域 bitfields

**位域(bitfield)**把多个小字段压进一个整数里,省空间:

```cpp
struct Flags { unsigned a : 1; unsigned b : 1; unsigned c : 6; };  // 共 8 bit
```

好处:`sizeof` 小(紧凑),cache 友好。代价是**位运算**:读写位域成员是「读整字节 + 位掩码 + 位操作」,比读写普通 `int` 多几条指令。所以位域**省内存、费指令**。适合「有大量标志位、内存是瓶颈」(协议头、标志集合);不适合「单个字段被高频读写、算力是瓶颈」。Agner 卷1 §7.27 有详细权衡。

### 3. enum class:零开销

**`enum class`**(C++11 强类型枚举)是「带类型安全」的枚举,且**零开销**:底层就是一个 `int`(或你指定的 underlying type),访问和普通 `int` 一样快,**类型安全是编译期的,运行时零成本**。所以:

- 优先用 `enum class` 而非裸 `int` 常量(类型安全、可读性,免费)。
- 别担心它的性能,和 `int` 一样。
- 指定 underlying type(`enum class Color : uint8_t`)能控制 sizeof,省空间。

这是「零开销抽象」真正成立的少数案例之一(Carruth 命题的例外:不是所有抽象都有成本,`enum class`/`optional` 正常路径接近零成本)。

## sizeof 速查(本机实测,libstdc++ C++20)

```text
sizeof:
  int                              = 4
  std::optional<int>               = 8   (int 4B + 有无值标记 + padding)
  std::variant<int,double>         = 16  (double 8B + index + padding)
  std::variant<int,char,double,str>= 40  (string 32B + index + padding)
  std::span<int>                   = 16  (指针 + 长度,零所有权)
  std::string_view                 = 16  (指针 + 长度,不保证 \0)
  std::shared_ptr<int>             = 16  (2 指针:对象 + 控制块)
  std::unique_ptr<int>             = 8   (1 指针)
  std::string                      = 32  (含 SSO 缓冲)
  std::vector<int>                 = 24  (3 指针)
```

读法:`optional`/`variant` 多出来的字节是「有无值」标记和 index;`span`/`string_view` 是「指针+长度」的轻量视图(零所有权,几乎免费);`string` 32 字节里有 SSO 小缓冲(SSO 机制归 vol3)。

怎么用这张表:写代码时优先选零开销或近零开销的抽象(`enum class`、`span`/`string_view`、`optional`/`variant` 正常路径),它们让代码更安全且几乎不费性能;真正要关注的是虚函数调用(via 指针,未被去虚化)、`std::function` 反复构造 + 大捕获、异常进热循环这几个,真有成本、常需要手动优化;永远测了再优化,「听起来贵」的抽象可能编译器早消除了,「听起来免费」的(`std::function` 构造)可能正埋着堆分配。

下一篇是 ch06 最后一篇,讲 RVO/NRVO 与 move。它不是「抽象成本」,而是「值语义下返回大对象」的机制,常被误解。

## 参考资源

- Agner Fog《Optimizing software in C++》§7 *Variables / objects / containers》(变量存储类型、位域、enum)。本地
- Carruth *There Are No Zero-Cost Abstractions*(CppCon 2019)——「没有零开销抽象」命题
- ch06-01/02/03/05(本卷,各成本的实测出处)
- 本篇 sizeof 程序:`code/volumn_codes/vol6-performance/ch06/abstraction_sizeof.cpp`
