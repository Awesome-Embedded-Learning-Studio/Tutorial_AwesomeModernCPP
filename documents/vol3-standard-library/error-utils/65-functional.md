---
title: 'functional：std::function 的代价与 C++23 的 move_only_function'
description: 讲透 <functional> 里三个东西的真面目——std::function 的类型擦除为什么必然带来虚调用和可能堆分配（实测代价）、reference_wrapper 如何让容器存下引用、move_only_function 在 C++23 解决了 std::function 存不下 move-only 可调用的硬伤
chapter: 7
order: 65
cpp_standard:
- 11
- 17
- 20
- 23
difficulty: intermediate
platform: host
reading_time_minutes: 14
prerequisites:
- 'optional：把「可能没有」做成类型'
- 'variant：类型安全的联合体与 visit'
- 'ranges 算法与 C++23 新件：fold、contains 与新适配器'
related:
- 'expected：值或错误，C++23 的错误处理新范式'
tags:
- host
- cpp-modern
- intermediate
- 函数对象
- std_function
- std_invoke
- lambda
---

# functional：std::function 的代价与 C++23 的 move_only_function

写 C++ 一段时间，你几乎一定会撞到这么个需求：手里有一堆「能被调用的东西」，想统一存起来。可能是一个普通函数，可能是一个捕获了若干状态的 lambda，可能是某个类的成员函数，还可能是 functor。它们签名相同（都是 `int(int)`），但**类型完全不同**——而容器和成员变量都得在编译期知道元素的类型。这下尴尬了：`std::vector</* 到底写啥？ */>` 写不出来。

`<functional>` 头文件就是来回答这个问题的。它的核心组件 `std::function` 把「能被调用、签名一致」的一类对象用**类型擦除**包成同一个类型，于是异质的可调用对象能塞进同一个容器、同一个成员。这能力很值钱，但它不是白来的——这一篇我们就把它拆开，看清楚类型擦除到底带来了什么代价，以及 C++23 用 `std::move_only_function` 补上了哪块缺口。

顺带我们把 `<functional>` 里另外两个高频工具也讲透：`reference_wrapper`（让容器能存引用）和 `std::hash`（无序容器的基石），最后给出「什么时候该用 `std::function`、什么时候能不用就不用」的判断准则。lambda 本身和闭包的深入机制不在这一篇的范围（那卷在 vol2），我们这里只把 lambda 当成「产生一个可调用对象」的工具来用。

## 三种可调用对象：它们到底是什么

在讲 `std::function` 之前，得先把「可调用对象」的几副面孔分清楚，否则后面讨论代价时会糊在一起。

第一类是**普通函数**和**函数指针**。函数本身是一个地址，函数指针就是存这个地址的变量，调用它是一次间接跳转——编译器一般无法通过指针做内联，这是后面性能对比的一个关键。

第二类是**函数对象（functor）**，就是重载了 `operator()` 的类实例：

```cpp
// Standard: C++11
struct Multiplier {
    int factor;
    explicit Multiplier(int f) : factor{f} {}
    int operator()(int x) const { return x * factor; }
};

Multiplier times3{3};
int r = times3(10);   // 30 —— 调用 operator()
```

functor 的特点是：它带状态（`factor` 是成员），且**类型是你自己定义的类**。

第三类是 **lambda**。lambda 看起来很轻量，写起来像匿名函数，但它在编译期其实被翻译成了「一个编译器生成的、独一无二的类」。具体说，每个 lambda 都对应一个**闭包类型（closure type）**，每个 lambda 表达式都是一个独立类型——哪怕两段 lambda 代码一模一样：

```cpp
// Standard: C++11
auto f1 = []() { return 1; };
auto f2 = []() { return 1; };   // 看起来和 f1 完全相同
// 但 f1、f2 类型不同
static_assert(not std::is_same_v<decltype(f1), decltype(f2)>);
```

跑出来印证：

```text
f1 和 f2 类型是否相同: no
```

捕获列表变成闭包类的成员，lambda 体变成 `operator()`。所以本质上，**lambda 就是让你省去手写 functor 类的语法糖**——它和 functor 是同一种东西，只是编译器帮你生成了类。这点一定要记住，它直接决定了 `std::function` 为什么需要类型擦除：这三类可调用对象类型各异，但调用签名一致，你没法用一个具体的 C++ 类型把它们全装下。

顺带一个常用结论：**零捕获 lambda 可以隐式转成函数指针**（因为没有状态成员），捕获了东西的就不行：

```text
零捕获 lambda -> 函数指针: 1
```

## std::function：类型擦除的可调用包装

回到开头的痛点。三种类不同的可调用对象，怎么用同一个类型装起来？`std::function` 的答案就是**类型擦除（type erasure）**——把「具体是什么可调用对象」这个信息藏到运行时，对外只暴露「签名是什么」。

```cpp
// Standard: C++11
#include <functional>

int free_fn(int x) { return x + 1; }

struct Doubler { int operator()(int x) const { return x * 2; } };

int main() {
    std::function<int(int)> f;   // 一个能装任何 int(int) 的槽

    f = free_fn;                 // 装函数指针
    f = Doubler{};               // 装 functor
    f = [](int x){ return x * 3; };   // 装 lambda
    int cap = 10;
    f = [cap](int x){ return x + cap; };  // 装带捕获的 lambda

    return f(5);   // 调用 —— 无关它现在装的是什么
}
```

`std::function<int(int)>` 的 `<int(int)>` 是被擦除后对外保留的调用签名，至于内部存的是函数指针、functor 还是闭包，对外不可见。这正是它能进容器、能当成员变量的原因——容器只需要一个固定的元素类型。

那「藏到运行时」具体怎么实现？剥一层看，`std::function` 内部大致是这样：它持有一个**间接调用器（invoker）函数指针**和一个**管理器（manager）**，真正存的可调用对象放在一个固定大小的内联小缓冲里（libstdc++ 的 `std::function<int(int)>` sizeof 是 32 字节）；当目标太大塞不进小缓冲时，再在堆上分配一块存它，内部只留一个指针。每次你调 `f(5)`，它实际上是通过那个函数指针**间接跳转**到真正的调用代码。

这套机制下，「能装任何类型」和「能逐个调用」都实现了，但代价我们也摸到了边——一次间接调用，外加可能的一次堆分配。下面我们逐一实测。

## 实测：std::function 的代价到底有多大

先把话说清楚：`std::function` 的代价不是「一个固定数字」，它由两部分组成，且在不同使用模式下权重不同。我们一项一项测，避免笼统下结论。

### 代价一：可能的堆分配

`std::function` 内部有个固定大小的 SBO（Small Buffer Optimization，小缓冲优化）缓冲区。捕获体小（塞得进缓冲）就内联存，零分配；捕获体大（塞不下）就堆分配一块。我们拦截全局 `operator new` 直接计数：

```cpp
// Standard: C++23
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <new>

static std::size_t g_alloc_count = 0;
static std::size_t g_alloc_bytes = 0;

void* operator new(std::size_t n) {
    ++g_alloc_count;
    g_alloc_bytes += n;
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }

int main() {
    // 小捕获:1 个 int,塞得进 SBO
    {
        int x = 42;
        g_alloc_count = 0; g_alloc_bytes = 0;
        std::function<int(int)> f = [x](int a){ return a + x; };
        // (用一下 f,别让编译器优化掉)
    }
    // 大捕获:int[64] ≈ 256B,塞不进 SBO
    {
        int big[64]{};
        big[0] = 7;
        g_alloc_count = 0; g_alloc_bytes = 0;
        std::function<int(int)> f = [big](int a){ return a + big[0]; };
    }
    return 0;
}
```

跑出来：

```text
小捕获(1 个 int): std::function 构造时堆分配次数 = 0, 字节 = 0
大捕获(int[64]): std::function 构造时堆分配次数 = 1, 字节 = 256
sizeof(std::function<int(int)>) = 32
```

结论很直白：小捕获零分配，大捕获一次堆分配。这意味着 `std::function` 存大捕获 lambda 时，构造和销毁各有一次堆操作——在热路径上、在容器里大量构造时，这是个真实的成本。同时 `sizeof` 是 32 字节，意味着即使你装一个 1 字节的可调用对象，`std::function` 本身也占 32 字节——容器里存一大堆它，内存占用不容忽视。

### 代价二：间接调用（无法内联）

这才是性能上更要命的一项。`std::function` 的调用是通过内部函数指针间接跳转的，编译器**无法跨这个间接调用做内联**。我们用汇编先确认「确实是间接 call」，再用微基准测时间。

```cpp
// 编译:g++ -std=c++23 -O2 -S
int main() {
    std::function<int(int)> f = target;   // target 是个 noinline 外部函数
    // ...
    return f(3);
}
```

汇编里 `f(3)` 对应的就是：

```text
call *%rax       ; 间接调用,目标地址运行时才能定
```

`*%rax` 这个星号就是间接——调用的目标在运行时才从 `function` 内部取出来，编译期看不见，于是跨不了这层做内联。这和直接调一个普通函数（编译器看得见实现、能内联）是两回事。

那时间上差多少？我们跑两个场景，一个能内联、一个不能，把对比讲清楚。先看「计算体极轻、能被内联」的场景，这样间接调用的固定开销不会被计算淹没：

```cpp
// Standard: C++23
#include <chrono>
#include <functional>
#include <iostream>

static volatile int g_sink = 0;

int main() {
    const int N = 1'000'000'000;

    auto lambda = [](int x){ return x + 1; };          // 能被内联
    int (*fptr)(int) = +[](int x){ return x + 1; };    // 函数指针,不能内联
    std::function<int(int)> func = lambda;             // 类型擦除,不能内联

    auto bench = [&](auto& c){
        long long acc = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i) acc += c(i);
        auto t1 = std::chrono::steady_clock::now();
        g_sink = acc;
        return std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    };

    g_sink = lambda(0) + fptr(0) + func(0);   // warmup

    std::cout << "N = " << N << " 次极轻调用(x+1),总耗时(毫秒):\n";
    std::cout << "  直接 lambda        : " << bench(lambda) << " ms\n";
    std::cout << "  函数指针            : " << bench(fptr)   << " ms\n";
    std::cout << "  std::function      : " << bench(func)   << " ms\n";
    return 0;
}
```

10 亿次循环，本机 GCC 16.1.1、`-O2`：

```text
N = 1000000000 次极轻调用(x+1),总耗时(毫秒):
  直接 lambda        : 0 ms
  函数指针            : 1594 ms
  std::function      : 1886 ms
```

这里数字很说明问题。**直接 lambda 是 0ms** —— 不是它快得离谱，而是编译器发现整个循环可以被强度归约成常量（`x+1` 求和有公式），整段被优化掉了。函数指针和 `std::function` 因为有间接调用、跨不了那层做这个优化，所以老老实实跑了 10 亿次，分别在 1.6 秒和 1.9 秒量级。

这揭示的正是「类型擦除」的核心代价：**它把一个本来可以被编译器看穿、甚至整个消除的调用，强制变成了一次实打实的间接调用**。在调用体极轻、调用极频繁的热路径上（比如 per-pixel、per-element 的回调），这个差距可以从「免费」变成「吃掉一整个 CPU 核心」。

那「调用体本身就有一定工作量、本来就内联不掉」的场景呢？我们把目标换成一个外部 `noinline` 函数，让三个调用者都得真正去调它：

```text
N = 1000000000 次调用(调用 noinline 外部函数),总耗时(毫秒):
  lambda -> noinline fn  : 1794 ms
  函数指针                : 1993 ms
  std::function          : 2124 ms
```

当目标函数本身不能内联时，三者的差距就收窄了——函数指针和 lambda 差不多（都产生一次调用），`std::function` 略贵（多一层间接，大约贵 10%–30%，具体随机器和波动）。这告诉我们一个判断准则：**调用体越重、越本来就内联不掉，`std::function` 的相对代价就越小；调用体越轻、越靠内联省时间，`std::function` 的相对代价就越大**。绝对数字随机器和负载波动，但「直接调用能被优化掉、间接调用不能」这个性质是稳的。

### 代价三：体积

前面看到 `sizeof(std::function<int(int)>)` 是 32 字节。哪怕装一个 1 字节的可调用对象，`function` 也占 32 字节。在「容器里存成千上万个 `function`」的场景下（比如事件系统里每个事件槽一个回调），这个体积乘以数量要算进内存预算。

## std::bind：能别用就别用

`<functional>` 里还有个老组件 `std::bind`，干的事是「把一个多参函数的部分参数固定，做成一个少参的可调用对象」。比如有个 `power(base, exp)`，我想要一个「平方」函数，可以用 `bind` 把 `exp` 固定为 2：

```cpp
// Standard: C++11
#include <functional>
int power(int base, int exp);

auto square_bind = std::bind(power, std::placeholders::_1, 2);
// 调用: square_bind(5) == power(5, 2) == 25
```

`std::placeholders::_1` 是占位符，表示「这个位置等调用时再传」。这在 C++11 lambda 还没普及、functor 要手写一大堆的年代是有用的。但从 C++14 起，**lambda 几乎在所有方面都比 `bind` 好**：

```cpp
// Standard: C++14
auto square_lam = [](int base){ return power(base, 2); };
```

lambda 更直观（不用记占位符语法）、类型是局部的（更好内联）、调试时能看清源码、对 move-only 参数也友好。`bind` 的返回类型是标准库内部的某个未指定类型，塞进 `std::function` 时还容易踩「值传递 vs 引用传递」的坑（要传引用得套一层 `std::ref`）。所以现在 `bind` 在新代码里基本是「能用 lambda 替就别用 bind」的过时组件——知道有这么个东西能读懂老代码就够了，自己写新代码直接 lambda。

## reference_wrapper：让容器能存引用

下一个高频工具是 `std::reference_wrapper`，以及配套的 `std::ref` / `std::cref`。它解决的是「容器不能直接存引用」这个硬限制：

```cpp
// Standard: C++11
std::vector<int&> v;   // 编不过 —— 元素类型必须是 Erasable / 可对象化的
```

C++ 标准要求容器元素是真正的对象类型，引用不是对象（没有地址、不能赋值），所以 `vector<T&>` 直接被拒。但实战里你确实经常想「让一个容器引用外部的一组变量」。`reference_wrapper` 就是一个「行为像引用、本身是个对象」的薄包装——它持有一个指针，并支持隐式转换回 `T&`：

```cpp
// Standard: C++23
#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>

int main() {
    int a = 1, b = 2, c = 3;
    std::vector<std::reference_wrapper<int>> refs{std::ref(a), std::ref(b), std::ref(c)};

    for (int& x : refs) {   // reference_wrapper 隐式转 int&
        x *= 10;
    }
    std::cout << "通过 ref 修改后: a=" << a << " b=" << b << " c=" << c << "\n";
    std::cout << "显式 get(): " << refs[0].get() << "\n";
    return 0;
}
```

```text
通过 ref 修改后: a=10 b=20 c=30
  显式 get(): 10
```

遍历时 `reference_wrapper<int>` 隐式转回 `int&`，于是修改写回了原变量。`ref()` 就是 `reference_wrapper` 的工厂函数，`cref()` 是 const 版本。`reference_wrapper` 的另一个经典用途是给「按值捕获/传参」的场合塞引用——比如给 `std::bind` 传引用（否则 `bind` 会按值拷贝），或者在「不能改签名、又想传出参」的算法调用里。

## std::hash：无序容器的基石

`std::hash` 是 `unordered_map` / `unordered_set` 能工作的底层依赖——无序容器靠哈希值定位桶，而算哈希值靠的就是 `std::hash<T>`。标准库为基本类型（整数、浮点、指针）和 `std::string`、`std::string_view` 等常用类型预置了 `std::hash` 特化：

```cpp
// Standard: C++11
std::hash<int>{}(42);                 // 算 42 的哈希
std::hash<std::string>{}("hello");    // 算字符串的哈希
```

```text
hash<int>(42)        = 42
hash<std::string>("hi") = 11290347552884584064
```

注意 `hash<int>(42)` 在 libstdc++ 里结果是 `42` 本身——对整数类型，标准没规定哈希函数的具体实现，但 libstdc++ 的实现就是恒等映射（整数的「哈希」就是它自己，因为整数本身就是个分布均匀的定宽值）。这只是实现细节，你**不应该依赖哈希的具体数值**，要依赖的是「相同输入给相同输出、不同输入尽量分散」。

如果你把自己写的类型塞进 `unordered_map` 当 key，标准库可不知道怎么算它的哈希，你得自己写一个 `std::hash<YourType>` 特化（或者用 `boost::hash_combine` 之类的工具把各字段哈希拼起来）。这是 `std::hash` 真正常被忽略的一面：它是**可扩展的**，不是只能给内置类型用。

## std::invoke：统一调用语法

最后两个小但关键的组件。`std::invoke`（C++17）解决的是「怎么用统一语法调任何可调用对象」的问题。普通函数和 functor 直接 `f(args)` 就行，但成员函数和成员指针要写成 `(obj.*pmf)(args)` / `obj.*pmd`，语法别扭。`invoke` 把它们统一了：

```cpp
// Standard: C++23
#include <functional>
#include <iostream>

struct Adder {
    int base{10};
    int add(int x) const { return base + x; }
};

int main() {
    Adder ad{100};
    auto lam = [](int x){ return x * 2; };

    std::cout << "invoke(成员函数): " << std::invoke(&Adder::add, ad, 5) << "\n";
    std::cout << "invoke(成员指针): " << std::invoke(&Adder::base, ad) << "\n";
    std::cout << "invoke(普通):    " << std::invoke(lam, 5) << "\n";
    return 0;
}
```

```text
invoke(成员函数): 105
invoke(成员指针): 100
invoke(普通):    10
```

成员函数、成员指针、普通可调用对象，全部用 `invoke(可调用, 参数...)` 一种写法。这在泛型代码里特别值钱——你写一个模板，不知道传进来的是函数还是成员指针，`invoke` 都能调对。`std::invoke_r<R>`（C++23）是 `invoke` 的带返回类型版本，强制把结果转成 `R`，在写回调签名严格的地方有用。

## C++23 的 move_only_function：补上 move-only 的缺口

到这里是本篇真正的新东西。`std::function` 有一个 longstanding 的硬伤：**它要求目标可调用对象必须可拷贝**。可一旦你想存一个捕获了 `std::unique_ptr` 的 lambda，这个要求就崩了——闭包持有 `unique_ptr` 成员，整个闭包是 move-only 的，拷贝不了：

```cpp
// Standard: C++23
std::unique_ptr<int> up = std::make_unique<int>(100);
auto lam = [up = std::move(up)](int x){ return *up + x; };   // 闭包是 move-only
std::function<int(int)> f = std::move(lam);   // 编不过
```

编译器直白拒绝，静态断言失败：

```text
/usr/include/c++/16.1.1/bits/std_function.h:429:69:
  error: static assertion failed: std::function target must be copy-constructible
```

`std::function` 内部为了支持拷贝（你 `function` 拷贝时，它得拷贝里面的目标），硬性要求目标 `is_copy_constructible`。这个约束在「存回调到容器、回调持有独占资源」的实战场景里经常绊脚。

C++23 的 `std::move_only_function` 就是来补这个缺口的。它和 `std::function` 一样做类型擦除，**但只要求 move，不要求 copy**——于是能存 move-only 可调用对象：

```cpp
// Standard: C++23
#include <functional>
#include <iostream>
#include <memory>

int main() {
    // 一个工厂:返回捕获了 unique_ptr 的 move-only lambda
    auto make_processor = [](std::unique_ptr<int> owner){
        return [owner = std::move(owner)](int x){
            return *owner + x;
        };
    };

    std::unique_ptr<int> up = std::make_unique<int>(100);
    std::move_only_function<int(int)> mof = make_processor(std::move(up));
    std::cout << "move_only_function 调用: " << mof(5) << "\n";
    std::cout << "  仍持有: " << (mof ? "yes" : "no") << "\n";

    // move_only_function 本身也是 move-only(不能拷贝)
    auto mof2 = std::move(mof);
    std::cout << "move 后 mof2(5) = " << mof2(5) << "\n";
    std::cout << "move 后源 mof 是否空: " << (mof ? "no" : "yes(被掏空)") << "\n";
    return 0;
}
```

```text
move_only_function 调用: 105
  仍持有: yes
move 后 mof2(5) = 105
move 后源 mof: yes(被掏空)
```

跑通了。这就是它和 `std::function` 最本质的区别：**`std::function` 要求 Copyable，`move_only_function` 只要 Movable**。代价是 `move_only_function` 自己也不能拷贝（只能 move），这其实很合理——既然它内部可能存 move-only 的东西，整个包装自然也拷贝不了。

其它方面两者很像：`move_only_function` 同样做类型擦除、同样有 SBO、同样有间接调用开销。我们实测它的调用开销和 `std::function` 基本同量级（甚至略快，因为它少维护一份拷贝路径）：

```text
N=1000000000 次间接调用(同样 noinline 目标):
  std::function          : 1871 ms
  std::move_only_function: 1666 ms
```

体积上 `move_only_function` 略大（`sizeof` 40 字节 vs `function` 32 字节，libstdc++）。所以选型规则很清晰：**可调用对象是 copyable 的、且你需要拷贝整个包装（比如拷贝容器），用 `std::function`；可调用对象是 move-only（持有 `unique_ptr`、`promise`、文件句柄等独占资源），用 `move_only_function`**。

::: warning function_ref 还没进 C++23
你可能听过一个「非拥有、零分配、纯引用」的轻量可调用包装 `std::function_ref`。它在 C++23 的时间窗里被讨论过，但最终没赶上 C++23，被推迟到 C++26。所以在 C++23 下，你只有 `std::function` 和 `move_only_function` 两个「拥有式」选择；想要非拥有的轻量视图，目前得自己写或用第三方（如 `tl::function_ref`）。这点别被老资料带歪——我们实测在 GCC 16.1.1 下 `std::function_ref` 直接报 `'function_ref' is not a member of 'std'`。
:::

## 何时用、何时别用

把这几节的经验收成几条判断准则。

**该用 `std::function`（或 `move_only_function`）的场景：**

- **异质可调用对象的存储**：一个回调槽要能接函数指针、functor、lambda，类型擦除是唯一解。
- **运行时需要替换可调用对象**：同一个 `function` 变量先装 A 再装 B，这种「可重新赋值」的语义模板给不了。
- **跨 ABI 边界**：库的接口要暴露一个回调类型，模板没法放头文件里、或者要虚函数配合时，`function` 是稳定的类型擦除边界。
- **可调用对象持有独占资源**：用 `move_only_function`（C++23 起）。

**不该用的场景——能用更轻的手段就别擦除：**

- **能用模板就别用 `function`**。模板参数推导出具体类型，调用可内联，零开销。一个接收回调的算法写成模板 `template <typename F> void algo(F f)` 几乎总比写成 `void algo(std::function<...> f)` 好——除非 `algo` 是虚函数、或你要把 `F` 存起来。
- **零捕获、只用一个签名**。直接用函数指针 `int(*)(int)`，开销比 `function` 小、可拷贝、够用。
- **热路径上的高频回调**。前面实测看到了，调用体越轻，`function` 的间接开销占比越大。把热路径的回调换成模板或函数指针。

一句话总结：**类型擦除是为「异质、可替换、跨边界」买的单，不是为日常回调买的**。在不需要它的能力时，它只会白白引入一次间接调用和可能的堆分配。

## 小结

`<functional>` 的核心就这几样，关键结论收一下：

- 三类可调用对象——函数/函数指针、functor、lambda。lambda 编译期被翻译成「独一无二的闭包类型」（每个 lambda 表达式都是独立类型），本质和 functor 同类，只是编译器帮你生成类。零捕获 lambda 可转函数指针。
- `std::function` 用类型擦除把异质可调用对象包成同一类型，代价有三种：① 可能的堆分配（小捕获走 SBO 零分配，大捕获触发堆分配，实测 `int[64]` 捕获分配 256 字节）；② 间接调用，编译器无法内联，实测 10 亿次极轻调用 `function` 约 1.9s、函数指针约 1.6s、能内联的直接 lambda 被优化成 0s；③ 体积固定 32 字节（`sizeof`），容器里大量存要算内存预算。
- `std::bind` 在 C++14 lambda 面前基本过时——能别用就别用，新代码写 lambda。
- `std::reference_wrapper`（`ref`/`cref`）让容器能「存引用」，解 `vector<T&>` 编不过的硬限制；也给 bind 等按值传参的场合塞引用。
- `std::hash` 是无序容器的基石，对基本类型和 `string` 预置特化，自定义类型当 key 要自己特化。
- `std::invoke`（C++17）统一调用语法，泛型代码里调成员函数/成员指针不再别扭；`std::invoke_r<R>`（C++23）带返回类型。
- **`std::move_only_function`（C++23）是本篇的新意**：它只要求 Movable 不要求 Copyable，能存捕获 `unique_ptr` 等 move-only 资源的 lambda，这是 `std::function`（硬要求 Copyable）做不到的，实测在 GCC 16.1.1 下编译通过并正确工作。代价是它自身也不能拷贝。
- 选型：异质存储/运行时替换/跨 ABI 边界用 `function`（move-only 资源用 `move_only_function`）；能用模板或函数指针就别擦除，热路径尤其如此。

## 参考资源

- [cppreference: std::function](https://en.cppreference.com/w/cpp/utility/functional/function) —— 类型擦除可调用包装，含 SBO 与 Copyable 要求
- [cppreference: std::move_only_function (C++23)](https://en.cppreference.com/w/cpp/utility/functional/move_only_function) —— move-only 版本，不要求 Copyable
- [cppreference: std::reference_wrapper](https://en.cppreference.com/w/cpp/utility/functional/reference_wrapper) —— 引用包装，让容器存引用
- [cppreference: std::invoke / std::invoke_r](https://en.cppreference.com/w/cpp/utility/functional/invoke) —— 统一调用语法
- [cppreference: std::hash](https://en.cppreference.com/w/cpp/utility/hash) —— 无序容器的哈希基石
- [P0288: move_only_function](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0288r9.html) —— move_only_function 提案，说明 move-only 语义动机
