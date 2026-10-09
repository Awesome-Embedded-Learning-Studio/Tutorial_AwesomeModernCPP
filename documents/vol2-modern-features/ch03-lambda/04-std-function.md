---
chapter: 3
cpp_standard:
- 11
- 14
- 17
description: 理解类型擦除、函数调用机制与零开销回调设计
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Chapter 3: Lambda 基础'
- 'Chapter 3: Lambda 捕获机制深入'
reading_time_minutes: 15
related:
- 函数式编程模式
tags:
- host
- cpp-modern
- intermediate
- lambda
- std_function
- std_invoke
- 函数对象
title: std::function、std::invoke 与可调用对象
---
# std::function、std::invoke 与可调用对象

笔者在写事件系统的时候，撞上过一个很实际的问题：要存的回调类型太杂了，普通函数、成员函数、lambda、仿函数全都来了。直接拿 `auto` 存 lambda？每个 lambda 的类型都不一样，咱们没法把它们装进同一个容器。函数指针那边能指向的范围又有限。`std::function` 就是来干这件事的：它用类型擦除，把各种可调用对象都统一成了同一种类型。不过类型擦除不是白来的，那咱们就得问一句了：代价到底有多大？有没有办法两全其美？

第一篇咱们把几个细节甩给了这一篇：类型擦除怎么把各式闭包类型藏到统一接口后面，那笔固定开销花在了哪里。咱们顺带把 `std::invoke` 统一的是什么也讲清楚。这篇咱们就一样一样拆开看，等您看到最后，自己就能掂量出什么场景该用哪种回调了。

---

## C++ 的可调用对象有哪些形态

咱们花一分钟，把“哪些东西算 C++ 的可调用对象”数一遍。可调用对象的定义，您按字面理解就行：能用 `()` 语法调用的东西（用 `std::invoke` 调也算）。普通函数和函数指针是最基础的形态，您可以直接调用，也可以经由函数指针间接地调用。仿函数（functor）是重载了 `operator()` 的类对象。Lambda 的本质是编译器生成的匿名仿函数，前几篇咱们已经拆过。成员函数指针指向的是类的成员函数，调用的时候还得带上对象实例。剩下的还有 `std::function` 包装出来的对象，以及 `std::bind` 的结果。

麻烦出在哪？这些形态的调用语法不统一。普通函数咱们直接调，成员函数得写成 `obj.*ptr` 或 `obj->*ptr` 这样的形式，仿函数和 lambda 倒是省事，直接 `f(args)` 就调走了。您要是想写一个泛型函数，想统一地去调用它们，搁在 C++17 以前的年代，您得准备一堆模板特化。等 `std::invoke` 出来了，一个函数就搞定了。

---

## std::function——类型擦除的函数包装器

`std::function` 是 C++11 引入的通用函数包装器，它的定义在 `<functional>` 头文件里。咱们可以把任何匹配签名要求的可调用对象交给它去存储、复制和调用。它的本事一句话就说得清：**把不同类型的可调用对象统一成同一种类型**。

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

`std::function` 凭什么能把不同类型的东西装进同一个类型？靠的是类型擦除（type erasure）。名字听着挺唬人的，其实拆开就一件事：把各式闭包类型藏到统一接口的后面，具体的类型对外就看不见了。具体怎么藏？咱们往下看。它内部定义了一个抽象基类（Concept），里面声明了纯虚函数 `invoke`。再给每种具体的可调用类型生成一个派生类（Model），让真正的 `invoke` 实现落在派生类里。`std::function` 自己只持有一个指向 Concept 的指针，调用的时候再靠虚函数分派回具体的实现。

咱们可以用代码把这个过程模拟一遍：

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

您对照代码看，类型擦除需要的零件就三样：统一的抽象接口 `ICallable`，模板化的具体实现 `CallableImpl<T>`，再加一个指向接口的指针 `ptr_`。存进去的时候，类型信息就被“擦除”了，外部能看到的只剩一个 `ICallable*`。等到调用的时候，再靠虚函数表找回具体的实现。

存入与调用的整个过程，咱们做成了动画。您可以播放、暂停，也可以按步进键单步看：三种可调用对象怎么统一成一个类型，调用的时候又怎么分派回具体的实现：

<Anim id="type-erasure" />

### 小对象优化（SBO）

上面的简化版有个明摆着的毛病，您应该也一眼看到了：每次构造都要 `new` 一回，内存也就落到了堆上。对只捕获一两个 `int` 的小 lambda 来说，堆分配的代价可能比 lambda 本身还高。所以实际的 `std::function` 实现都带小对象优化，它的英文全名是 Small Buffer Optimization，直译过来其实是“小缓冲区优化”，咱们中文圈两种叫法都在用，平时用的简称就是 SBO（偶尔也写作 SOO）：在 `std::function` 对象内部预留一块固定大小的缓冲区（通常是 16-32 字节），被包装的可调用对象只要足够小，就直接放进这块缓冲区里了，堆分配省了。

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

咱们在 GCC 15.2.1 上实际测了一把 libstdc++ 的 SBO 行为，`std::function<int()>` 的大小是 32 字节。欸，结果有点意思：闭包才 4 字节的 lambda（捕获单个 `int`）没有触发堆分配，可捕获 5 个 `int`、或者一个指针的 lambda，就落到堆上了。看来 GCC 15.2 的 SBO 实现比较保守，大概是虚函数表指针和管理元数据也要占地方的。libc++（Clang）那边的实现可能不一样，具体行为因版本而异，您要较真，在自己的编译器上跑一遍最稳。

> **验证代码**：咱们跑的测试在 `code/volumn_codes/vol2/ch03-lambda/test_sbo_size.cpp`，用的环境是 GCC 15.2.1 加 `-O2`。
>
> 多提醒一句：不同编译器和版本的 SBO 行为差异很大。您要是写性能敏感的代码，建议您改用模板参数或者手写类型擦除，换一个行为可预测的方案。

咱们把两种闭包的去向也做成了动画，您可以按步进键单步看：4 字节的小闭包怎么塞进内部缓冲区，400 字节的大闭包又为什么落到堆上：

<Anim id="function-sbo" />

---

## 函数指针——零开销但功能受限

咱们把 `std::function` 放一边，回头看看从 C 时代传下来的函数指针。它指向的就是代码地址本身，简单而高效。它的大小就是一个指针（64 位系统上是 8 字节），调用走的就是一条 `call` 指令（`call *%rax`），没有额外多出来的间接层。

> **性能实测**：笔者在 GCC 15.2.1 上跑了测试，开了 `-O2`，函数指针的调用比直接调用慢了约 30%（1.29x）。差距的来源也清楚：直接调用可以被完全内联成计算指令，函数指针那边则仍然要走间接的 `call`。不过在无优化的代码中，两边走的都是 `call` 指令，差异也就更小了。
>
> **验证代码**：咱们跑的脚本在 `code/volumn_codes/vol2/ch03-lambda/test_function_performance.cpp`。

```cpp
// 函数指针的声明和赋值
int (*func_ptr)(int, int) = [](int a, int b) { return a + b; };

// 用 using 简化类型名
using BinaryOp = int(*)(int, int);
BinaryOp op = [](int a, int b) { return a + b; };
int result = op(3, 4);   // 7
```

函数指针最大的局限是带不了上下文：它指向的目标，除了无捕获的 lambda 就是普通函数和静态成员函数了。任何有捕获的 lambda，是没办法转换成函数指针的。您哪天要把 `this` 指针或者某些状态传给回调，函数指针就帮不上忙了。

```cpp
// 无捕获 lambda 可以转换为函数指针
int (*fp1)(int, int) = [](int a, int b) { return a + b; };  // OK

// 有捕获 lambda 不能转换
int x = 42;
int (*fp2)(int, int) = [x](int a, int b) { return a + b + x; };  // 编译错误
```

| 特性 | 函数指针 | std::function |
|------|----------|---------------|
| 大小 | 8 字节（64 位） | 32-64 字节 |
| 堆分配 | 无 | SBO 范围外触发 |
| 间接调用层数 | 1 层（直接 call） | 1 层（虚函数表间接） |
| 携带上下文 | 否 | 是 |
| 内联友好 | 相对较好 | 较差（类型擦除阻碍） |
| 性能（相对直接调用） | ~1.3x | ~7-9x |

> 表中 ~7-9x 的出处，是咱们跑过的 `code/volumn_codes/vol2/ch03-lambda/test_function_performance.cpp`，用的编译器是 GCC 15.2.1，开了 `-O2`，跑了 1 亿次调用，咱们在选择指南一节还会再用到这组数字。

---

## std::invoke——统一调用接口

C++17 引入的 `std::invoke`（定义在 `<functional>` 中）管的是另一件事：统一调用语法。不管您拿来的是普通函数、成员函数指针、lambda 还是仿函数，咱们用同一个写法就能调。它实现的是 C++ 标准里 INVOKE 表达式的语义。INVOKE 您可以当成标准给“统一调用”起的名字，专门负责把成员函数指针和成员变量指针这样的特殊形态也覆盖进来。

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

请您看代码里成员函数那一行，`std::invoke(&Widget::greet, w, "hello")`。传统的写法是 `(w.*(&Widget::greet))("hello")`，或者换成 `(w.*mem_func)("hello")` 的形式，这个语法笔者每次写都得查一遍。咱们换 `std::invoke`，您只要写 `std::invoke(mem_func, obj, args...)`，省事多了。

### invoke 的底层原理

`std::invoke` 的实现原理不复杂，干的是编译期的类型判断和分派，咱们挨个看。对象要是普通的可调用（函数指针、lambda、仿函数），直接 `f(args...)` 就调过去了。轮到成员函数指针的时候，它就按传入对象的类别挑对应的调用语法，能传的类别就是指针、引用和 `std::reference_wrapper` 几种。`std::reference_wrapper` 是 `<functional>` 里把引用包成可拷贝对象的标准库类型，为的就是让引用能按值传递。轮到成员变量指针的时候，它返回的就是对应的成员引用。所有判断都在编译期就完成了，运行时是零开销的。

### std::invoke_result_t

C++17 还配套给了咱们一个 `std::invoke_result_t`，能在编译期拿到 `std::invoke` 调用的返回类型。写泛型代码的时候，它帮了咱们大忙：

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

咱们在模板代码里用 `std::invoke`，编译器能看到完整的调用链，会把它内联到和直接调用一样的程度。笔者实测了一下：`-O2` 优化下，`std::invoke` 调用和直接调用的性能完全相同，误差范围内打成了平手（偶尔显得稍快也只是测试的误差）。原因咱们也看得出来：`std::invoke` 本身只是一层编译期的分派包装，优化后就被完全内联掉了。

> **验证代码**：咱们跑性能的脚本在 `code/volumn_codes/vol2/ch03-lambda/test_invoke_performance.cpp`。
>
> **汇编验证**：咱们生成汇编看了一眼（`g++ -O2 -S`），直接调用、`std::invoke`、函数指针、lambda 被编译成了完全相同的代码，直接算出结果就返回了，`call` 指令一条都没有了。测试里调用目标在编译期就知道了，函数指针才跟着一起被折叠掉了。调用目标是等到运行时才确定的话，它走的还是间接 `call`。

当然，您要是经由 `std::function` 存储的对象去调用，间接开销是 `std::function` 的类型擦除带来的，跟 `std::invoke` 是没有关系的。

---

## 零开销回调设计——模板 + lambda

`std::function` 的开销来源，咱们心里已经有数了：类型擦除、可能的堆分配、间接调用。那咱们再往前想一步：很多场景下，回调的类型在注册那一刻就定死了，咱们是不是根本用不着类型擦除？

当然可以。最简单的零开销方案，就是咱们直接拿模板参数传 lambda：编译器知道完整的闭包类型，连调用都能完全内联了。

```cpp
#include <algorithm>
#include <vector>
#include <iostream>

// 两个模板参数各自接收一个可调用对象，零开销
template<typename Pred, typename Action>
void for_each_if(std::vector<int>& data, Pred pred, Action action) {
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
    // 输出: Sum of elements > 5: 21
}
```

不过它也有短板：每个不同的 lambda 类型，都会实例化出不同的模板函数，您没法把不同类型的回调放进同一个容器。设计上确实需要运行时多态的话（比如事件队列里存各种类型的回调），某种形式的类型擦除就躲不开了。

### 手动类型擦除：函数指针表替代虚函数

您要是需要类型擦除，又想躲开 `std::function` 的全套开销，还有一条路：手写一个轻量级的类型擦除容器。思路其实就两条，函数指针表替掉了虚函数表，栈上一块固定大小的缓冲区替掉了堆分配：

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

您拿这个 `LightCallback` 跟 `std::function` 一比，通用性上确实差了一截，拷贝和分配器的支持它确实没做。但最常见的使用场景它接得住：存储带捕获的 lambda，堆分配也省了，间接调用也只剩一层了。

### 选择指南

回到开场的那个事件系统，咱们就能把几个选项摆开来看。您的回调不需要上下文、又跑在热路径上，函数指针就是最省心的，它能指的目标，也就是无捕获的 lambda、普通函数这些。事件队列要的是运行时多态，那 `std::function` 就顶上了，代价也是明摆着的：哪怕对象落在 SBO 范围内，虚函数表带的那一层间接调用也会阻碍内联，实测下来比直接调用慢了 7-9 倍。回调的类型在编译期就确定了，咱们直接上模板参数，拿到的是零开销，只是它们进不了容器。运行时多态和性能都想要的，就得手写类型擦除了，代码量是要多写一点的，行为也变得完全可控了。

> **性能数据来源**：还是笔者那套 `code/volumn_codes/vol2/ch03-lambda/test_function_performance.cpp`，在 GCC 15.2.1 上跑的，开了 `-O2`，一共跑了 1 亿次调用。

---

## 参考资源

- [std::function - cppreference](https://en.cppreference.com/w/cpp/utility/functional/function)
- [std::invoke - cppreference](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [std::invoke_result - cppreference](https://en.cppreference.com/w/cpp/types/result_of)
- [Type Erasure implementation details - Arthur O'Dwyer](https://quuxplusone.github.io/blog/2019/03/27/design-space-for-std-function/)
