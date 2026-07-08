---
chapter: 0
cpp_standard:
- 23
description: 深入理解 C++23 的 std::move_only_function——OnceCallback 的核心存储类型，从 std::function
  的演进动机到 SBO 行为，再到为什么 OnceCallback 需要独立的三态管理
difficulty: intermediate
order: 5
platform: host
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
- OnceCallback 前置知识（一）：函数类型与模板偏特化
reading_time_minutes: 9
related:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 实战（六）：测试与性能对比
tags:
- host
- cpp-modern
- intermediate
- 函数对象
- 智能指针
title: OnceCallback 前置知识（五）：std::move_only_function (C++23)
---
# OnceCallback 前置知识（五）：std::move_only_function (C++23)

OnceCallback 的 `func_` 成员,类型是 `std::move_only_function<FuncSig>`。这玩意儿干的是脏活——类型擦除:把 lambda、函数指针、仿函数这些五花八门的可调用对象,都收编成一个固定签名的调用入口。咱们这一篇要把它拆开看清楚:它跟老 `std::function` 到底差在哪,SBO(小对象优化)给不给力,以及一个笔者自己踩过的坑——为什么 OnceCallback 还得多养一个自己的 `Status` 枚举,而不能图省事直接用它的判空。

## 从 std::function 到 std::move_only_function

### std::function 卡在哪

`std::function` 是 C++11 给的通用可调用对象容器,靠类型擦除把一锅可调用对象收成同一接口。但它有个要命的硬约束:要求被存的东西可拷贝。

根源在它自己能拷贝。您 copy 一个 `std::function`,它就得连带着把内部那个对象也拷一份。可如果您想塞进去的是一个捕了 `std::unique_ptr` 的 lambda 呢——unique_ptr 独占所有权,根本不让拷。结果就是,这行代码编译就给您拍脸上一个报错:

```cpp
#include <functional>
#include <memory>

auto ptr = std::make_unique<int>(42);

// 编译错误！unique_ptr 不可拷贝，std::function 要求可拷贝
std::function<int()> f = [p = std::move(ptr)]() { return *p; };
```

这事儿在 OnceCallback 这里直接顶到墙——OnceCallback 的招牌就是 move-only,它非得支持捕了 `unique_ptr` 的回调不可。

### std::move_only_function 怎么解的

C++23 的 `std::move_only_function`(还在 `<functional>` 里)就是冲着这块来的:把拷贝操作砍了,只留移动,被存的对象自然也不用满足可拷贝。

```cpp
#include <functional>
#include <memory>

auto ptr = std::make_unique<int>(42);

// OK！move_only_function 不要求可拷贝
std::move_only_function<int()> f = [p = std::move(ptr)]() { return *p; };

int result = f();  // result == 42
```

两者接口上的差别一句话能说清:`std::function` 能拷能移,被存对象必须能拷;`std::move_only_function` 只能移不能拷,被存对象只要能移就行。

---

## 构造、移动、调用、判空

构造的玩法跟 `std::function` 一个模子:`std::move_only_function<R(Args...)>` 张开手接任何签名对得上的可调用对象:lambda、函数指针、仿函数,连另一个 `std::move_only_function` 也行。默认构造出来就是空的,判空时跟 `nullptr` 一样。调用也就是熟悉的 `f(args...)` 语法,空对象被调了直接抛 `std::bad_function_call`,该崩让它崩。

```cpp
// 从 lambda 构造
std::move_only_function<int(int, int)> f1 = [](int a, int b) { return a + b; };

// 从函数指针构造
int add(int a, int b) { return a + b; }
std::move_only_function<int(int, int)> f2 = &add;

// 从仿函数构造
struct Multiplier {
    int operator()(int a, int b) { return a * b; }
};
std::move_only_function<int(int, int)> f3 = Multiplier{};

// 默认构造：创建空的 move_only_function
std::move_only_function<int()> f4;  // f4 == nullptr
```

真正值得停下来看一眼的是移动。语义很直白,源对象的可调用物整个搬家到目标。但搬完之后,源对象是个什么状态?标准给的是四个字:未指定(valid but unspecified)。它没保证源一定变空。

```cpp
std::move_only_function<int()> f = []() { return 42; };
auto g = std::move(f);
// f 的状态未指定——可能为空，也可能不为空
// 不要依赖 f 在移动后的行为
```

笔者顺手在 GCC 16 上跑了一把,实测 `bool(f)` 移动后确实给了 `false`。但请您记住,这是实现给的善意,不是标准给您兜底的承诺——换一家实现,明天它给您个 `true` 也不是不可能。这条尾巴很要紧,等会儿咱们看 OnceCallback 为什么还得养自己的 `Status` 枚举,根子有一半就在这儿。

判空走 `operator bool()` 或者直接跟 `nullptr` 比,两者等价。要主动清空就赋个 `nullptr` 进去,之前攥着的可调用对象随之析构:

```cpp
std::move_only_function<int()> f;
if (!f) {
    std::cout << "f is empty\n";
}
// 等价于
if (f == nullptr) {
    std::cout << "f is empty\n";
}

f = []() { return 42; };
if (f) {
    std::cout << "f is not empty\n";
}
```

```cpp
f = nullptr;  // 清空 f，析构之前持有的可调用对象
```

---

## SBO:小对象优化

`std::move_only_function` 内部跟 `std::function` 一样,做了小对象优化(Small Buffer Optimization,SBO)。套路不复杂:对象自留一块固定大小的缓冲区——通常是几个指针那么宽——可调用物个头够小,就把它直接塞进缓冲区,堆分配省了;要是太大装不下,退而求其次上堆。

![SBO 小对象优化内部结构](./pre-05-sbo-structure.drawio)

SBO 的阈值是实现自己定的,常见落在 2 到 3 个指针宽(16 到 24 字节)这个区间。捕的东西少的 lambda,像 `[x = 42]` 或 `[&ref]` 这种,基本都能蹭进 SBO,不触发堆分配。但要是 lambda 捕了一坨,比如一个 `std::string` 外加几个 `int`,撑过了阈值,构造时就得老实上堆。

### sizeof 实测对比

光说没意思,咱们量一下实物。GCC 16 上跑出来是这样:

```cpp
#include <functional>
#include <iostream>

int main() {
    std::cout << "sizeof(std::function<void()>):           "
              << sizeof(std::function<void()>) << "\n";
    std::cout << "sizeof(std::move_only_function<void()>): "
              << sizeof(std::move_only_function<void()>) << "\n";
}
```

```text
sizeof(std::function<void()>):           32
sizeof(std::move_only_function<void()>): 40
```

`std::function<void()>` 是 32 字节,`std::move_only_function<void()>` 多出 8 字节,做到 40。两者底层 SBO 思路相近,但 move-only 那一套——省掉了拷贝路径要做的事、留下了移动 vtable 之类——多出来的这点开销主要花在这儿。

---

## 为什么 OnceCallback 还得养自己的 Status 枚举

读到这儿您可能会问:既然 `std::move_only_function` 自己能判空,OnceCallback 怎么还多此一举,又在外面套了个 `Status` 枚举?笔者一开始也想图省事直接用它的判空,真动手才发现不够。

根子在状态数不够。`operator bool()` 只分得出"空"和"非空",可 OnceCallback 要分辨的是三态:

```cpp
enum class Status : uint8_t {
    kEmpty,     // 从未被赋值（默认构造）
    kValid,     // 持有有效的可调用对象
    kConsumed   // 已被 run() 调用过
};
```

"从来没被赋过值"(kEmpty)和"赋过、跑过、已经消化完了"(kConsumed)——这俩在 `operator bool()` 眼里都是空,可含义差远了。调试的时候,kEmpty 通常在提醒您"忘了给回调赋值",是个真 bug;kConsumed 是回调被正常调用完后的预期状态,正常得很。两种情况糊在一起,DCHECK 想说句明白话都没辙。

还有个更阴的,就是上一节那个"移动后状态未指定"。标准没保证移完之后 `operator bool()` 给 `false`——某些实现大可以还给个 `true`,里头东西却已经搬空了。OnceCallback 真要靠它判状态,移动一发生就可能误判。自己的 `Status` 就踏实了——它完全攥在咱们手里,移动构造的时候咱们显式把源对象标成 `kEmpty`,干净利落,没有半点含糊。

---

## 跟 Chromium BindState 放一块儿看

Chromium 那边没碰标准库的类型擦除,它自己手撸了一套 `BindState`。两套方案放一块儿,差异挺有意思。

Chromium 的 `BindState<Functor, BoundArgs...>` 是个堆上的对象,把可调用物和所有绑定参数全揽进去。`OnceCallback` 自己呢,就攥着一个指向 `BindState` 的智能指针(`scoped_refptr`),总共才 8 字节,一个指针那么宽。状态全堆到 `BindState` 那头,回调自己就是个瘦代理。

咱们这版把整个 `BindState` 层换成了 `std::move_only_function`——类型擦除和 SBO 它内部都给您办了,函数指针表、SBO 缓冲区、移动析构那一摊手写活儿全省了。代价是尺寸:从 8 字节长到 40 字节(`std::move_only_function` 自己),再叠上 `Status` 枚举和可选的 `CancelableToken` 指针,一个 `OnceCallback` 大概 56 到 64 字节的样子。

| 指标 | Chromium BindState | 我们的 std::move_only_function |
|------|-------------------|-------------------------------|
| 回调对象大小 | 8 字节（一个指针） | 56-64 字节 |
| 堆分配 | 总是（new BindState） | 仅当 lambda 超过 SBO 阈值 |
| 移动代价 | 复制一个指针 | 复制 32+ 字节 |
| 实现复杂度 | 很高（手写引用计数+函数指针表） | 低（复用标准库） |

对教学、对大多数实际场景,五六十字节的回调对象压根儿不是瓶颈。真要把尺寸压到极致,那就走 Chromium 那条路——核心思路咱们在后续实战篇里再细说。

下一篇是 OnceCallback 的最后一个前置点:C++23 的 deducing this(显式对象参数),`run()` 方法能靠它在编译期分清左值右值拦截,根子就在它身上。

## 参考资源

- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0288R9 - move_only_function 提案](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0288r9.html)
- [cppreference: std::function](https://en.cppreference.com/w/cpp/utility/functional/function)
