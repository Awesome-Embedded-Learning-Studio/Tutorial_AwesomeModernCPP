---
title: 'Move Semantics in Practice: From STL to Custom Types'
description: 移动语义在标准库和自定义类型中的实际应用与性能对比
chapter: 0
order: 5
tags:
- host
- cpp-modern
- intermediate
- 移动语义
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
reading_time_minutes: 20
prerequisites:
- 'Chapter 0: 移动构造与移动赋值'
- 'Chapter 0: RVO 与 NRVO'
related:
- 完美转发
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/05-move-in-practice.md
  source_hash: 67b03b192397fcccb49a2ff1a51a34528037856d00b95231857e91e36f53720a
  translated_at: '2026-05-26T11:19:49.630252+00:00'
  engine: anthropic
  token_count: 5735
---
# Move Semantics in Practice: From STL to Custom Types

In the previous four articles, we thoroughly covered the theoretical foundations of move semantics: value categories, rvalue references, move construction and move assignment, RVO/NRVO, and perfect forwarding. Now it is time to put theory into practice. Let us look at how much of a real-world performance difference move semantics can make, and how to use it correctly with STL containers and custom types. This article includes a fair amount of code and real benchmark data, so we recommend following along and typing it out yourself to experience the difference between copying and moving firsthand.

## Move Semantics in STL Containers — Ubiquitous Benefits

Standard library containers are among the biggest beneficiaries of move semantics. Since C++11, all standard library containers have implemented move constructors and move assignment operators, meaning that transferring between containers no longer requires element-by-element copying.

First, let us look at the `push_back` of `std::vector`. It has two overloads: one accepting an lvalue reference (copy), and one accepting an rvalue reference (move). When you pass an lvalue, the copy version is called; when you pass an rvalue, the move version is called.

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

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -O2 -o push_demo push_demo.cpp
./push_demo
```

Output:

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

The effects of the three approaches are clear at a glance. `v3` triggers a copy — the 10,000 `int`s in `src` are fully duplicated. `v4` triggers a move — only the internal pointer of `src` is transferred, and `src`'s `vector` becomes empty. `v5` skips even the move — the `vector` object is constructed directly in place.

The performance ranking of the three approaches is: `emplace_back` > `push_back(std::move(...))` > `push_back(lvalue)`. In daily coding, if you have an existing object to put into a container, use `push_back(std::move(...))` to move it in; if you have constructor arguments, use `emplace_back` to construct it directly in place.

## The swap Idiom — A Classic Application of Move Semantics

`std::swap` was reimplemented in C++11 as a version based on move semantics. The core logic is to exchange the contents of two objects through three moves:

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

Three move operations complete the exchange of two objects. For classes that manage resources indirectly through pointers (holding memory from `new`, file descriptors, etc.), each move is just a pointer transfer, so the cost of the entire swap is O(1) — regardless of the size of the managed resources. But note the prerequisite: this conclusion relies on "resources being held indirectly." If your object stores data directly inside itself like `std::array` (with no indirection layer), then moving and copying are equivalent — swap remains O(n). In contrast, C++03's swap for types with indirectly held resources required one copy construction plus two copy assignments, at a cost of O(n).

In sorting algorithms, swap is one of the most frequent operations. `std::sort` internally calls swap extensively to adjust element positions, and efficient move operations can reduce the cost of each element adjustment during sorting from O(n) to O(1). It is worth specifically noting that `noexcept` has no direct impact on `std::sort` itself — sort internally uses `std::move` and placement `new`, and does not care whether the move operation is `noexcept` (as long as the type satisfies the move-constructible and move-assignable requirements). Where `noexcept` truly comes into play is during `std::vector` reallocation: when a vector needs to move old elements to new memory, it uses `std::move_if_noexcept` to select a strategy — if the move operation is `noexcept`, it uses move; otherwise, it falls back to copy to guarantee strong exception safety. We use the following verification program to prove this point:

```cpp
// noexcept_sort_vs_realloc_verify.cpp -- 验证 noexcept 对 sort 和 vector 扩容的影响
// 完整代码见 code/volumn_codes/vol2/ch00-move-semantics/

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
// （完整代码见仓库）
// ...

int main()
{
    const int kCount = 5000;

    // Test 1: std::sort
    {
        std::vector<NoexceptType> vec;
        vec.reserve(kCount);
        for (int i = 0; i < kCount; ++i) vec.emplace_back(kCount - i);
        NoexceptType::reset();
        std::sort(vec.begin(), vec.end());
        std::cout << "noexcept sort:  拷贝=" << NoexceptType::copy_count
                  << " 移动=" << NoexceptType::move_count << "\n";
    }

    // Test 2: vector 扩容（无 reserve）
    {
        NoexceptType::reset();
        std::vector<NoexceptType> vec;
        for (int i = 0; i < 200; ++i) vec.emplace_back(i);
        std::cout << "noexcept 扩容:  拷贝=" << NoexceptType::copy_count
                  << " 移动=" << NoexceptType::move_count << "\n";
    }

    // Test 3: vector 扩容（非 noexcept 类型）
    // ThrowingType 的扩容会退回拷贝，因为 move_if_noexcept 不选中它的移动
    // ...
}
```

Compile and run (g++ 15.2, -std=c++17 -O2, x86_64):

```text
noexcept sort:  拷贝=0 移动=23516
非noexcept sort: 拷贝=0 移动=23516

noexcept 扩容:  拷贝=0 移动=255
非noexcept扩容: 拷贝=255 移动=0
```

The data is very clear. `std::sort` uses moves in both cases (23,516 times), completely ignoring `noexcept`. But `std::vector` reallocation is a completely different story: `noexcept` types use moves during reallocation (255 moves), while non-`noexcept` types fall back entirely to copies (255 copies). If you frequently `push_back` into a `vector` without reserving space in advance, moves without `noexcept` will turn every reallocation into a full copy — this is where `noexcept` truly impacts performance.

The correct way to write a custom swap requires attention to ADL (Argument-Dependent Lookup). The standard practice is to provide a non-member `swap` function in the class's namespace, and then let users call it via `using std::swap; swap(a, b);`. This way, ADL will preferentially find your custom version, falling back to `std::swap` if not found.

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

Here we use the copy-and-swap idiom to implement the assignment operator, and `std::swap` to provide efficient swapping. `std::swap` itself only exchanges two pointers and two integers — the cost is negligible.

## Performance Comparison — Copy vs. Move Benchmark

We have discussed a lot of theory, but numbers are the most persuasive. Let us do a benchmark comparing the actual time cost of copying versus moving. This time we isolate the construction overhead separately, so you can see exactly how fast a pure move operation is.

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

Compile and run:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -o move_bench move_benchmark.cpp
./move_bench
```

Output on the author's machine (g++ 15.2, -O2, x86_64 WSL2):

```text
数据大小: 7812 KB
迭代次数: 100

仅构造（baseline）: 95.6 ms
构造 + 拷贝:        1404 ms
构造 + 移动:        94.8 ms

=== 分离后的实际耗时 ===
纯拷贝: 1308 ms
纯移动: -0.8 ms
```

This result is much more persuasive than simply reporting a "speedup ratio." Let us look at it line by line: constructing a `vector<int>` (allocating 8MB of memory and filling it with data) took about 96ms, which is the common baseline overhead for both test groups. After adding a copy, the total time soared to 1,404ms — the pure copy portion accounted for 1,308ms, because it needed to allocate new memory and copy the 8MB of data byte by byte. After adding a move, the total time was 94.8ms — even slightly less than pure construction by less than 1ms (measurement noise), indicating that the overhead of the move operation itself is virtually unmeasurable at this data scale.

> 💡 **Note on measurement noise**: You might see the "pure move" time show a negative value (such as -0.8 ms). This is completely normal. High-precision timers capture minute differences in system scheduling, cache state, and so on, causing the total time of "construction + move" to occasionally be slightly less than the construction time alone. This precisely demonstrates that the overhead of the move operation is extremely small, having been drowned out by measurement noise.

What does the move operation do? It simply copies three pointer-sized fields inside the `vector` (the pointer to the heap buffer, the size, and the capacity), and then nullifies the source object's pointers. The entire operation is only a few CPU instructions (at the nanosecond level), completely negligible compared to the 96ms construction time. This is why isolating the construction is important — if we did not isolate it, the "move time" you would see is actually 95ms of construction plus a few nanoseconds of moving, compared to 285ms of construction plus copying, yielding only a 3x speedup ratio that severely underestimates the true advantage of moving.

> ⚠️ **Pitfall warning**: Do not expect performance improvements on types without move semantics. "Moving" and "copying" are equivalent for `std::array` — because `std::array`'s data is stored directly inside the object, there are no pointers to transfer. Move semantics only provides real benefits for types that manage indirect resources (dynamic memory, file handles, etc.).

## Best Practices for Move Semantics in Custom Types

When applying the move semantics knowledge you have learned to your own classes, here are several battle-tested best practices.

For classes that manage dynamic resources (holding memory from `new`, files opened by `fopen`, or similar resource handles), you should implement the complete Rule of Five: custom destructor, copy constructor, move constructor, copy assignment operator, and move assignment operator. In the move constructor and move assignment operator, you must nullify the source object's resource pointers to ensure that the source object's destructor will not release the transferred resources. As long as the move operation is guaranteed not to throw exceptions, you should mark it `noexcept` (in the vast majority of cases, move operations are just pointer copies and will not throw exceptions).

For classes that only hold fundamental types and standard library containers, you can usually use `= default` to let the compiler generate move operations. Standard library components like `std::vector`, `std::string`, and `std::unique_ptr` all have efficient move semantics. The compiler-generated move constructor will call each member's move constructor in order of declaration (for class members) or copy directly (for scalar members). This aligns with the C++ standard's specifications (see C++17 [class.copy.ctor]).

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

For classes that wrap exclusive resources (file handles, network connections, locks), you should **disable copying and enable moving**. Copying makes no sense — you cannot "duplicate" a TCP connection or a mutex. But moving is reasonable — you can transfer control of the connection from one object to another.

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

## Embedded Practical Application — Moving Resource Handles

Although this tutorial series focuses primarily on general C++, move semantics also has very practical application scenarios in embedded development. On resource-constrained embedded systems, avoiding unnecessary copies not only improves performance but sometimes is even a guarantee of functional correctness — for example, the ownership of a DMA buffer must be unique, and peripheral access rights must not be shared.

Below is a simplified yet realistic DMA buffer management class, demonstrating how move semantics ensures the uniqueness of resource ownership:

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

Runtime output:

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

Note that only one 1,024-byte buffer is allocated throughout the entire lifecycle — from creation inside `createDmaBuffer`, to `buf` in `main` (via NRVO or move), to `target` (via move construction), there is always only one buffer in circulation. There are no redundant memory allocations, no data copies, and absolutely no situation where two objects simultaneously operate on the same DMA buffer — because copying is forbidden by `= delete`.

## Exercise — Implementing a Move-Supporting Dynamic Array

Reading theory is never as effective as writing code yourself. This exercise requires you to implement a simplified dynamic array class that supports both copy semantics and move semantics. This class does not need to be as complex as `std::vector`, but it must correctly handle resource management.

The requirements are as follows: class name `DynamicArray`, internally storing data in a `T*` array allocated with `new[]`. Support `push_back` to add elements, with reallocation as needed (you can simply grow by a factor of two). Implement the complete Rule of Five. Mark move operations as `noexcept`. Implement `size` and `operator[]`. Write a test snippet to verify copy and move behavior.

Below is the reference implementation skeleton:

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

If you get stuck, you can refer to the earlier `Buffer` class implementation — the logic is almost exactly the same. The key points are: `delete[]` in the destructor, transfer pointers and nullify the source object's pointers in the move constructor, allocate new memory and copy data in the copy constructor, and `delete[]` the current data before taking over the new data in the move assignment operator.

Complete reference implementation:

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

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o simple_vec simple_vector_solution.cpp
./simple_vec
```

Expected output:

```text
a: 0 1 4 9 16 25 36 49 64 81
  a.size()=10, a.capacity()=16

b (拷贝构造): 0 1 4 9 16 25 36 49 64 81

c (移动构造): 0 1 4 9 16 25 36 49 64 81
  a 移动后: size=0, capacity=0

a 重新赋值后: 999
```

After copy construction, `copied` owns an independent copy of the data, and modifying `copied` does not affect `arr`. After move construction, `moved` takes over all data from `arr`, and `arr` enters an empty state (size=0, capacity=0). Afterwards, `arr` can regain a valid object through move assignment, proving that a moved-from object is indeed in a "valid but unspecified" state — it can be safely assigned a new value or destructed, but you should not rely on its current value.

## Summary

In this article, we pushed move semantics from theory into practice. STL containers (especially `std::vector`'s `push_back`, `std::sort`, and reallocation) are the most direct beneficiaries of move semantics. The `swap` idiom leverages three move operations to achieve O(1) swapping, serving as the core of sorting, data structure reorganization, and other scenarios. Performance tests show that for types managing large blocks of dynamic memory, the overhead of the move operation itself is virtually zero — copying requires byte-by-byte replication of all data, while moving only transfers pointers. Additionally, we verified an important detail: the `noexcept` qualifier has no effect on `std::sort`, but is crucial for `std::vector` reallocation — moves without `noexcept` cause reallocation to fall back to copying.

In custom types, the key is to identify what resources your class manages: exclusive resources (file handles, peripherals, DMA buffers) should forbid copying and allow moving; shared resources can be managed with smart pointers; simple value types are fine letting the compiler auto-generate everything. Remember to mark move operations as `noexcept` — this is not just a promise, but a critical condition for `std::vector` to choose moving over copying during reallocation. The `DynamicArray` in the exercise covers all the points of the Rule of Five — if you can complete it independently, it shows you have truly mastered the core mechanisms of move semantics.

With this, the chapter on move semantics is fully covered. From the binding rules of rvalue references to the implementation of move constructors, from compiler optimizations like RVO/NRVO to the type deduction chain of perfect forwarding, and finally to real-world performance comparisons and best practices — we hope this content helps you, when you encounter `std::move` in the future, to no longer just "copy and paste it," but to clearly know what it is doing and why it does it that way.
