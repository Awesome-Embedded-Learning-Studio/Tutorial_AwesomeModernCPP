---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: 移动语义在标准库和自定义类型中的实际应用与性能对比
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'Chapter 0: 移动构造与移动赋值'
- 'Chapter 0: RVO 与 NRVO'
reading_time_minutes: 23
related:
- 完美转发
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: 移动语义实战：从 STL 到自定义类型
---
# 移动语义实战：从 STL 到自定义类型

理论铺垫够了，这篇把移动语义落到实际代码里。咱们要看清两件事：移动到底比拷贝快多少，以及 STL 容器和自定义类型各自怎么写才能吃到这份收益。代码和实测数据不少，建议您跟着敲一遍，亲手感受拷贝和移动的差距。

## STL 容器中的移动——无处不在的收益

标准库容器是移动语义最大的受益者之一。C++11 之后，所有标准库容器都实现了移动构造和移动赋值，这意味着容器之间的传递不再需要逐元素拷贝。

先看 `std::vector` 的 `push_back`。它有两个重载：一个接收 `const T&`（拷贝），一个接收 `T&&`（移动）。当您传入左值时调用拷贝版本，传入右值时调用移动版本。

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

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -O2 -o push_demo push_demo.cpp
./push_demo
```

输出：

```text
=== push_back 左值（拷贝）===
  [Alpha] 构造，数据量: 10000
  [Alpha_copy] 拷贝构造

=== push_back 右值（移动）===
  [Beta] 构造，数据量: 10000
  [Beta] 移动构造

=== emplace_back 原位构造 ===
  [Gamma] 构造，数据量: 10000

=== 程序结束 ===
  [(moved-from)] 析构，数据量: 0
  [Alpha] 析构，数据量: 10000
  [Alpha_copy] 析构，数据量: 10000
  [Beta] 析构，数据量: 10000
  [Gamma] 析构，数据量: 10000
```

三种方式的效果一目了然。`push_back(h1)` 触发拷贝——`h1` 的 10000 个 `int` 被完整复制。`push_back(std::move(h2))` 触发移动——只转移了 `vector` 的内部指针，`h2` 的 `data_` 变成空的。`emplace_back("Gamma", 10000)` 连移动都省了——直接在 vector 的空间里构造 `Heavy` 对象。

三种方式的性能排序是：`emplace_back` > `push_back(std::move(...))` > `push_back(lvalue)`。在日常编码中，如果您有一个现成的对象要放进容器，用 `std::move` 移动进去；如果您有构造参数，用 `emplace_back` 直接原位构造。

## swap 惯用法——移动语义的经典应用

`std::swap` 在 C++11 之后被重新实现为基于移动语义的版本。核心逻辑就是把两个对象的内容通过三次移动进行交换：

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

在排序算法中，swap 是最频繁的操作之一。`std::sort` 内部会大量调用 swap 来调整元素位置，高效的移动操作能让排序过程中每次元素调整的代价从 O(n) 降到 O(1)。需要特别说明的是，`noexcept` 对 `std::sort` 本身并没有直接影响——sort 内部直接使用 `std::move` 和 `std::swap`，不关心移动操作是否 `noexcept`（只要类型满足可移动构造和可移动赋值要求即可）。`noexcept` 真正发挥作用的场景是 `std::vector` 扩容：当 vector 需要把旧元素搬到新内存时，它会通过 `std::move_if_noexcept` 来选择策略——如果移动操作是 `noexcept` 的，就用移动；否则退回拷贝，以保证强异常安全。咱们用下面这个验证程序来证明这一点：

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

编译运行（GCC 16.1.1, -std=c++17 -O2, x86_64）：

```text
noexcept sort:  拷贝=0 移动=23516
非noexcept sort: 拷贝=0 移动=23516

noexcept 扩容:  拷贝=0 移动=255
非noexcept扩容: 拷贝=255 移动=0
```

数据非常清楚。`std::sort` 两种情况都只使用移动（23516 次），完全不区分 `noexcept`。但 `vector` 扩容就大不一样了：`noexcept` 的类型在扩容时使用移动（255 次移动），非 `noexcept` 的类型在扩容时全部退回拷贝（255 次拷贝）。如果您在 `vector` 里频繁 `push_back` 但没有提前 `reserve`，没有 `noexcept` 的移动会让每次扩容都变成全量拷贝——这才是 `noexcept` 真正影响性能的地方。

正确的自定义 swap 写法需要注意 ADL（Argument-Dependent Lookup）。标准做法是在类的命名空间中提供一个非成员的 `swap` 函数，然后让用户通过 `using std::swap; swap(a, b);` 的方式调用。这样 ADL 会优先找到您写的 swap，找不到时退回到 `std::swap`。

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

编译运行：

```bash
g++ -std=c++17 -O2 -Wall -Wextra -o move_bench move_benchmark.cpp
./move_bench
```

笔者的机器上输出（GCC 16.1.1, -O2, x86_64 WSL2，取一次稳定运行）：

```text
数据大小: 7812 KB
迭代次数: 100

仅构造（baseline）: 47.3 ms
构造 + 拷贝:        505.3 ms
构造 + 移动:        44.6 ms

=== 分离后的实际耗时 ===
纯拷贝: 458.1 ms
纯移动: -2.7 ms
移动耗时在测量噪声范围内（接近零）
```

这个结果比单报一个"加速比"有说服力。咱们逐行看：构造一个 `BigData`（分配约 8MB 内存并填充数据）花了 47ms，这是两组测试共有的固定开销。加上拷贝，总耗时飙到 505ms——纯拷贝占 458ms，因为要另开一块内存把 8MB 数据逐字节复制过去。加上移动，总耗时是 45ms，和纯构造几乎没差——说明移动操作本身在这个数据规模下根本测不出来。

> 💡 **测量噪声说明**：「纯移动」时间会在零附近抖动——这次是 -2.7 ms，换个时间点也许是个位数的正值，都正常。高精度计时器会捕捉到系统调度、缓存状态这些微小差异，而移动本身的开销远小于这些差异，所以被噪声淹没了。要紧的是它和纯拷贝那几百毫秒根本不在一个量级。

移动操作到底做了什么？它只是复制 `std::vector` 内部那几个指针大小的字段（指向堆缓冲区的指针、大小、容量），再把源对象的指针置空，拢共几条 CPU 指令，纳秒级别，在 47ms 的构造面前可以忽略。这就是为什么要把构造单独分离出来：如果不分离，您看到的"移动耗时"其实是 47ms 构造加上几纳秒移动，和 505ms 的构造加拷贝一比，只会得出一个"快十来倍"的结论——这个数被构造稀释了，反而把"移动几乎免费"这件事给盖住了。

> ⚠️ **踩坑预警**：不要在没有移动语义的类型上期待性能提升。`std::array<int, 1000>` 的"移动"和"拷贝"是等价的——因为 `std::array` 的数据直接存储在对象内部，没有指针可以转移。移动语义只在管理了间接资源（动态内存、文件句柄等）的类型上有实际收益。

## 自定义类型的移动最佳实践

把您学到的移动语义知识应用到自己的类上，这里有几条经过实战验证的最佳实践。

对于管理了动态资源的类（持有 `new` 出来的内存、`fopen` 打开的文件、或者类似的资源句柄），应该实现完整的规则五：自定义析构函数、拷贝构造、移动构造、拷贝赋值、移动赋值。移动构造和移动赋值中要把源对象的资源指针置空，确保源对象析构时不会释放已转移的资源。只要移动操作保证不抛出异常，就应该标记 `noexcept`（绝大多数情况下移动操作只是指针复制，不会抛出异常）。

对于只持有基本类型和标准库容器的类，通常可以用 `= default` 让编译器生成移动操作。`std::string`、`std::vector`、`std::map` 这些标准库组件都有高效的移动语义，编译器自动生成的移动构造函数会按照成员声明顺序逐个调用成员的移动构造函数（对类成员）或直接复制（对标量成员）。这符合 C++ 标准的规定（参见 C++17 [class.copy.ctor]）。

```cpp
struct UserProfile
{
    std::string name;
    std::string email;
    std::vector<std::string> permissions;
    int level = 0;

    // 编译器生成的移动操作已经足够好
    // 因为 std::string 和 std::vector 都有 noexcept 移动
    ~UserProfile() = default;
    UserProfile(const UserProfile&) = default;
    UserProfile(UserProfile&&) noexcept = default;
    UserProfile& operator=(const UserProfile&) = default;
    UserProfile& operator=(UserProfile&&) noexcept = default;
};
```

对于封装了独占资源的类（文件句柄、网络连接、锁），应该**禁用拷贝、启用移动**。拷贝没有意义——您不能"复制"一个 TCP 连接或一个互斥锁。但移动是合理的——您可以把连接的控制权从一个对象转移到另一个对象。

```cpp
class NetworkConnection
{
    int socket_fd_;

public:
    explicit NetworkConnection(const char* host, int port);
    ~NetworkConnection() { if (socket_fd_ >= 0) close_socket(socket_fd_); }

    // 禁止拷贝
    NetworkConnection(const NetworkConnection&) = delete;
    NetworkConnection& operator=(const NetworkConnection&) = delete;

    // 允许移动
    NetworkConnection(NetworkConnection&& other) noexcept
        : socket_fd_(other.socket_fd_)
    {
        other.socket_fd_ = -1;  // 标记为已转移
    }

    NetworkConnection& operator=(NetworkConnection&& other) noexcept
    {
        if (this != &other) {
            if (socket_fd_ >= 0) close_socket(socket_fd_);
            socket_fd_ = other.socket_fd_;
            other.socket_fd_ = -1;
        }
        return *this;
    }
};
```

## 嵌入式实战应用——资源句柄的移动

虽然本系列教程以通用 C++ 为主，但移动语义在嵌入式开发中也有非常实际的应用场景。在资源受限的嵌入式系统上，避免不必要的拷贝不仅能提升性能，有时甚至是功能正确性的保证——比如 DMA 缓冲区的所有权必须唯一、外设的访问权限不可共享。

下面是一个简化但真实的 DMA 缓冲区管理类，展示了移动语义如何确保资源所有权的唯一性：

```cpp
#include <cstddef>
#include <cstring>
#include <utility>
#include <iostream>

/// @brief 模拟的 DMA 缓冲区管理
/// 在真实嵌入式项目中，allocate_dma_buffer 和 free_dma_buffer
/// 会对接到实际的内存管理单元或内存池
class DMABuffer
{
    void* buffer_;       // 指向 DMA 缓冲区
    std::size_t size_;   // 缓冲区大小

public:
    explicit DMABuffer(std::size_t size)
        : buffer_(::operator new(size))
        , size_(size)
    {
        std::memset(buffer_, 0, size_);
        std::cout << "  [DMA] 分配 " << size << " 字节\n";
    }

    ~DMABuffer()
    {
        if (buffer_) {
            ::operator delete(buffer_);
            std::cout << "  [DMA] 释放 " << size_ << " 字节\n";
        }
    }

    // 禁止拷贝：DMA 缓冲区不能有两份
    DMABuffer(const DMABuffer&) = delete;
    DMABuffer& operator=(const DMABuffer&) = delete;

    // 允许移动：所有权可以转移
    DMABuffer(DMABuffer&& other) noexcept
        : buffer_(other.buffer_)
        , size_(other.size_)
    {
        other.buffer_ = nullptr;
        other.size_ = 0;
        std::cout << "  [DMA] 所有权转移（移动构造）\n";
    }

    DMABuffer& operator=(DMABuffer&& other) noexcept
    {
        if (this != &other) {
            if (buffer_) {
                ::operator delete(buffer_);
            }
            buffer_ = other.buffer_;
            size_ = other.size_;
            other.buffer_ = nullptr;
            other.size_ = 0;
            std::cout << "  [DMA] 所有权转移（移动赋值）\n";
        }
        return *this;
    }

    void* data() { return buffer_; }
    const void* data() const { return buffer_; }
    std::size_t size() const { return size_; }
};

/// @brief 模拟从 DMA 接收数据
DMABuffer receive_dma(std::size_t expected_size)
{
    DMABuffer buf(expected_size);
    // 在真实系统中，这里会触发 DMA 传输并等待完成
    // buf.data() 指向的内存由 DMA 控制器直接写入
    char msg[] = "DMA data received";
    std::memcpy(buf.data(), msg, sizeof(msg));
    return buf;  // NRVO 或移动语义确保零拷贝返回
}

int main()
{
    std::cout << "=== 嵌入式 DMA 缓冲区管理 ===\n\n";

    // 从 DMA 接收数据——缓冲区所有权从函数转移到 main
    auto rx_buf = receive_dma(1024);
    std::cout << "  接收到: " << static_cast<const char*>(rx_buf.data()) << "\n\n";

    // 把缓冲区转移到处理队列（模拟）
    std::cout << "=== 转移到处理队列 ===\n";
    DMABuffer process_buf = std::move(rx_buf);
    std::cout << "  rx_buf 大小: " << rx_buf.size() << "\n";
    std::cout << "  process_buf 大小: " << process_buf.size() << "\n\n";

    std::cout << "=== 程序结束，资源自动释放 ===\n";
    return 0;
}
```

运行输出：

```text
=== 嵌入式 DMA 缓冲区管理 ===

  [DMA] 分配 1024 字节
  接收到: DMA data received

=== 转移到处理队列 ===
  [DMA] 所有权转移（移动构造）
  rx_buf 大小: 0
  process_buf 大小: 1024

=== 程序结束，资源自动释放 ===
  [DMA] 释放 1024 字节
```

注意整个生命周期中只分配了一次 1024 字节的缓冲区——从 `receive_dma` 内部创建，到 `main` 中的 `rx_buf`（通过 NRVO 或移动），再到 `process_buf`（通过移动构造），始终只有一份缓冲区在流转。没有多余的内存分配，没有数据拷贝，更不会出现两个对象同时操作同一个 DMA 缓冲区的情况——因为拷贝被 `= delete` 禁止了。

## 练习——实现一个支持移动的动态数组

理论看得再多不如动手写一遍。这个练习要求您实现一个简化版的动态数组类，支持拷贝语义和移动语义。这个类不需要像 `std::vector` 那么复杂，但需要正确处理资源管理。

要求如下：类名 `SimpleVector`，内部用 `new[]` 分配的 `int` 数组存储数据。支持 `push_back(int)` 添加元素，必要时扩容（可以简单地按 2 倍增长）。实现完整的规则五。移动操作标记 `noexcept`。实现 `size()` 和 `operator[]`。写一段测试代码验证拷贝和移动的行为。

以下是参考实现框架：

```cpp
// simple_vector.cpp -- 练习：支持移动的动态数组
// Standard: C++17

#include <iostream>
#include <algorithm>
#include <utility>

class SimpleVector
{
    int* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    SimpleVector() : data_(nullptr), size_(0), capacity_(0) {}

    explicit SimpleVector(std::size_t cap)
        : data_(new int[cap])
        , size_(0)
        , capacity_(cap)
    {
    }

    // TODO: 实现析构函数
    // TODO: 实现拷贝构造函数（深拷贝）
    // TODO: 实现移动构造函数（指针转移 + 源对象置空）
    // TODO: 实现拷贝赋值运算符
    // TODO: 实现移动赋值运算符

    void push_back(int value)
    {
        if (size_ >= capacity_) {
            std::size_t new_cap = capacity_ == 0 ? 4 : capacity_ * 2;
            int* new_data = new int[new_cap];
            std::copy(data_, data_ + size_, new_data);
            delete[] data_;
            data_ = new_data;
            capacity_ = new_cap;
        }
        data_[size_++] = value;
    }

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }

    int& operator[](std::size_t i) { return data_[i]; }
    const int& operator[](std::size_t i) const { return data_[i]; }
};

int main()
{
    // 测试代码
    SimpleVector a;
    for (int i = 0; i < 10; ++i) {
        a.push_back(i * i);
    }

    std::cout << "a: ";
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::cout << a[i] << " ";
    }
    std::cout << "\n";

    // 测试拷贝构造
    SimpleVector b = a;
    std::cout << "b (拷贝): ";
    for (std::size_t i = 0; i < b.size(); ++i) {
        std::cout << b[i] << " ";
    }
    std::cout << "\n";

    // 测试移动构造
    SimpleVector c = std::move(a);
    std::cout << "c (移动): ";
    for (std::size_t i = 0; i < c.size(); ++i) {
        std::cout << c[i] << " ";
    }
    std::cout << "\n";
    std::cout << "a 移动后: size=" << a.size()
              << ", capacity=" << a.capacity() << "\n";

    return 0;
}
```

如果您卡住了，可以参考前面 `Buffer` 类的实现——逻辑几乎完全一样。关键点是：析构函数里 `delete[] data_`，移动构造里转移指针并置空源对象的指针，拷贝构造里分配新内存并复制数据，移动赋值里先 `delete[]` 当前数据再接管新数据。

完整的参考实现：

```cpp
// simple_vector_solution.cpp -- 练习参考答案
// Standard: C++17

#include <iostream>
#include <algorithm>
#include <utility>

class SimpleVector
{
    int* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    SimpleVector() : data_(nullptr), size_(0), capacity_(0) {}

    explicit SimpleVector(std::size_t cap)
        : data_(cap > 0 ? new int[cap] : nullptr)
        , size_(0)
        , capacity_(cap)
    {
    }

    ~SimpleVector()
    {
        delete[] data_;
    }

    // 拷贝构造：深拷贝
    SimpleVector(const SimpleVector& other)
        : data_(other.capacity_ > 0 ? new int[other.capacity_] : nullptr)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        if (data_) {
            std::copy(other.data_, other.data_ + other.size_, data_);
        }
    }

    // 移动构造：指针转移
    SimpleVector(SimpleVector&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // 拷贝赋值
    SimpleVector& operator=(const SimpleVector& other)
    {
        if (this != &other) {
            delete[] data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            data_ = capacity_ > 0 ? new int[capacity_] : nullptr;
            if (data_) {
                std::copy(other.data_, other.data_ + size_, data_);
            }
        }
        return *this;
    }

    // 移动赋值
    SimpleVector& operator=(SimpleVector&& other) noexcept
    {
        if (this != &other) {
            delete[] data_;
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    void push_back(int value)
    {
        if (size_ >= capacity_) {
            std::size_t new_cap = capacity_ == 0 ? 4 : capacity_ * 2;
            int* new_data = new int[new_cap];
            std::copy(data_, data_ + size_, new_data);
            delete[] data_;
            data_ = new_data;
            capacity_ = new_cap;
        }
        data_[size_++] = value;
    }

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }
    const int* data() const { return data_; }

    int& operator[](std::size_t i) { return data_[i]; }
    const int& operator[](std::size_t i) const { return data_[i]; }
};

int main()
{
    SimpleVector a;
    for (int i = 0; i < 10; ++i) {
        a.push_back(i * i);
    }

    std::cout << "a: ";
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::cout << a[i] << " ";
    }
    std::cout << "\n";
    std::cout << "  a.size()=" << a.size() << ", a.capacity()=" << a.capacity() << "\n\n";

    SimpleVector b = a;   // 拷贝构造
    std::cout << "b (拷贝构造): ";
    for (std::size_t i = 0; i < b.size(); ++i) {
        std::cout << b[i] << " ";
    }
    std::cout << "\n\n";

    SimpleVector c = std::move(a);  // 移动构造
    std::cout << "c (移动构造): ";
    for (std::size_t i = 0; i < c.size(); ++i) {
        std::cout << c[i] << " ";
    }
    std::cout << "\n";
    std::cout << "  a 移动后: size=" << a.size()
              << ", capacity=" << a.capacity() << "\n\n";

    // 验证移动后的 a 可以安全使用
    a = SimpleVector(5);  // 移动赋值一个新对象
    a.push_back(999);
    std::cout << "a 重新赋值后: ";
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::cout << a[i] << " ";
    }
    std::cout << "\n";

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o simple_vec simple_vector_solution.cpp
./simple_vec
```

预期输出：

```text
a: 0 1 4 9 16 25 36 49 64 81
  a.size()=10, a.capacity()=16

b (拷贝构造): 0 1 4 9 16 25 36 49 64 81

c (移动构造): 0 1 4 9 16 25 36 49 64 81
  a 移动后: size=0, capacity=0

a 重新赋值后: 999
```

拷贝构造后 `b` 拥有独立的数据副本，修改 `b` 不影响 `a`。移动构造后 `c` 接管了 `a` 的所有数据，`a` 变成空的状态（size=0, capacity=0）。之后 `a` 可以通过移动赋值重新获得一个有效的对象，证明移动后的对象确实处于"有效但未指定"的状态——它可以被安全地赋新值、析构，但您不应该依赖它的当前值。

## 在线运行

在线运行两个示例，亲手验证这篇的关键结论：

<OnlineCompilerDemo
  title="push_back vs emplace_back：拷贝、移动、原位构造"
  source-path="code/examples/vol2/push_back_emplace.cpp"
  description="追踪 Heavy 对象的构造、拷贝、移动、析构日志，对比 push_back(左值)、push_back(右值)、emplace_back 三种方式的开销。"
/>

<OnlineCompilerDemo
  title="noexcept 对 sort 与 vector 扩容的影响"
  source-path="code/examples/vol2/noexcept_sort_vs_realloc.cpp"
  description="统计拷贝和移动次数：std::sort 不区分 noexcept，但 vector 扩容会通过 move_if_noexcept 在非 noexcept 类型上退回拷贝。"
/>

移动语义这一章到这里就讲完了。从右值引用的绑定规则，到移动构造的实现，再到 RVO/NRVO 和完美转发，最后落到这篇的性能实测——希望您以后看到 `std::move`，不再只是照抄，而是清楚它在做什么、为什么这么做。

顺着资源所有权这条线，下一章咱们聊智能指针：RAII 会把这一章里那些手动的 `delete`、所有权转移，变成编译器自动管的事。
