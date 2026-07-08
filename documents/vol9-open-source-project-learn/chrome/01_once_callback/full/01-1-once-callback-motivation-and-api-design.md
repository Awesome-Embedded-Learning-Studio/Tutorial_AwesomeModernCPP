---
chapter: 1
cpp_standard:
- 23
description: 从一次真实的异步回调 bug 出发，拆解 std::function 在异步场景的三大缺陷，设计 OnceCallback 的完整目标 API
difficulty: beginner
order: 1
platform: host
prerequisites:
- OnceCallback 前置知识（一）：函数类型与模板偏特化
- OnceCallback 前置知识（五）：std::move_only_function
- OnceCallback 前置知识（六）：Deducing this
reading_time_minutes: 10
related:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
title: OnceCallback 实战（一）：动机与接口设计
---
# OnceCallback 实战（一）：动机与接口设计

## 从一个 bug 说起

笔者在异步编程上栽过最多次的坑，叫"回调被多调一次"。场景很俗——封装一个异步文件读取，注册个 I/O 完成的回调，指望它跑一次拉倒。结果某条错误重试路径手滑多触发了一次，回调里 `release_resources()` 跑第二遍，第二次访问的是已经释放的内存，段错误。这玩意儿最恶心的是测试里几乎复现不了，正常异步路径都只触发一次回调，真正的导火索是某种竞态或重试，得在生产环境的高并发下以极低概率冒头。笔者第一次撞这坑的时候盯了半个下午 dump，才发现不是逻辑错，是调用次数根本没人管。

`std::function` 在这种场景下帮不上忙。它能被调任意次，能被拷贝到满天飞，回调对象您想约束住——没门。咱们要的是把"只能调一次"这条规矩焊进类型系统，让编译器去查，而不是指望每个写回调的人都长记性。这一篇就先把动机和接口想透，下一篇再动手写代码。

### 场景：异步文件读取

假设咱们在写一个异步文件读取的封装。用户调用 `read_file_async(path, callback)`，I/O 完成后 `callback` 被触发一次，传入文件内容。

```cpp
void read_file_async(const std::string& path,
                     std::function<void(std::string)> callback);

// 使用
void on_file_read(std::string content) {
    process(content);        // 处理内容
    release_resources();     // 释放相关资源
}

read_file_async("data.txt", on_file_read);
```

看起来人畜无害。可一旦 I/O 子系统因为某种错误走了重试——回调被触发两次，`release_resources()` 跑两遍，第二遍摸的就是已释放的内存。段错误。测试里这条重试路径永远跑不到，bug 只在生产的高并发下以极低概率冒头。

### std::function 没帮上咱们

问题出在哪？`std::function<void(std::string)>` 的类型签名里，压根没藏着"这个回调该被调几次"这层信息。类型系统在这儿是缺席的，约束全靠运行时断言——前提是您写了——要么就靠程序员的纪律。

更要命的是，`std::function` 的几个特性还把这 bug 往更难查的方向推。它是可拷贝的，回调能被复制到任意多个地方。哪天多个执行路径同时攥着同一份副本，竞态就埋那儿了。它的 `operator()` 又是 `const` 限定的，调一下不会改动对象自身状态——所以"调用即消费"这个语义，您想从调用接口上表达出来都表达不了。

---

## std::function 的三大缺陷

把问题系统化一下。`std::function` 作为通用可调用对象容器，本身设计是成功的——这点笔者不否认。但塞进异步回调这个特定场景，它有仨致命的地方。

第一桩，可复制。`std::function` 天生支持拷贝，您拷一份过去，它内部的类型擦除机制会把存着的可调用对象也跟着拷一份。放到异步系统里什么意思？一个回调能被复制到任意多个地方——任务队列里一份、定时器里一份、错误处理器里又一份——每份都能独立调用。要是回调里捕获了 move-only 资源（比如 `std::unique_ptr`），拷贝直接编译挂；捕获的是裸指针或引用呢，多个副本同时跑就是竞态。Chrome 团队的思路直白得很：异步任务回调压根就不该被复制，那就让它在类型层面拷不动。

第二桩，可重复调用。`std::function::operator()` 对调用次数一句管束都没有，您在同一个对象上调一千遍它都照跑。可异步回调场景里，文件读取完成的回调被调两次就是实打实的逻辑错误——两次资源释放、两次状态转换、两次消息发送，爱哪样来哪样。这种错类型系统一个字都查不出来。

第三桩，也是最隐蔽的：没法表达消费语义。在 Chrome 的任务投递模型里，一次 `PostTask(FROM_HERE, callback)` 之后，`callback` 就不该再被碰了——它的所有权已经交给了任务系统。可 `std::function::operator()` 是 `const` 限定的，调一下不改对象状态，所以"调用即消费"这个语义，您想从接口上挂出去都挂不上去。

仨问题其实戳的是同一个点：`std::function` 的接口压根表达不了"这个回调只能调一次，调完即失效"这条约束。咱们的 OnceCallback，就是冲着填这个空缺去的。

---

## Chromium 的回答：OnceCallback 设计哲学

Chrome 的回调系统压在一条核心原则上：消息传递优于锁，序列化优于线程。顺着这条往下走，投递到任务系统的每个回调都是一条独立的、一次性的消息。投递之后，回调所有权就从调用方挪到任务系统；执行完，回调销毁。没共享，没复用，没歧义。

这套哲学直接刻在 `OnceCallback` 的类型设计上。先是 move-only：`OnceCallback` 把拷贝构造和拷贝赋值都删了，只留移动操作，类型层面就保证回调任意时刻只有一个持有者。再是右值限定的 `Run()`——它只能从右值上调，左值调直接编译报错，等于在语法上拽一把调用方："您在消费这个回调，回头别再碰了"。最后是单次消费，`Run()` 内部靠引用计数机制销毁 `BindState`，调完之后对同一对象的任何访问都是安全的空操作。三条加起来，"只能调一次"这事就从纪律问题变成了类型问题。

### Chromium 内部架构概览

Chromium 的回调系统叠了三层。最底下是 `BindStateBase`，类型擦除的基类，挂引用计数——它不走虚函数那条路，改用函数指针成员来玩多态。中间一层是 `BindState<Functor, BoundArgs...>`，模板化的具体类，真正的可调用对象和绑定参数都存这儿。最顶上是 `OnceCallback<Signature>`，用户直接上手的就是它，本质是 `BindState` 套了个智能指针壳子，大小才 8 字节。

咱们的实现会保留"外层接口 + 内部存储 + 类型擦除"这套分层骨架，但动手的地方有两处：用 `std::move_only_function` 替掉 Chromium 手写的 `BindState` + 引用计数组合，用 deducing this 替掉双重重载加 `!sizeof` hack。说白了，老一辈的体力活让现代语法干了。

---

## 设计目标 API

工程师的规矩：先把"我要什么"摆出来，再回头抠每个决策的为什么。咱们这就把目标 API 定下来。

### 构造与调用

```cpp
#include "once_callback/once_callback.hpp"

using namespace tamcpp::chrome;

// 从 lambda 构造
auto cb = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
});

// 调用：必须通过右值
int result = std::move(cb).run(3, 4);  // result == 7

// 调用后 cb 被消费
// std::move(cb).run(1, 2);  // 运行时断言失败
```

### 参数绑定

```cpp
// bind_once：预绑定部分参数，返回一个新的 OnceCallback
auto bound = bind_once<int(int)>(
    [](int x, int y, int z) { return x + y + z; },
    10, 20  // 预绑定前两个参数
);

int r = std::move(bound).run(30);  // r == 60
```

### 取消检查

```cpp
auto cb = OnceCallback<void(int)>([](int x) { /* ... */ });

// 检查回调是否仍然有效
if (!cb.is_cancelled()) {
    std::move(cb).run(42);
}

// maybe_valid：乐观检查
if (cb.maybe_valid()) {
    std::move(cb).run(42);
}
```

### 链式组合

```cpp
auto pipeline = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
}).then([](int sum) {
    return sum * 2;
});

int final_result = std::move(pipeline).run(3, 4);
// final_result == 14，因为 (3+4)*2 = 14
```

---

## 接口设计决策分析

### 为什么用 run() 而不是 operator()

Chromium 那边是 `Run()`，Google 风格要求首字母大写。咱们用 `run()`，对齐 snake_case 规范。往深一层讲，这其实是语义切分：`operator()` 太通用了，是个可调用对象就有 `operator()`；`run()` 这名字本身就在喊"我在执行一个任务"，code review 的时候一眼就能看出这儿在消费一个 OnceCallback，而不是随手调个普通函数。

### 为什么 run() 必须通过右值

这是整个设计里笔者最在意的一处。咱们用 deducing this 让编译器替咱们拦左值调用——写 `cb.run(args)` 而不是 `std::move(cb).run(args)`，编译器当场报错，错误信息还会明明白白告诉您该怎么改。这套机制前置知识（六）里讲过了，这里不重复。

### 为什么区分 is_cancelled() 和 maybe_valid()

差别在安全保证的强弱上。`is_cancelled()` 给的是确定性回答，只能在回调绑定的序列上调，保证返回准确结果；`maybe_valid()` 走的是乐观估计，能从任意线程调，但结果可能已经过时。在 Chromium 的完整实现里，这俩的区分跟线程安全保证是绑一块儿的。咱们的简化版暂时让两者语义一致，接口先留着，等后面真要扩了再分。

### 为什么 then() 消费 *this

`then()` 想表达的是"把当前回调的执行结果递给下一个回调"。这就要求当前回调在 `then()` 返回的新回调里被完整吃进去。要是 `then()` 不消费 `*this`，同一个回调就同时待在两个地方了——move-only 的语义当场破功。所以 `then()` 声明成右值限定成员函数，调完原回调就进入已消费态。

---

## 环境搭建

动手之前先把工具链点一点。OnceCallback 依赖 `std::move_only_function` 和 deducing this，俩都是 C++23 特性，环境不齐后面全白搭。

### 编译器要求

GCC 13+ 或 Clang 17+ 能完整撑起上面这套特性，编译加 `-std=c++23`。

### 验证代码

```cpp
#include <functional>

// 验证 std::move_only_function 可用
static_assert(__cpp_lib_move_only_function >= 202110L);

// 验证 deducing this 可用
struct Check {
    void test(this auto&& self) {}
};

int main() {
    Check c;
    c.test();
    return 0;
}
```

如果这段代码编译通过，环境就算齐了。

### CMake 最小配置

```cmake
cmake_minimum_required(VERSION 3.20)
project(once_callback_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(once_callback INTERFACE)
target_include_directories(once_callback INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
```

---

接口和动机到这儿算是捏出形状了。下一篇咱们就动手——从模板偏特化到三态管理，把 OnceCallback 的类骨架一根根搭起来。

## 参考资源

- [Chromium Callback 文档](https://chromium.googlesource.com/chromium/src/+/main/docs/callback.md)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this 提案](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
