---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: 移动语义在标准库容器中的收益、swap 惯用法，以及拷贝与移动的性能实测
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'Chapter 0: 移动构造与移动赋值'
- 'Chapter 0: RVO 与 NRVO'
reading_time_minutes: 12
related:
- 完美转发
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: 移动语义实战：标准库容器与性能实测
---
# 移动语义实战：从 STL 到自定义类型

理论铺垫够了，这篇先看标准库这一侧：STL 容器怎么吃到移动的收益，swap 惯用法为什么快，移动到底比拷贝快多少。代码和实测数据不少，建议您跟着敲一遍，亲手感受拷贝和移动的差距。

## STL 容器中的移动——无处不在的收益

标准库容器是移动语义最大的受益者之一。C++11 之后，所有标准库容器都实现了移动构造和移动赋值，这意味着容器之间的传递不再需要逐元素拷贝。

先看 `std::vector` 的 `push_back`。它有两个重载：一个接收 `const T&`（拷贝），一个接收 `T&&`（移动）<RefLink :id="1" preview="cppreference std::vector — push_back overloads and emplace_back" />。当您传入左值时调用拷贝版本，传入右值时调用移动版本。

```cpp
#include <iostream>
#include <vector>
#include <string>

class Heavy
{
    std::string name_;
    std::vector<int> data_;

public:
    explicit Heavy(std::string name, std::size_t n)
        : name_(std::move(name))
        , data_(n, 42)
    {
        std::cout << "  [" << name_ << "] 构造，数据量: "
                  << data_.size() << "\n";
    }

    Heavy(const Heavy& other)
        : name_(other.name_ + "_copy")
        , data_(other.data_)
    {
        std::cout << "  [" << name_ << "] 拷贝构造\n";
    }

    Heavy(Heavy&& other) noexcept
        : name_(std::move(other.name_))
        , data_(std::move(other.data_))
    {
        other.name_ = "(moved-from)";
        std::cout << "  [" << name_ << "] 移动构造\n";
    }

    ~Heavy()
    {
        std::cout << "  [" << name_ << "] 析构，数据量: "
                  << data_.size() << "\n";
    }

    const std::string& name() const { return name_; }
    std::size_t data_size() const { return data_.size(); }
};

int main()
{
    std::vector<Heavy> items;
    items.reserve(4);

    std::cout << "=== push_back 左值（拷贝）===\n";
    Heavy h1("Alpha", 10000);
    items.push_back(h1);

    std::cout << "\n=== push_back 右值（移动）===\n";
    Heavy h2("Beta", 10000);
    items.push_back(std::move(h2));

    std::cout << "\n=== emplace_back 原位构造 ===\n";
    items.emplace_back("Gamma", 10000);

    std::cout << "\n=== 程序结束 ===\n";
    return 0;
}
```

这份演示程序就在下面，点「动手试一试」直接跑：

<OnlineCompilerDemo allow-run
  title="push_back vs emplace_back：拷贝、移动、原位构造"
  source-path="code/examples/vol2/push_back_emplace.cpp"
  description="追踪 Heavy 对象的构造、拷贝、移动、析构日志，对比 push_back(左值)、push_back(右值)、emplace_back 三种方式的开销。"
  run-options="-O2 -std=c++17"
/>

三种方式的效果一目了然。`push_back(h1)` 触发拷贝——`h1` 的 10000 个 `int` 被完整复制。`push_back(std::move(h2))` 触发移动——只转移了 `vector` 的内部指针，`h2` 的 `data_` 变成空的。`emplace_back("Gamma", 10000)` 连移动都省了——直接在 vector 的空间里构造 `Heavy` 对象。

三种方式的性能排序是：`emplace_back` > `push_back(std::move(...))` > `push_back(lvalue)`。在日常编码中，如果您有一个现成的对象要放进容器，用 `std::move` 移动进去；如果您有构造参数，用 `emplace_back` 直接原位构造。

## swap 惯用法——移动语义的经典应用

`std::swap` 在 C++11 之后被重新实现为基于移动语义的版本<RefLink :id="2" preview="cppreference std::swap — three moves, conditionally noexcept since C++11" />。核心逻辑就是把两个对象的内容通过三次移动进行交换：

```cpp
// std::swap 的简化实现（C++11 之后）
template<typename T>
void swap(T& a, T& b) noexcept(
    std::is_nothrow_move_constructible_v<T> &&
    std::is_nothrow_move_assignable_v<T>)
{
    T temp = std::move(a);   // 移动构造 temp
    a = std::move(b);        // 移动赋值
    b = std::move(temp);     // 移动赋值
}
```

三次移动操作完成了两个对象的交换。对于通过指针间接管理资源的类（内部持有 `new` 出来的内存、文件描述符等），每次移动只是指针转移，所以整个 swap 的代价是 O(1)——与对象管理的资源大小无关。但要注意前提：这条结论依赖于"资源是间接持有的"。如果您的对象像 `std::array<int, 1000>` 那样把数据直接存在对象内部（没有间接层），那么移动和拷贝是等价的——swap 仍然是 O(n)。相比之下，C++03 的 swap 对间接持有资源的类型需要一次拷贝构造加两次拷贝赋值，代价是 O(n)。

在排序算法中，swap 是最频繁的操作之一。`std::sort` 内部会大量调用 swap 来调整元素位置，高效的移动操作能让排序过程中每次元素调整的代价从 O(n) 降到 O(1)。需要特别说明的是，`noexcept` 对 `std::sort` 本身并没有直接影响——sort 内部直接使用 `std::move` 和 `std::swap`，不关心移动操作是否 `noexcept`（只要类型满足可移动构造和可移动赋值要求即可）。`noexcept` 真正发挥作用的场景是 `std::vector` 扩容：当 vector 需要把旧元素搬到新内存时，它会通过 `std::move_if_noexcept` 来选择策略——如果移动操作是 `noexcept` 的，就用移动；否则退回拷贝，以保证强异常安全<RefLink :id="3" preview="cppreference std::move_if_noexcept — move unless the move ctor may throw" />。咱们用下面这个验证程序来证明这一点：

```cpp
// noexcept_sort_vs_realloc_verify.cpp -- 验证 noexcept 对 sort 和 vector 扩容的影响
// 完整可编译版本见 code/examples/vol2/noexcept_sort_vs_realloc.cpp

#include <iostream>
#include <vector>
#include <algorithm>
#include <string>

struct NoexceptType
{
    std::string payload;
    int value;

    static int copy_count;
    static int move_count;

    NoexceptType(int v) : payload("data"), value(v) {}
    NoexceptType(const NoexceptType& o)
        : payload(o.payload + "_c"), value(o.value) { ++copy_count; }
    NoexceptType(NoexceptType&& o) noexcept
        : payload(std::move(o.payload)), value(o.value)
    {
        o.payload = "(moved)";
        ++move_count;
    }
    NoexceptType& operator=(NoexceptType&& o) noexcept
    {
        payload = std::move(o.payload);
        value = o.value;
        o.payload = "(moved)";
        ++move_count;
        return *this;
    }
    NoexceptType& operator=(const NoexceptType& o)
    {
        payload = o.payload + "_c";
        value = o.value;
        ++copy_count;
        return *this;
    }
    bool operator<(const NoexceptType& rhs) const { return value < rhs.value; }
    static void reset() { copy_count = 0; move_count = 0; }
};

int NoexceptType::copy_count = 0;
int NoexceptType::move_count = 0;

// ThrowingType 与 NoexceptType 完全相同，唯一区别是移动操作没有 noexcept
struct ThrowingType
{
    std::string payload;
    int value;

    static int copy_count;
    static int move_count;

    ThrowingType(int v) : payload("data"), value(v) {}
    ThrowingType(const ThrowingType& o)
        : payload(o.payload + "_c"), value(o.value) { ++copy_count; }
    ThrowingType(ThrowingType&& o) // 注意：没有 noexcept
        : payload(std::move(o.payload)), value(o.value)
    {
        o.payload = "(moved)";
        ++move_count;
    }
    ThrowingType& operator=(ThrowingType&& o) // 注意：没有 noexcept
    {
        payload = std::move(o.payload);
        value = o.value;
        o.payload = "(moved)";
        ++move_count;
        return *this;
    }
    ThrowingType& operator=(const ThrowingType& o)
    {
        payload = o.payload + "_c";
        value = o.value;
        ++copy_count;
        return *this;
    }
    bool operator<(const ThrowingType& rhs) const { return value < rhs.value; }
    static void reset() { copy_count = 0; move_count = 0; }
};

int ThrowingType::copy_count = 0;
int ThrowingType::move_count = 0;

int main()
{
    const int kCount = 5000;

    // Test 1: std::sort（noexcept 类型）
    {
        std::vector<NoexceptType> vec;
        vec.reserve(kCount);
        for (int i = 0; i < kCount; ++i) vec.emplace_back(kCount - i);
        NoexceptType::reset();
        std::sort(vec.begin(), vec.end());
        std::cout << "noexcept sort:  拷贝=" << NoexceptType::copy_count
                  << " 移动=" << NoexceptType::move_count << "\n";
    }

    // Test 2: std::sort（非 noexcept 类型）
    {
        std::vector<ThrowingType> vec;
        vec.reserve(kCount);
        for (int i = 0; i < kCount; ++i) vec.emplace_back(kCount - i);
        ThrowingType::reset();
        std::sort(vec.begin(), vec.end());
        std::cout << "非noexcept sort: 拷贝=" << ThrowingType::copy_count
                  << " 移动=" << ThrowingType::move_count << "\n";
    }

    std::cout << "\n";

    // Test 3: vector 扩容（noexcept 类型，无 reserve）
    {
        NoexceptType::reset();
        std::vector<NoexceptType> vec;
        for (int i = 0; i < 200; ++i) vec.emplace_back(i);
        std::cout << "noexcept 扩容:  拷贝=" << NoexceptType::copy_count
                  << " 移动=" << NoexceptType::move_count << "\n";
    }

    // Test 4: vector 扩容（非 noexcept 类型，无 reserve）
    // ThrowingType 的扩容会退回拷贝，因为 move_if_noexcept 不选中它的移动
    {
        ThrowingType::reset();
        std::vector<ThrowingType> vec;
        for (int i = 0; i < 200; ++i) vec.emplace_back(i);
        std::cout << "非noexcept扩容: 拷贝=" << ThrowingType::copy_count
                  << " 移动=" << ThrowingType::move_count << "\n";
    }
}
```

这份验证程序就在下面，点「动手试一试」直接跑（完整可编译版本即 `code/examples/vol2/noexcept_sort_vs_realloc.cpp`）：

<OnlineCompilerDemo allow-run
  title="noexcept 对 sort 与 vector 扩容的影响"
  source-path="code/examples/vol2/noexcept_sort_vs_realloc.cpp"
  description="统计拷贝和移动次数：std::sort 不区分 noexcept，但 vector 扩容会通过 move_if_noexcept 在非 noexcept 类型上退回拷贝。"
  run-options="-O2 -std=c++17"
/>

数据非常清楚。`std::sort` 两种情况都只使用移动（GCC 16 上是 23516 次，具体次数随标准库实现而变），完全不区分 `noexcept`。但 `vector` 扩容就大不一样了：`noexcept` 的类型在扩容时使用移动（255 次移动），非 `noexcept` 的类型在扩容时全部退回拷贝（255 次拷贝）。如果您在 `vector` 里频繁 `push_back` 但没有提前 `reserve`，没有 `noexcept` 的移动会让每次扩容都变成全量拷贝——这才是 `noexcept` 真正影响性能的地方。

正确的自定义 swap 写法需要注意 ADL（Argument-Dependent Lookup）。标准做法是在类的命名空间中提供一个非成员的 `swap` 函数，然后让用户通过 `using std::swap; swap(a, b);` 的方式调用。这样 ADL 会优先找到您写的 swap，找不到时退回到 `std::swap`<RefLink :id="4" preview="cppreference Swappable named requirement — ADL two-step for customizing swap" />。

```cpp
namespace mylib {

class BigBuffer
{
    int* data_;
    std::size_t size_;

public:
    explicit BigBuffer(std::size_t n)
        : data_(new int[n]()), size_(n) {}

    ~BigBuffer() { delete[] data_; }

    BigBuffer(const BigBuffer& other)
        : data_(new int[other.size_]), size_(other.size_)
    {
        std::memcpy(data_, other.data_, size_ * sizeof(int));
    }

    BigBuffer(BigBuffer&& other) noexcept
        : data_(other.data_), size_(other.size_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    BigBuffer& operator=(BigBuffer other) noexcept
    {
        swap(*this, other);
        return *this;
    }

    friend void swap(BigBuffer& a, BigBuffer& b) noexcept
    {
        using std::swap;
        swap(a.data_, b.data_);
        swap(a.size_, b.size_);
    }
};

}  // namespace mylib
```

这里咱们用了 copy-and-swap 惯用法来实现赋值运算符，用 `friend swap` 来提供高效的交换操作。`swap` 本身只是交换两个指针和两个整数——代价微乎其微。

## 性能对比——拷贝 vs 移动的 benchmark

理论讲了一大堆，数字最有说服力。咱们来做一个 benchmark，对比拷贝和移动的实际耗时。这一次咱们把构造的开销单独分离出来，这样您能看到纯粹的移动操作到底有多快。

```cpp
// move_benchmark.cpp -- 拷贝 vs 移动性能对比（分离构造开销）
// Standard: C++17

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <numeric>

class BigData
{
    std::vector<double> payload_;

public:
    explicit BigData(std::size_t n) : payload_(n)
    {
        std::iota(payload_.begin(), payload_.end(), 0.0);
    }

    BigData(const BigData& other) : payload_(other.payload_) {}
    BigData(BigData&& other) noexcept = default;
    BigData& operator=(const BigData&) = default;
    BigData& operator=(BigData&&) noexcept = default;
};

/// @brief 测量函数执行时间的辅助模板
template<typename Func>
double measure_ms(Func&& func, int iterations)
{
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        func();
    }
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int main()
{
    constexpr std::size_t kDataSize = 1000000;   // 100 万个 double，约 8MB
    constexpr int kIterations = 100;

    std::cout << "数据大小: " << kDataSize * sizeof(double) / 1024
              << " KB\n";
    std::cout << "迭代次数: " << kIterations << "\n\n";

    // 测试 0：仅构造（baseline）
    auto construct_time = measure_ms([&]() {
        BigData source(kDataSize);
        (void)source;
    }, kIterations);

    std::cout << "仅构造（baseline）: " << construct_time << " ms\n";

    // 测试 1：构造 + 拷贝
    auto copy_time = measure_ms([&]() {
        BigData source(kDataSize);
        BigData copy = source;  // 拷贝构造
        (void)copy;
    }, kIterations);

    std::cout << "构造 + 拷贝:        " << copy_time << " ms\n";

    // 测试 2：构造 + 移动
    auto move_time = measure_ms([&]() {
        BigData source(kDataSize);
        BigData moved = std::move(source);  // 移动构造
        (void)moved;
    }, kIterations);

    std::cout << "构造 + 移动:        " << move_time << " ms\n\n";

    // 分离出纯粹的拷贝/移动耗时
    double actual_copy = copy_time - construct_time;
    double actual_move = move_time - construct_time;

    std::cout << "=== 分离后的实际耗时 ===\n";
    std::cout << "纯拷贝: " << actual_copy << " ms\n";
    std::cout << "纯移动: " << actual_move << " ms\n";

    if (actual_move > 0.01) {
        std::cout << "加速比: " << actual_copy / actual_move << "x\n";
    } else {
        std::cout << "移动耗时在测量噪声范围内（接近零）\n";
    }

    return 0;
}
```

这个 benchmark 就在下面，点「动手试一试」直接跑——您机器上的绝对数字会和笔者的不一样，量级关系才是重点：

<OnlineCompilerDemo allow-run
  title="动手测量：move_benchmark.cpp"
  source-path="code/examples/vol2/move_benchmark.cpp"
  description="在线测量拷贝 vs 移动的耗时（分离构造开销）。笔者一次稳定运行：纯拷贝 458.1 ms，纯移动在噪声范围内（约 -2.7 ms）。"
  run-options="-O2 -std=c++17"
/>

这个结果比单报一个"加速比"有说服力。咱们拿笔者的一次稳定运行（GCC 16.1.1, -O2, x86_64 WSL2）逐行看：构造一个 `BigData`（分配约 8MB 内存并填充数据）花了 47ms，这是两组测试共有的固定开销。加上拷贝，总耗时飙到 505ms——纯拷贝占 458ms，因为要另开一块内存把 8MB 数据逐字节复制过去。加上移动，总耗时是 45ms，和纯构造几乎没差——说明移动操作本身在这个数据规模下根本测不出来。

> 💡 **测量噪声说明**："纯移动"时间会在零附近抖动——这次是 -2.7 ms，换个时间点也许是个位数的正值，都正常。高精度计时器会捕捉到系统调度、缓存状态这些微小差异，而移动本身的开销远小于这些差异，所以被噪声淹没了。要紧的是它和纯拷贝那几百毫秒根本不在一个量级。

移动操作到底做了什么？它只是复制 `std::vector` 内部那几个指针大小的字段（指向堆缓冲区的指针、大小、容量），再把源对象的指针置空，拢共几条 CPU 指令，纳秒级别，在 47ms 的构造面前可以忽略。这就是为什么要把构造单独分离出来：如果不分离，您看到的"移动耗时"其实是 47ms 构造加上几纳秒移动，和 505ms 的构造加拷贝一比，只会得出一个"快十来倍"的结论——这个数被构造稀释了，反而把"移动几乎免费"这件事给盖住了。

不要在没有移动语义的类型上期待性能提升。`std::array<int, 1000>` 的"移动"和"拷贝"是等价的——因为 `std::array` 的数据直接存储在对象内部，没有指针可以转移。移动语义只在管理了间接资源（动态内存、文件句柄等）的类型上有实际收益。

标准库这边看完了，收益也量出来了。下一篇轮到您自己的类型：规则五怎么落地、哪些类该禁拷贝只留移动、资源句柄在嵌入式场景里怎么安全转手。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="std::vector"
    chapter="push_back / emplace_back overloads"
    url="https://en.cppreference.com/w/cpp/container/vector"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="std::swap"
    url="https://en.cppreference.com/w/cpp/algorithm/swap"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="std::move_if_noexcept"
    url="https://en.cppreference.com/w/cpp/utility/move_if_noexcept"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Swappable (Named Requirement)"
    url="https://en.cppreference.com/w/cpp/named_req/Swappable"
  />
</ReferenceCard>
