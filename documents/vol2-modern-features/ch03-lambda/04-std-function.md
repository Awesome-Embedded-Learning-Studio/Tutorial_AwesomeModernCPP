---
title: "std::function、std::invoke 与可调用对象"
description: "理解类型擦除、函数调用机制与零开销回调设计"
chapter: 3
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - lambda
  - std_function
  - std_invoke
  - 函数对象
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 20
prerequisites:
  - "Chapter 3: Lambda 基础"
related:
  - "函数式编程模式"
---

# std::function、std::invoke 与可调用对象

## 引言

笔者在写事件系统的时候遇到一个很实际的问题：需要存储各种类型的回调——普通函数、成员函数、lambda、仿函数，五花八门。函数指针只能指向静态函数和全局函数，没法携带上下文；直接用 `auto` 存 lambda 的话，每个 lambda 类型都不一样，没法放进同一个容器里。`std::function` 解决了这个问题——它用类型擦除把各种可调用对象统一成同一种类型。但类型擦除是有代价的，于是问题就变成了：这个代价到底有多大？有没有办法两全其美？

这一章我们从 `std::function` 的内部机制讲起，再到 `std::invoke` 这个"万能调用器"，最后聊聊零开销回调的设计模式——在类型安全和性能之间找到平衡点。

> **学习目标**
>
> - 理解 `std::function` 的类型擦除机制和 SBO
> - 掌握 `std::invoke` 统一调用可调用对象的方式
> - 学会用模板 + lambda 设计零开销的回调系统

---

## C++ 的可调用对象全家福

在讲具体机制之前，我们先梳理一下 C++ 里的"可调用对象"到底有哪些形态。所谓可调用对象，就是能用 `()` 语法（或者 `std::invoke`）来调用的东西。普通函数和函数指针是最基础的——直接调用或者通过指针间接调用。仿函数（functor）是重载了 `operator()` 的类对象。Lambda 是编译器生成的匿名仿函数。成员函数指针指向类的成员函数，调用时需要配合对象实例。此外还有 `std::function` 包装后的对象和 `std::bind` 的结果。

问题在于，这些可调用对象的调用语法各不相同——普通函数直接调，成员函数要用 `obj.*ptr` 或 `obj->*ptr`，仿函数和 lambda 像 `f(args)` 一样调。如果你要写一个泛型函数来"统一调用"这些东西，在 C++17 之前你得写一堆模板特化；有了 `std::invoke`，一个函数搞定。

---

## std::function——类型擦除的函数容器

`std::function` 是 C++11 引入的通用函数包装器，定义在 `<functional>` 头文件中。它可以存储、复制和调用任何匹配签名要求的可调用对象。核心能力就一个：**把不同类型的可调用对象统一成同一种类型**。

```cpp
#include <functional>
#include <iostream>

int add(int a, int b) { return a + b; }

struct Multiplier {
    int factor;
    int operator()(int x) const { return x * factor; }
};

void demo_std_function() {
    std::function<int(int, int)> func;

    // 存储普通函数
    func = add;
    std::cout << func(3, 4) << "\n";   // 7

    // 存储 lambda
    func = [](int a, int b) { return a * b; };
    std::cout << func(3, 4) << "\n";   // 12

    // 存储仿函数
    func = Multiplier{5};
    std::cout << func(10) << "\n";     // 编译错误：签名不匹配
    // Multiplier 的 operator() 只接受一个参数，但 func 签名是 int(int,int)
}
```

### 类型擦除机制

`std::function` 是怎么做到把不同类型的东西放进同一个壳子里的？答案是类型擦除（type erasure）。简化的原理是这样的：`std::function` 内部定义了一个抽象基类（Concept），持有纯虚函数 `invoke`；然后为每种具体的可调用类型生成一个派生类（Model），在派生类中实现 `invoke`。`std::function` 持有一个指向 Concept 的指针，调用时通过虚函数分派到具体实现。

我们可以用代码来模拟这个过程：

```cpp
#include <memory>
#include <utility>

// 简化版 std::function 原理示意
template<typename Signature>
class SimpleFunction;

template<typename R, typename... Args>
class SimpleFunction<R(Args...)> {
    // 抽象接口
    struct ICallable {
        virtual ~ICallable() = default;
        virtual R invoke(Args... args) = 0;
        virtual ICallable* clone() const = 0;
    };

    // 具体实现：模板化的派生类存储真正的可调用对象
    template<typename T>
    struct CallableImpl : ICallable {
        T callable;
        explicit CallableImpl(T c) : callable(std::move(c)) {}

        R invoke(Args... args) override {
            return callable(std::forward<Args>(args)...);
        }

        ICallable* clone() const override {
            return new CallableImpl(callable);
        }
    };

    ICallable* ptr_ = nullptr;

public:
    SimpleFunction() = default;

    template<typename T>
    SimpleFunction(T callable)
        : ptr_(new CallableImpl<std::decay_t<T>>(std::move(callable))) {}

    SimpleFunction(const SimpleFunction& other)
        : ptr_(other.ptr_ ? other.ptr_->clone() : nullptr) {}

    ~SimpleFunction() { delete ptr_; }

    R operator()(Args... args) {
        return ptr_->invoke(std::forward<Args>(args)...);
    }
};
```

从这段伪代码可以看到类型擦除的三个要素：一个统一的抽象接口（`ICallable`）、一个模板化的具体实现（`CallableImpl<T>`）、一个指向接口的指针（`ptr_`）。存储的时候类型信息被"擦除"了——外部看到的只是 `ICallable*`；调用的时候通过虚函数表恢复回来。

### 小对象优化（SBO）

上面的简化版有个明显的问题：每次构造都用 `new` 在堆上分配。对于捕获一两个 `int` 的小 lambda 来说，这个堆分配的代价可能比 lambda 本身还高。所以实际的 `std::function` 实现都使用了小对象优化（Small Buffer Optimization，也叫 SBO 或 SOO）——在 `std::function` 对象内部预留一块固定大小的缓冲区（通常是 16-32 字节），如果被包装的可调用对象足够小，就直接存放在这个缓冲区里，不需要堆分配。

```cpp
#include <functional>
#include <iostream>
#include <array>

void demo_sbo_size() {
    // 小 lambda：通常能放进 SBO 缓冲区
    auto small = [x = 42]() { return x * 2; };
    std::function<int()> f1 = small;
    std::cout << "sizeof(std::function<int()>): "
              << sizeof(f1) << " bytes\n";
    // 通常 32-64 字节（取决于实现）

    // 大 lambda：超出 SBO 缓冲区，触发堆分配
    auto large = [data = std::array<int, 100>{}]() {
        return data.size();
    };
    std::function<std::size_t()> f2 = large;
    std::cout << "sizeof(std::function<size_t()>): "
              << sizeof(f2) << " bytes\n";
    // 同样大小，但内部有堆分配

    // 对比：函数指针的大小
    std::cout << "sizeof(void(*)()): "
              << sizeof(void(*)()) << " bytes\n";
    // 通常 8 字节（64 位系统）
}
```

libstdc++（GCC）的 `std::function` 通常预留 16 字节的 SBO 缓冲区，libc++（Clang）的缓冲区大小也类似。只要你的 lambda 捕获的数据不超过这个大小（一两个 `int`、一个指针之类的），就不会触发堆分配。但 `std::function` 本身的大小仍然是 32-64 字节——它要容纳 SBO 缓冲区、虚函数表指针和管理元数据。相比之下，函数指针只需要 8 字节。

---

## 函数指针——零开销但功能受限

在讲零开销替代方案之前，我们先回顾一下函数指针。函数指针是 C 时代传承下来的机制，直接指向代码地址，简单而高效。它的大小就是一个指针（64 位系统上 8 字节），调用就是一条 `call` 指令，没有任何间接层。

```cpp
// 函数指针的声明和赋值
int (*func_ptr)(int, int) = [](int a, int b) { return a + b; };

// 用 using 简化类型名
using BinaryOp = int(*)(int, int);
BinaryOp op = [](int a, int b) { return a + b; };
int result = op(3, 4);   // 7
```

函数指针最大的局限是无法携带上下文——它只能指向无捕获的 lambda（或者普通函数、静态成员函数）。任何有捕获的 lambda 都不能转换为函数指针。当你需要把 `this` 指针或者某些状态传给回调的时候，函数指针就无能为力了。

| 特性 | 函数指针 | std::function |
|------|----------|---------------|
| 大小 | 8 字节（64 位） | 32-64 字节 |
| 堆分配 | 无 | 可能 |
| 间接调用层数 | 1 层 | 1-2 层 |
| 携带上下文 | 否 | 是 |
| 内联友好 | 是 | 较差 |

---

## std::invoke——统一调用接口

C++17 引入的 `std::invoke`（定义在 `<functional>` 中）是一个"万能调用器"。不管你的可调用对象是什么类型——普通函数、成员函数指针、lambda、仿函数——`std::invoke` 都能用同一个语法调用。它实现了标准中定义的 INVOKE 表达式的语义：

```cpp
#include <functional>
#include <iostream>

struct Widget {
    void greet(const std::string& msg) {
        std::cout << "Widget says: " << msg << "\n";
    }
    int data = 42;
};

void free_func(int x) {
    std::cout << "free_func: " << x << "\n";
}

void demo_invoke() {
    Widget w;

    // 普通函数
    std::invoke(free_func, 42);

    // 仿函数 / lambda
    std::invoke([](int x) { std::cout << "lambda: " << x << "\n"; }, 99);

    // 成员函数指针 + 对象
    std::invoke(&Widget::greet, w, "hello");

    // 成员变量指针 + 对象（可以读取和修改）
    int val = std::invoke(&Widget::data, w);
    std::invoke(&Widget::data, w) = 100;
}
```

看那个成员函数调用——传统写法是 `(w.*(&Widget::greet))("hello")` 或者 `(wg.*mem_func)("hello")`，这个语法笔者每次写都得查一下。用 `std::invoke` 只需要 `std::invoke(mem_func, obj, args...)`，简洁得多。

### invoke 的底层原理

`std::invoke` 的实现原理不复杂，核心就是编译期的类型判断和分派。对于普通可调用对象（函数指针、lambda、仿函数），它直接用 `f(args...)` 调用；对于成员函数指针，它根据对象的类别（指针、引用、`reference_wrapper`）选择合适的调用语法；对于成员变量指针，它返回对应的成员引用。所有这些判断都在编译期完成，运行时零开销。

### std::invoke_result_t

C++17 还配套提供了 `std::invoke_result_t`，可以在编译期获取 `std::invoke` 调用的返回类型。在写泛型代码的时候，这个工具非常实用：

```cpp
#include <type_traits>
#include <functional>

template<typename Func, typename... Args>
auto safe_call(Func&& func, Args&&... args)
    -> std::invoke_result_t<Func, Args...>
{
    using Ret = std::invoke_result_t<Func, Args...>;

    if constexpr (std::is_void_v<Ret>) {
        std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
        std::cout << "(void return)\n";
    } else {
        Ret result = std::invoke(std::forward<Func>(func),
                                 std::forward<Args>(args)...);
        std::cout << "result: " << result << "\n";
        return result;
    }
}
```

### invoke 的性能

在模板代码中使用 `std::invoke`，编译器能看到完整的调用链，会把它内联到和直接调用一样的程度。`std::invoke` 本身只是一层薄薄的编译期分派包装，不引入任何运行时开销。当然，如果你通过 `std::function` 存储的可调用对象来调用，间接开销来自 `std::function` 的类型擦除，不是 `std::invoke` 造成的。

---

## 零开销回调设计——模板 + lambda

理解了 `std::function` 的开销来源（类型擦除、可能的堆分配、间接调用）之后，问题就变成了：在很多场景下，回调的类型在注册时就已经确定了，我们是不是可以不用类型擦除？

答案是肯定的。最简单的零开销方案就是直接用模板参数传递 lambda——编译器知道完整的闭包类型，调用完全内联：

```cpp
#include <algorithm>
#include <vector>
#include <iostream>

// 模板参数接收任意可调用对象，零开销
template<typename Callback>
void for_each_if(std::vector<int>& data, Callback pred, Callback action) {
    for (auto& elem : data) {
        if (pred(elem)) {
            action(elem);
        }
    }
}

void demo_template_callback() {
    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8};

    int threshold = 5;
    int sum = 0;

    // lambda 直接传给模板参数，完全内联
    for_each_if(data,
        [threshold](int x) { return x > threshold; },   // 谓词
        [&sum](int& x) { sum += x; }                     // 操作
    );

    std::cout << "Sum of elements > " << threshold << ": " << sum << "\n";
    // 输出: Sum of elements > 5: 21 (6+7+8)
}
```

这个方案的问题是：每个不同的 lambda 类型会实例化不同的模板函数，你不能把不同类型的回调放进同一个容器。如果你的设计确实需要运行时多态（比如事件队列里存各种类型的回调），那就必须引入某种形式的类型擦除。

### 手动类型擦除：函数指针表替代虚函数

如果你需要类型擦除但又想避免 `std::function` 的全部开销，可以手写一个轻量级的类型擦除容器。核心思路是用函数指针表替代虚函数表，用固定大小的栈上缓冲区替代堆分配：

```cpp
#include <cstddef>
#include <utility>
#include <iostream>
#include <new>

template<typename Signature, std::size_t BufSize = 32>
class LightCallback;

template<typename R, typename... Args, std::size_t BufSize>
class LightCallback<R(Args...), BufSize> {
    // 操作表：用函数指针代替虚函数
    struct VTable {
        void (*move)(void* dst, void* src);
        void (*destroy)(void* obj);
        R (*invoke)(void* obj, Args... args);
    };

    // 为每种可调用类型生成专属的 VTable
    template<typename T>
    struct VTableFor {
        static void do_move(void* dst, void* src) {
            new(dst) T(std::move(*static_cast<T*>(src)));
        }
        static void do_destroy(void* obj) {
            static_cast<T*>(obj)->~T();
        }
        static R do_invoke(void* obj, Args... args) {
            return (*static_cast<T*>(obj))(std::forward<Args>(args)...);
        }
        static constexpr VTable value{do_move, do_destroy, do_invoke};
    };

    alignas(std::max_align_t) unsigned char storage_[BufSize];
    const VTable* vtable_ = nullptr;

public:
    LightCallback() = default;

    template<typename T>
    LightCallback(T&& callable) {
        using Decay = std::decay_t<T>;
        static_assert(sizeof(Decay) <= BufSize, "Callable too large for buffer");
        static_assert(alignof(Decay) <= alignof(std::max_align_t),
                     "Callable alignment too high");
        new(storage_) Decay(std::forward<T>(callable));
        vtable_ = &VTableFor<Decay>::value;
    }

    LightCallback(LightCallback&& other) noexcept : vtable_(other.vtable_) {
        if (vtable_) {
            vtable_->move(storage_, other.storage_);
            other.vtable_ = nullptr;
        }
    }

    ~LightCallback() {
        if (vtable_) vtable_->destroy(storage_);
    }

    LightCallback(const LightCallback&) = delete;
    LightCallback& operator=(const LightCallback&) = delete;

    R operator()(Args... args) {
        return vtable_->invoke(storage_, std::forward<Args>(args)...);
    }

    explicit operator bool() const { return vtable_ != nullptr; }
};

void demo_light_callback() {
    int multiplier = 3;
    LightCallback<int(int), 32> cb = [multiplier](int x) {
        return x * multiplier;
    };

    std::cout << cb(14) << "\n";  // 42
}
```

这个 `LightCallback` 没有 `std::function` 那么通用（不支持拷贝、不支持分配器），但它满足了最常见的使用场景：存储带捕获的 lambda，无堆分配，单层间接调用。在嵌入式或高性能场景中，这种"够用就好"的设计通常是最务实的选择。

### 选择指南

总结一下回调存储方案的权衡。函数指针适合不需要上下文的场景，零开销但只能指向无捕获的 lambda 或普通函数。`std::function` 适合需要运行时多态的场景，通用但有 SBO 范围外的堆分配风险和间接调用开销。模板参数适合编译期类型已知的场景，完全零开销但不能存入容器。手写类型擦除适合需要运行时多态且对性能有要求的场景，代码量稍多但可控。

```cpp
// 1. 无上下文、热路径：函数指针
void fast_path(int (*cb)(int)) { cb(42); }

// 2. 有上下文、通用场景：std::function
void generic_path(std::function<void(int)> cb) { cb(42); }

// 3. 编译期类型已知：模板参数
template<typename CB>
void zero_cost_path(CB&& cb) { cb(42); }

// 4. 有上下文、高性能：手动类型擦除
void optimized_path(LightCallback<void(int), 24> cb) { cb(42); }
```

---

## 小结

这一章我们把 C++ 中可调用对象的存储和调用机制串了起来：

- `std::function` 用类型擦除统一了各种可调用对象的类型，SBO 避免了小对象的堆分配
- 函数指针零开销但无法携带上下文，适合无状态回调
- `std::invoke` 是可调用对象的统一调用接口，在模板代码中零开销
- 零开销回调的核心思路是"能用模板就不用类型擦除，必须类型擦除时用函数指针表替代虚函数"
- 根据场景在通用性和性能之间选择合适的方案

## 参考资源

- [std::function - cppreference](https://en.cppreference.com/w/cpp/utility/functional/function)
- [std::invoke - cppreference](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [std::invoke_result - cppreference](https://en.cppreference.com/w/cpp/types/result_of)
- [Type Erasure implementation details - Arthur O'Dwyer](https://quuxplusone.github.io/blog/2019/03/27/design-space-for-std-function/)
