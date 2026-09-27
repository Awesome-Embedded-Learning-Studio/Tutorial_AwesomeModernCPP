---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: Practical applications of move semantics in the standard library and
  custom types, with performance comparisons
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'Chapter 0: Move Construction and Move Assignment'
- "Chapter 0: RVO and NRVO: The Compiler's Return Value Optimization"
reading_time_minutes: 23
related:
- 'Perfect Forwarding: Preserving Value Categories Exactly'
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: 'Move Semantics in Practice: From STL to Custom Types'
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/05-move-in-practice.md
  source_hash: c60e7c2173c7d06c0cc03a19f9ac8d78f0d5dc9b953d2cce1b67da5d61fffeab
  translated_at: '2026-09-25T14:32:14+00:00'
  engine: anthropic
  token_count: 12500
---
# Move Semantics in Practice: From STL to Custom Types

That's enough theory—this article brings move semantics down into real code. We want to see two things clearly: how much faster moving actually is than copying, and how to write code with STL containers and with custom types so that each reaps the benefit. There is a lot of code and measured data ahead; we suggest you type it all in yourself and feel the gap between copying and moving first-hand.

## Moving in STL Containers—Benefits Everywhere

Standard library containers are among the biggest beneficiaries of move semantics. Since C++11, every standard library container implements move construction and move assignment, which means passing containers around no longer requires element-by-element copies.

Take `std::vector`'s `push_back` first. It has two overloads: one taking `const T&` (copy), one taking `T&&` (move). Pass an lvalue and the copy version is called; pass an rvalue and the move version is.

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

The effect of the three approaches is clear at a glance. `push_back(h1)` triggers a copy—all 10000 of `h1`'s `int`s get replicated. `push_back(std::move(h2))` triggers a move—only the `vector`'s internal pointers are transferred, and `h2`'s `data_` becomes empty. `emplace_back("Gamma", 10000)` skips even the move—it constructs the `Heavy` object directly in the vector's storage.

The performance ranking of the three is: `emplace_back` > `push_back(std::move(...))` > `push_back(lvalue)`. In day-to-day coding: if you have an existing object to put into a container, move it in with `std::move`; if you have constructor arguments, use `emplace_back` and construct in place.

## The swap Idiom—A Classic Application of Move Semantics

Since C++11, `std::swap` has been reimplemented on top of move semantics. The core logic exchanges the contents of two objects through three moves:

```cpp
// A simplified implementation of std::swap (post-C++11)
template<typename T>
void swap(T& a, T& b) noexcept(
    std::is_nothrow_move_constructible_v<T> &&
    std::is_nothrow_move_assignable_v<T>)
{
    T temp = std::move(a);   // move-construct temp
    a = std::move(b);        // move assignment
    b = std::move(temp);     // move assignment
}
```

Three move operations complete the exchange of two objects. For classes that manage resources indirectly through pointers (memory from `new` held internally, file descriptors, and the like), each move is just a pointer transfer, so the whole swap costs O(1)—independent of how much resource the object manages. But note the precondition: this conclusion relies on the resources being held indirectly. If your object stores its data directly inside itself the way `std::array<int, 1000>` does (no layer of indirection), then moving and copying are equivalent—swap is still O(n). By comparison, C++03's swap needs one copy construction plus two copy assignments for types with indirectly-held resources, at a cost of O(n).

In sorting algorithms, swap is one of the most frequent operations. `std::sort` calls swap heavily internally to shuffle elements into position, and efficient move operations drop the cost of each element adjustment during the sort from O(n) to O(1). One thing deserves a special note: `noexcept` has no direct effect on `std::sort` itself—sort uses `std::move` and `std::swap` directly and doesn't care whether the move operations are `noexcept` (the type merely has to satisfy the move-constructible and move-assignable requirements). The scenario where `noexcept` really comes into play is `std::vector` reallocation: when a vector needs to move its old elements to new memory, it chooses its strategy through `std::move_if_noexcept`—if the move operations are `noexcept`, it moves; otherwise it falls back to copying, to preserve the strong exception-safety guarantee. Let's prove this with the verification program below:

```cpp
// noexcept_sort_vs_realloc_verify.cpp -- verify how noexcept affects sort and vector reallocation
// Full compilable version: code/examples/vol2/noexcept_sort_vs_realloc.cpp

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

// ThrowingType is identical to NoexceptType, except its move operations lack noexcept
struct ThrowingType
{
    std::string payload;
    int value;

    static int copy_count;
    static int move_count;

    ThrowingType(int v) : payload("data"), value(v) {}
    ThrowingType(const ThrowingType& o)
        : payload(o.payload + "_c"), value(o.value) { ++copy_count; }
    ThrowingType(ThrowingType&& o) // note: no noexcept
        : payload(std::move(o.payload)), value(o.value)
    {
        o.payload = "(moved)";
        ++move_count;
    }
    ThrowingType& operator=(ThrowingType&& o) // note: no noexcept
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

    // Test 1: std::sort (noexcept type)
    {
        std::vector<NoexceptType> vec;
        vec.reserve(kCount);
        for (int i = 0; i < kCount; ++i) vec.emplace_back(kCount - i);
        NoexceptType::reset();
        std::sort(vec.begin(), vec.end());
        std::cout << "noexcept sort:  拷贝=" << NoexceptType::copy_count
                  << " 移动=" << NoexceptType::move_count << "\n";
    }

    // Test 2: std::sort (non-noexcept type)
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

    // Test 3: vector reallocation (noexcept type, no reserve)
    {
        NoexceptType::reset();
        std::vector<NoexceptType> vec;
        for (int i = 0; i < 200; ++i) vec.emplace_back(i);
        std::cout << "noexcept 扩容:  拷贝=" << NoexceptType::copy_count
                  << " 移动=" << NoexceptType::move_count << "\n";
    }

    // Test 4: vector reallocation (non-noexcept type, no reserve)
    // ThrowingType's reallocation falls back to copying, because move_if_noexcept doesn't select its moves
    {
        ThrowingType::reset();
        std::vector<ThrowingType> vec;
        for (int i = 0; i < 200; ++i) vec.emplace_back(i);
        std::cout << "非noexcept扩容: 拷贝=" << ThrowingType::copy_count
                  << " 移动=" << ThrowingType::move_count << "\n";
    }
}
```

Compile and run (GCC 16.1.1, -std=c++17 -O2, x86_64):

```text
noexcept sort:  拷贝=0 移动=23516
非noexcept sort: 拷贝=0 移动=23516

noexcept 扩容:  拷贝=0 移动=255
非noexcept扩容: 拷贝=255 移动=0
```

The data speaks plainly. `std::sort` uses only moves (23516 of them) in both cases and doesn't distinguish `noexcept` at all. Vector reallocation is a completely different story: the `noexcept` type moves during reallocation (255 moves), while the non-`noexcept` type falls back entirely to copying during reallocation (255 copies). If you `push_back` into a `vector` frequently without reserving capacity up front, moves without `noexcept` turn every reallocation into a full copy—this is where `noexcept` genuinely affects performance.

Writing a correct custom swap requires attention to ADL (Argument-Dependent Lookup). The standard approach is to provide a non-member `swap` function in the class's namespace, then have users call it the `using std::swap; swap(a, b);` way. That way ADL finds your swap first, and falls back to `std::swap` when it can't find one.

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

Here we used the copy-and-swap idiom to implement the assignment operator, and `friend swap` to provide efficient exchange. The `swap` itself merely exchanges two pointers and two integers—a negligible cost.

## Performance Comparison—A Copy vs Move Benchmark

We've talked a lot of theory; numbers are the most persuasive thing. Let's build a benchmark comparing the actual time cost of copying versus moving. This time we separate the construction cost out on its own, so you can see just how fast a pure move operation really is.

```cpp
// move_benchmark.cpp -- copy vs move performance comparison (construction cost separated out)
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

/// @brief Helper template for measuring a function's execution time
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
    constexpr std::size_t kDataSize = 1000000;   // 1 million doubles, about 8 MB
    constexpr int kIterations = 100;

    std::cout << "数据大小: " << kDataSize * sizeof(double) / 1024
              << " KB\n";
    std::cout << "迭代次数: " << kIterations << "\n\n";

    // Test 0: construction only (baseline)
    auto construct_time = measure_ms([&]() {
        BigData source(kDataSize);
        (void)source;
    }, kIterations);

    std::cout << "仅构造（baseline）: " << construct_time << " ms\n";

    // Test 1: construct + copy
    auto copy_time = measure_ms([&]() {
        BigData source(kDataSize);
        BigData copy = source;  // copy construction
        (void)copy;
    }, kIterations);

    std::cout << "构造 + 拷贝:        " << copy_time << " ms\n";

    // Test 2: construct + move
    auto move_time = measure_ms([&]() {
        BigData source(kDataSize);
        BigData moved = std::move(source);  // move construction
        (void)moved;
    }, kIterations);

    std::cout << "构造 + 移动:        " << move_time << " ms\n\n";

    // Isolate the pure copy/move cost
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

On my machine (GCC 16.1.1, -O2, x86_64 WSL2, one stable run taken), the output:

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

This result is more persuasive than reporting a single "speedup ratio". Let's go through it line by line: constructing a `BigData` (allocating about 8MB of memory and filling it with data) took 47ms—a fixed overhead shared by both test groups. Add a copy, and total time shoots up to 505ms—458ms of it pure copying, because a separate block of memory has to be allocated and the 8MB of data replicated byte by byte. Add a move, and total time is 45ms, nearly indistinguishable from construction alone—showing that at this data scale, the move operation itself simply doesn't register.

> 💡 **A note on measurement noise**: the "pure move" time will jitter around zero—this run it was -2.7 ms; at another moment it might be a small positive single-digit value. Both are normal. A high-resolution timer picks up tiny differences such as system scheduling and cache state, and the cost of the move itself is far smaller than those differences, so it drowns in the noise. What matters is that it isn't even in the same order of magnitude as the hundreds of milliseconds of the pure copy.

What does the move operation actually do? It copies the handful of pointer-sized fields inside `std::vector` (the pointer to the heap buffer, the size, the capacity), then nulls out the source object's pointer—a few CPU instructions in total, on the order of nanoseconds, negligible next to the 47ms construction. This is exactly why we separate construction out: without the separation, the "move time" you see is really 47ms of construction plus a few nanoseconds of moving; set against 505ms of construction-plus-copy, you would only conclude "roughly ten times faster"—a number diluted by the construction cost, which actually ends up hiding the fact that moving is nearly free.

Don't expect a performance gain on types without move semantics. For `std::array<int, 1000>`, "moving" and "copying" are equivalent—`std::array` stores its data directly inside the object, with no pointers to transfer. Move semantics only yields real gains on types that manage indirect resources (dynamic memory, file handles, and so on).

## Move Best Practices for Custom Types

To apply the move semantics you've learned to your own classes, here are several battle-tested best practices.

For classes that manage dynamic resources (memory obtained with `new`, files opened with `fopen`, or similar resource handles), you should implement the full Rule of Five: a custom destructor, copy constructor, move constructor, copy assignment, and move assignment. The move constructor and move assignment must null out the source object's resource pointer, ensuring the source object won't release the already-transferred resources when it is destroyed. Whenever the move operations are guaranteed not to throw, mark them `noexcept` (in the vast majority of cases a move is just pointer copying and won't throw).

What does each of these five members take care of? Let's use the SimpleVector from the exercise at the end of this article as an example, drawn as a diagram:

![SimpleVector's Rule of Five: the responsibilities of the five special members, contrasted with = default](./05-move-in-practice-rulefive.drawio)

For classes that hold only fundamental types and standard library containers, you can usually let the compiler generate the move operations with `= default`. Standard library components like `std::string`, `std::vector`, and `std::map` all have efficient move semantics; the compiler-generated move constructor invokes each member's move constructor in member-declaration order (for class members) or copies directly (for scalar members). This follows what the C++ standard specifies (see C++17 [class.copy.ctor]).

```cpp
struct UserProfile
{
    std::string name;
    std::string email;
    std::vector<std::string> permissions;
    int level = 0;

    // The compiler-generated move operations are good enough
    // because std::string and std::vector both have noexcept moves
    ~UserProfile() = default;
    UserProfile(const UserProfile&) = default;
    UserProfile(UserProfile&&) noexcept = default;
    UserProfile& operator=(const UserProfile&) = default;
    UserProfile& operator=(UserProfile&&) noexcept = default;
};
```

For classes that wrap exclusively-owned resources (file handles, network connections, locks), you should **disable copying and enable moving**. Copying is meaningless—you can't "copy" a TCP connection or a mutex. But moving is reasonable—you can transfer control of the connection from one object to another.

```cpp
class NetworkConnection
{
    int socket_fd_;

public:
    explicit NetworkConnection(const char* host, int port);
    ~NetworkConnection() { if (socket_fd_ >= 0) close_socket(socket_fd_); }

    // Forbid copying
    NetworkConnection(const NetworkConnection&) = delete;
    NetworkConnection& operator=(const NetworkConnection&) = delete;

    // Allow moving
    NetworkConnection(NetworkConnection&& other) noexcept
        : socket_fd_(other.socket_fd_)
    {
        other.socket_fd_ = -1;  // mark as transferred
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

## Practical Embedded Applications—Moving Resource Handles

Although this tutorial series focuses on general-purpose C++, move semantics also has very practical application scenarios in embedded development. On resource-constrained embedded systems, avoiding unnecessary copies doesn't just improve performance—sometimes it is a guarantee of functional correctness. For example, the ownership of a DMA buffer must be unique, and access rights to a peripheral cannot be shared.

Below is a simplified but realistic DMA buffer management class, showing how move semantics ensures uniqueness of resource ownership:

```cpp
#include <cstddef>
#include <cstring>
#include <utility>
#include <iostream>

/// @brief Simulated DMA buffer management
/// In a real embedded project, allocate_dma_buffer and free_dma_buffer
/// would hook into the actual memory management unit or a memory pool
class DMABuffer
{
    void* buffer_;       // points to the DMA buffer
    std::size_t size_;   // buffer size

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

    // Forbid copying: a DMA buffer cannot have two copies
    DMABuffer(const DMABuffer&) = delete;
    DMABuffer& operator=(const DMABuffer&) = delete;

    // Allow moving: ownership can be transferred
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

/// @brief Simulate receiving data from DMA
DMABuffer receive_dma(std::size_t expected_size)
{
    DMABuffer buf(expected_size);
    // In a real system, this would trigger a DMA transfer and wait for completion
    // The memory pointed to by buf.data() is written directly by the DMA controller
    char msg[] = "DMA data received";
    std::memcpy(buf.data(), msg, sizeof(msg));
    return buf;  // NRVO or move semantics ensures a zero-copy return
}

int main()
{
    std::cout << "=== 嵌入式 DMA 缓冲区管理 ===\n\n";

    // Receive data from DMA—buffer ownership transfers from the function to main
    auto rx_buf = receive_dma(1024);
    std::cout << "  接收到: " << static_cast<const char*>(rx_buf.data()) << "\n\n";

    // Transfer the buffer to the processing queue (simulated)
    std::cout << "=== 转移到处理队列 ===\n";
    DMABuffer process_buf = std::move(rx_buf);
    std::cout << "  rx_buf 大小: " << rx_buf.size() << "\n";
    std::cout << "  process_buf 大小: " << process_buf.size() << "\n\n";

    std::cout << "=== 程序结束，资源自动释放 ===\n";
    return 0;
}
```

Program output:

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

Notice that across the entire lifetime, the 1024-byte buffer is allocated exactly once—created inside `receive_dma`, then handed to `rx_buf` in `main` (via NRVO or a move), then to `process_buf` (via move construction); a single buffer circulates the whole time. No redundant memory allocations, no data copies, and never a situation where two objects operate on the same DMA buffer simultaneously—because copying is forbidden with `= delete`.

## Exercise—Implement a Dynamic Array That Supports Moving

No amount of reading theory beats writing the code once yourself. This exercise asks you to implement a simplified dynamic array class that supports both copy semantics and move semantics. The class doesn't need to be as elaborate as `std::vector`, but it does need to handle resource management correctly.

Requirements: name the class `SimpleVector`, storing data in an `int` array allocated with `new[]`. Support `push_back(int)` to add elements, growing capacity when necessary (simply doubling is fine). Implement the full Rule of Five. Mark the move operations `noexcept`. Implement `size()` and `operator[]`. Write a stretch of test code that verifies copy and move behavior.

Here is a reference skeleton:

```cpp
// simple_vector.cpp -- exercise: a dynamic array that supports moving
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

    // TODO: implement the destructor
    // TODO: implement the copy constructor (deep copy)
    // TODO: implement the move constructor (pointer transfer + null out the source)
    // TODO: implement the copy assignment operator
    // TODO: implement the move assignment operator

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
    // Test code
    SimpleVector a;
    for (int i = 0; i < 10; ++i) {
        a.push_back(i * i);
    }

    std::cout << "a: ";
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::cout << a[i] << " ";
    }
    std::cout << "\n";

    // Test copy construction
    SimpleVector b = a;
    std::cout << "b (拷贝): ";
    for (std::size_t i = 0; i < b.size(); ++i) {
        std::cout << b[i] << " ";
    }
    std::cout << "\n";

    // Test move construction
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

If you get stuck, refer back to the `Buffer` class implementation from earlier—the logic is nearly identical. The key points: in the destructor, `delete[] data_`; in the move constructor, transfer the pointer and null out the source object's pointer; in the copy constructor, allocate new memory and replicate the data; in move assignment, `delete[]` the current data first, then take over the new data.

The complete reference implementation:

```cpp
// simple_vector_solution.cpp -- exercise reference answer
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

    // Copy constructor: deep copy
    SimpleVector(const SimpleVector& other)
        : data_(other.capacity_ > 0 ? new int[other.capacity_] : nullptr)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        if (data_) {
            std::copy(other.data_, other.data_ + other.size_, data_);
        }
    }

    // Move constructor: pointer transfer
    SimpleVector(SimpleVector&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // Copy assignment
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

    // Move assignment
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

    SimpleVector b = a;   // copy construction
    std::cout << "b (拷贝构造): ";
    for (std::size_t i = 0; i < b.size(); ++i) {
        std::cout << b[i] << " ";
    }
    std::cout << "\n\n";

    SimpleVector c = std::move(a);  // move construction
    std::cout << "c (移动构造): ";
    for (std::size_t i = 0; i < c.size(); ++i) {
        std::cout << c[i] << " ";
    }
    std::cout << "\n";
    std::cout << "  a 移动后: size=" << a.size()
              << ", capacity=" << a.capacity() << "\n\n";

    // Verify that the moved-from a can be used safely
    a = SimpleVector(5);  // move-assign a new object
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

After copy construction, `b` owns an independent copy of the data; modifying `b` doesn't affect `a`. After move construction, `c` has taken over all of `a`'s data, and `a` is left in an empty state (size=0, capacity=0). Afterwards, `a` can regain a valid object through move assignment—proof that a moved-from object really is in a "valid but unspecified" state: it can safely be assigned a new value and destroyed, but you should not rely on its current value.

## Run It Online

Run the two examples online and verify this article's key conclusions first-hand:

<OnlineCompilerDemo allow-run
  title="push_back vs emplace_back: Copy, Move, and In-Place Construction"
  source-path="code/examples/vol2/push_back_emplace.cpp"
  description="Trace the construction, copy, move, and destruction logs of Heavy objects, and compare the cost of push_back(lvalue), push_back(rvalue), and emplace_back."
/>

<OnlineCompilerDemo allow-run
  title="How noexcept Affects sort and vector Reallocation"
  source-path="code/examples/vol2/noexcept_sort_vs_realloc.cpp"
  description="Count copies and moves: std::sort doesn't distinguish noexcept, but vector reallocation falls back to copying non-noexcept types via move_if_noexcept."
/>

And with that, the chapter on move semantics is complete. From the binding rules of rvalue references, to implementing move construction, on to RVO/NRVO and perfect forwarding, and finally down to this article's hands-on measurements—the hope is that from now on, when you see `std::move`, you won't just copy it mechanically, but will know clearly what it is doing and why.

Following the thread of resource ownership, the next chapter takes up smart pointers: RAII will turn all those manual `delete`s and ownership transfers from this chapter into things the compiler manages automatically.
