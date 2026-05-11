---
title: "OnceCallback 前置知识速查：C++11/14/17 核心特性回顾"
description: "一篇快速复习 OnceCallback 系列所需的所有 C++ 基础特性——移动语义、完美转发、可变参数模板、智能指针、atomic、lambda、类型特征等，为后续深度学习做好准备"
chapter: 0
order: 0
tags:
  - host
  - cpp-modern
  - intermediate
  - 基础
  - 入门
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 25
prerequisites:
  - "卷一 C++ 基础入门"
related:
  - "OnceCallback 前置知识（一）：函数类型与模板偏特化"
  - "OnceCallback 前置知识（三）：Lambda 高级特性"
---

# OnceCallback 前置知识速查：C++11/14/17 核心特性回顾

## 引言

说实话，这一篇不是给你"从零讲明白"的——如果你对移动语义、智能指针这些概念完全陌生，建议先回到卷二把对应章节啃完再回来。这一篇的角色是**速查手册**：我们把 OnceCallback 系列后面会反复用到的 C++ 特性全部拉出来过一遍，每个特性只讲三件事——"它是什么"、"怎么用"、"OnceCallback 里哪里会用到"。目的是让你在读后续文章的时候不会因为某个语法细节卡住。

> **学习目标**
>
> - 快速回顾 OnceCallback 系列所需的全部 C++11/14/17 基础特性
> - 理解每个特性在 OnceCallback 设计中的具体应用位置
> - 建立后续深度学习所需的知识基线

---

## 移动语义与 std::move

移动语义是整个 OnceCallback 的根基——它是一个 move-only 类型，核心设计全靠移动语义撑着。我们先快速过一遍核心概念。

### 右值引用与移动构造

C++11 引入了右值引用 `T&&`，它能绑定到临时对象（右值）上。移动构造函数 `T(T&& other)` 的语义是"从 `other` 那里把资源**偷**过来，而不是复制一份"。偷完之后，`other` 进入一个"有效但未指定"的状态——通常是被清空。

```cpp
// 一个最简单的移动语义示例
class Buffer {
    int* data_;
    std::size_t size_;
public:
    // 普通构造
    Buffer(std::size_t n) : data_(new int[n]), size_(n) {}
    // 移动构造：偷走 other 的资源
    Buffer(Buffer&& other) noexcept
        : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;   // 清空源对象
        other.size_ = 0;
    }
    ~Buffer() { delete[] data_; }
};

Buffer a(100);          // a 拥有 100 个 int
Buffer b = std::move(a); // b 偷走了 a 的资源，a 变空
```

### std::move 的本质

`std::move` 其实什么都不移动——它只是一个 `static_cast<T&&>`，把传入的对象无条件转换成右值引用。真正执行"移动"的是移动构造函数或移动赋值运算符。`std::move` 的角色是告诉编译器"我同意把这个对象当作右值对待，你可以从它那里偷资源"。

### 在 OnceCallback 中的应用

OnceCallback 的调用方式是 `std::move(cb).run(args...)`——`std::move` 把 `cb` 转成右值，`run()` 通过 deducing this（C++23 特性，后面有专门一篇文章讲）检测到这是一个右值调用，执行回调并把 `cb` 的状态标记为"已消费"。之后任何对 `cb` 的访问都是非法的。整个设计思路就是：**通过类型系统来强制约束"调用一次即失效"的语义**。

OnceCallback 同时删除了拷贝构造和拷贝赋值（`= delete`），只保留移动操作。这意味着一个 OnceCallback 对象在任意时刻只有一个持有者——你没法复制它，只能通过 `std::move` 转移所有权。

---

## 完美转发与 std::forward

完美转发解决的问题是：你写了一个函数模板，它接受参数并原封不动地传给另一个函数。所谓"原封不动"是指保持参数的值类别（左值还是右值）和 const 修饰。

### 转发引用与推导规则

当函数模板的参数是 `T&&` 且 `T` 是模板参数时，`T&&` 不是普通的右值引用，而是**转发引用**（也叫万能引用）。编译器会根据传入参数的值类别来推导 `T`：

- 传入左值 `x`（类型 `int`）→ `T = int&`，`T&&` 折叠为 `int&`
- 传入右值 `42`（类型 `int`）→ `T = int`，`T&&` 就是 `int&&`

### std::forward 的作用

`std::forward<T>(arg)` 根据模板参数 `T` 的类型决定是返回左值引用还是右值引用：

```cpp
template<typename T>
void wrapper(T&& arg) {
    // std::forward 保持 arg 的原始值类别
    target(std::forward<T>(arg));
}

int x = 10;
wrapper(x);    // arg 是左值引用，forward 返回左值引用
wrapper(10);   // arg 是右值引用，forward 返回右值引用
```

如果你不用 `std::forward` 而直接传 `arg`，那 `arg` 在函数内部永远是左值（因为具名变量都是左值），右值信息就丢失了。

### 在 OnceCallback 中的应用

完美转发在 OnceCallback 里出现了很多次。`bind_once` 函数模板用它来保持绑定参数的值类别——`std::forward<BoundArgs>(args)...` 确保传入的右值仍然是右值，传入的左值仍然是左值。`run()` 方法的 deducing this 实现里也用到了 `std::forward<Self>(self)` 来将 `self` 的值类别完美转发给内部的 `impl_run`。

---

## 可变参数模板与参数包展开

可变参数模板让你写出一个接受任意数量、任意类型参数的函数或类。OnceCallback 的模板签名 `OnceCallback<R(Args...)>` 就用到了参数包。

### 基本语法

```cpp
template<typename... Types>  // Types 是参数包
void print_all(Types... args) {
    // args... 在这里展开
    // sizeof...(Types) 返回参数数量
}
```

`Types...` 叫做参数包（parameter pack），它可以包含零个或多个类型。`args...` 是函数参数包，在调用时展开。`sizeof...(Types)` 是编译期常量，返回包中元素的数量。

### 展开位置

参数包可以在多个位置展开：函数参数列表、模板参数列表、初始化列表、捕获列表（C++20 起）等。OnceCallback 里最关键的一个展开位置是 lambda 的捕获列表——这个特性在 C++20 才引入，我们后面有专门一篇文章讲。

### 在 OnceCallback 中的应用

`OnceCallback<R(Args...)>` 的 `Args...` 就是一个参数包，它在类的整个实现中反复出现——构造函数的参数类型、`run()` 的参数类型、内部 `func_` 的签名，全部来自这个包。`bind_once` 的 `BoundArgs...` 是另一个参数包，展开到 lambda 的捕获列表和 `std::invoke` 的调用参数中。

---

## 智能指针速查

OnceCallback 内部用到了两种智能指针，我们快速过一下各自的角色。

### std::unique_ptr：独占所有权

`unique_ptr` 是独占式的智能指针——同一时刻只有一个 `unique_ptr` 指向对象。它不可拷贝，只能移动。创建方式是 `std::make_unique<T>(args...)`。

```cpp
auto p = std::make_unique<int>(42);
// auto p2 = p;             // 编译错误：不可拷贝
auto p3 = std::move(p);    // OK：移动转移所有权
// 此后 p 为 nullptr
```

在 OnceCallback 中，`unique_ptr` 的意义不在于我们直接使用它，而在于 OnceCallback 必须支持捕获 move-only 对象的 lambda——如果一个 lambda 捕获了 `unique_ptr`，那么包含这个 lambda 的 `std::move_only_function`（OnceCallback 的内部存储）也必须是 move-only 的。这是 `std::function` 做不到的，也是我们选择 `std::move_only_function` 的原因之一。

### std::shared_ptr：共享所有权

`shared_ptr` 通过引用计数管理对象生命周期。所有指向同一对象的 `shared_ptr` 共享同一个引用计数，最后一个 `shared_ptr` 被销毁时对象也被销毁。

```cpp
auto p1 = std::make_shared<int>(42);
auto p2 = p1;   // OK：拷贝，引用计数 +1
// p1 和 p2 都指向同一个 int
```

在 OnceCallback 中，`shared_ptr` 用于管理取消令牌 `CancelableToken`。令牌需要在 OnceCallback 对象和外部控制方之间共享——外部控制方调用 `invalidate()` 使令牌失效，OnceCallback 在执行回调前通过自己持有的 `shared_ptr` 副本检查令牌状态。`shared_ptr` 的引用计数保证了只要还有人持有令牌，底层的 `Flag` 对象就不会被销毁。

---

## std::atomic 与 memory_order

取消令牌的内部实现用到了 `std::atomic<bool>` 和 `memory_order_acquire/release`。

### 原子操作

`std::atomic<T>` 提供对 `T` 类型变量的原子访问——读和写不会被其他线程的操作打断。基本操作是 `load()`（读）和 `store()`（写），可以指定内存序。

```cpp
std::atomic<bool> flag{true};

// 线程 A：写入
flag.store(false, std::memory_order_release);

// 线程 B：读取
if (flag.load(std::memory_order_acquire)) {
    // flag 仍然为 true
}
```

### acquire/release 语义

`memory_order_release` 和 `memory_order_acquire` 是一对配对的内存序。简单说：`release` store 保证了在 store 之前的所有写操作对其他线程可见；`acquire` load 保证了在 load 之后的所有读操作能看到 release store 之前的写入。在 OnceCallback 的取消令牌中，`invalidate()` 用 `release` store 把 `valid` 设为 `false`，`is_valid()` 用 `acquire` load 读取 `valid`——这保证了如果 `is_valid()` 返回 `true`，令牌相关的所有状态对当前线程都是可见的。

---

## enum class

`enum class` 是 C++11 引入的作用域枚举，解决的是老式 `enum` 的名字污染和隐式转换问题。

```cpp
// 老式 enum：名字污染全局命名空间，可以隐式转成 int
enum Color { Red, Green, Blue };
int x = Red;  // OK，隐式转换

// enum class：名字被限定在枚举作用域内，不可隐式转换
enum class Status : uint8_t {
    kEmpty,    // 从未被赋值
    kValid,    // 持有有效的可调用对象
    kConsumed  // 已被 run() 消费
};
Status s = Status::kValid;
// int y = s;  // 编译错误：不可隐式转换
```

OnceCallback 用 `enum class Status` 来区分回调的三种状态。底层类型指定为 `uint8_t` 是为了节省内存——整个枚举只占 1 个字节。

---

## Lambda 基础

Lambda 在 OnceCallback 中无处不在——构造回调、`bind_once`、`then()` 的内部实现全部依赖 lambda。这里快速复习基础语法。

```cpp
auto add = [](int a, int b) { return a + b; };
// add 的类型是编译器生成的唯一闭包类

int x = 10;
// 值捕获：拷贝 x
auto f1 = [x]() { return x; };
// 引用捕获：引用 x（注意生命周期）
auto f2 = [&x]() { return x; };
// 初始化捕获（C++14）：可以移动捕获
auto f3 = [p = std::make_unique<int>(42)]() { return *p; };
```

Lambda 生成的闭包类的 `operator()` 默认是 `const` 的——这意味着你不能在 lambda 内部修改值捕获的变量，除非加上 `mutable` 关键字。在 OnceCallback 的 `bind_once` 和 `then()` 实现中，lambda 必须声明为 `mutable`，因为内部需要调用 `std::move(self).run()` 来修改 `self` 的状态。这个细节我们在 Lambda 高级特性那篇文章里会展开讲。

泛型 lambda（C++14 起）允许参数使用 `auto`：

```cpp
auto generic = [](auto x, auto y) { return x + y; };
// 编译器为 operator() 生成模板版本
```

`bind_once` 内部的 lambda 用 `(auto&&... call_args)` 来接受运行时参数——这里的 `auto&&` 是转发引用（因为 `auto` 等同于模板参数）。

---

## 类型特征（Type Traits）

类型特征是编译期查询和操作类型信息的工具。OnceCallback 里用到了几个关键的 traits，我们快速过一遍。

```cpp
#include <type_traits>

// std::decay_t<T>：去掉 T 上的引用、const/volatile 限定符，数组变指针，函数变函数指针
using T1 = std::decay_t<const int&>;       // T1 = int
using T2 = std::decay_t<OnceCallback&&>;   // T2 = OnceCallback（去掉引用）

// std::is_same_v<A, B>：A 和 B 是否是同一类型
static_assert(std::is_same_v<int, int>);           // 通过
static_assert(!std::is_same_v<int, double>);       // 通过

// std::is_lvalue_reference_v<T>：T 是否是左值引用类型
static_assert(std::is_lvalue_reference_v<int&>);      // 通过
static_assert(!std::is_lvalue_reference_v<int>);      // 通过
static_assert(!std::is_lvalue_reference_v<int&&>);    // 通过

// std::is_void_v<T>：T 是否是 void
static_assert(std::is_void_v<void>);            // 通过
static_assert(!std::is_void_v<int>);            // 通过
```

在 OnceCallback 中，`std::decay_t` 和 `std::is_same_v` 用于 `not_the_same_t` concept——它检查"模板参数退化后是否和 `OnceCallback` 本身是同一类型"，用来防止模板构造函数劫持移动构造函数的调用。`std::is_lvalue_reference_v` 用于 `run()` 的 deducing this 实现——检测调用方是否传了左值，如果是就触发 `static_assert` 报错。`std::is_void_v` 用于 `impl_run()` 和 `then()` 中区分 void 和非 void 返回类型的编译期分支。

---

## if constexpr

`if constexpr` 是 C++17 引入的编译期条件分支。它和普通 `if` 的区别在于：条件必须是编译期常量表达式，**未选中的分支不会被编译**——甚至连语法检查都不会做。这个特性在处理 void 返回类型时特别有用。

```cpp
template<typename R>
R do_something() {
    if constexpr (std::is_void_v<R>) {
        // void 返回：执行操作，不 return
        perform_action();
        return;  // void return
    } else {
        // 非 void 返回：执行操作，return 结果
        return perform_action();
    }
}
```

如果没有 `if constexpr` 而用普通的 `if`，两边的分支都会被编译。此时 void 分支里的 `return result` 会直接报错——void 不是一种可以赋值的类型。`if constexpr` 保证了 void 的情况只生成 `return;` 的代码，非 void 的情况只生成 `return result;` 的代码。

在 OnceCallback 中，`if constexpr (std::is_void_v<ReturnType>)` 出现在两个地方：`impl_run()` 的回调执行逻辑，和 `then()` 的链式组合逻辑。两处都是同一个问题——void 返回类型不能用常规方式赋值和返回。

---

## decltype(auto)

`decltype(auto)` 是 C++14 引入的返回类型推导方式。它和 `auto` 的区别在于对引用的处理：`auto` 会丢掉引用和顶层 const，`decltype(auto)` 会保留。

```cpp
int x = 10;
int& ref = x;

auto f1() { return ref; }           // 返回 int（丢掉了引用）
decltype(auto) f2() { return ref; } // 返回 int&（保留了引用）
```

在 OnceCallback 中，`bind_once` 和 `then()` 的 lambda 用 `-> decltype(auto)` 作为尾置返回类型。这样做的目的是完美转发可调用对象的返回值——如果被调用的函数返回 `int&&`，`decltype(auto)` 也会返回 `int&&`，不会丢失值类别信息。

---

## [[nodiscard]] 属性

`[[nodiscard]]` 是 C++17 标准化的属性，告诉编译器"这个函数的返回值不应该被忽略"。如果调用方写了 `cb.is_cancelled();` 但没有使用返回值，编译器会发出警告。

```cpp
[[nodiscard]] bool is_cancelled() const noexcept;
[[nodiscard]] bool maybe_valid() const noexcept;
[[nodiscard]] bool is_null() const noexcept;
```

OnceCallback 的三个查询方法都标注了 `[[nodiscard]]`。原因很简单——调用这些方法就是为了拿返回值做判断，忽略返回值的调用大概率是手滑写错了（比如把 `if (!cb.is_cancelled())` 写成了 `cb.is_cancelled();`）。`explicit operator bool()` 的 `explicit` 也起类似作用——防止隐式转换到 `bool` 引发的意外行为。

---

## Ref-qualified 成员函数

C++11 允许对非静态成员函数进行引用限定（ref-qualifier），用 `&` 或 `&&` 标注在函数参数列表后面。`&` 表示只能通过左值调用，`&&` 表示只能通过右值调用。

```cpp
class Widget {
public:
    void process() & {
        // 只能通过左值调用：Widget w; w.process();
    }
    void process() && {
        // 只能通过右值调用：Widget().process(); 或 std::move(w).process();
    }
};
```

在 OnceCallback 中，`then()` 方法声明为 `auto then(Next&& next) &&`——末尾的 `&&` 意味着 `then()` 只能通过右值调用（`std::move(cb).then(next)` 或临时对象上的 `.then(next)`）。这是表达消费语义的另一种方式——和 `run()` 用 deducing this 不同，`then()` 不需要区分左值和右值给出不同的错误信息，直接用 ref-qualifier 更简洁。

---

## 小结

这一篇我们把 OnceCallback 系列会用到的所有 C++ 基础特性快速过了一遍。每个特性我们都明确了三点：它是什么、怎么用、在 OnceCallback 的哪里会出现。如果你对某个特性感到陌生，建议回到对应卷的章节系统学习——后续文章不会再重复解释这些基础语法。

接下来我们要进入深度环节了。第一站是"函数类型与模板偏特化"——这是理解 `OnceCallback<R(Args...)>` 这个古怪写法的关键，也是我们搭建整个模板骨架的入口。

## 参考资源

- [cppreference: 移动语义与右值引用](https://en.cppreference.com/w/cpp/language/reference)
- [cppreference: std::forward](https://en.cppreference.com/w/cpp/utility/forward)
- [cppreference: 可变参数模板](https://en.cppreference.com/w/cpp/language/parameter_pack)
- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
- [cppreference: if constexpr](https://en.cppreference.com/w/cpp/language/if)
- [cppreference: Type traits](https://en.cppreference.com/w/cpp/header/type_traits)
