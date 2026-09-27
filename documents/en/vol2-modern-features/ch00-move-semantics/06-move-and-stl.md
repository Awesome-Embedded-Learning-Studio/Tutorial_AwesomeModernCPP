---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: The gains move semantics brings to standard library containers, the swap
  idiom, and measured copy-versus-move performance
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'Chapter 0: Move Construction and Move Assignment'
- 'Chapter 0: RVO and NRVO: The Compiler''s Return Value Optimization'
reading_time_minutes: 12
related:
- 'Perfect Forwarding: Keeping Value Categories Intact'
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: 'Move Semantics in Practice: Standard Library Containers and Performance Benchmarks'
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/06-move-and-stl.md
  source_hash: f11f44ba7094361275955b1cdd0b0f1b19121b2abd4dacaaf87d46473eff4c8f
  translated_at: '2026-09-27T04:42:54+00:00'
  engine: anthropic
  token_count: 7000
---
# Move Semantics in Practice: From the STL to Your Own Types

That's enough theory on the shelf—this article looks at the standard library side first: how STL containers reap the gains of moves, why the swap idiom is fast, and just how much faster moving is than copying. There's a fair amount of code and measured data ahead, so we suggest typing the examples along and feeling the copy-versus-move gap first-hand.

## Moves in STL Containers: Benefits Everywhere

Standard library containers are among the biggest beneficiaries of move semantics. Since C++11, every standard library container implements move construction and move assignment, which means handing a container around no longer requires element-by-element copying.

Start with `std::vector`'s `push_back`. It has two overloads: one taking `const T&` (copy) and one taking `T&&` (move)<RefLink :id="1" preview="cppreference std::vector — push_back overloads and emplace_back" />. Pass an lvalue and the copy version is chosen; pass an rvalue and the move version is.

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

The demo program is right below—click "Try It Yourself" to run it directly:

<OnlineCompilerDemo allow-run
  title="push_back vs emplace_back: Copy, Move, and In-Place Construction"
  source-path="code/examples/vol2/push_back_emplace.cpp"
  description="Trace Heavy objects' construction, copy, move, and destruction logs, and compare the cost of push_back(lvalue), push_back(rvalue), and emplace_back."
  run-options="-O2 -std=c++17"
/>

The effect of the three approaches is plain to see. `push_back(h1)` triggers a copy—all 10,000 `int`s in `h1` get duplicated in full. `push_back(std::move(h2))` triggers a move—only the `vector`'s internal pointer changes hands, leaving `h2`'s `data_` empty. `emplace_back("Gamma", 10000)` skips even the move—it constructs the `Heavy` object directly in the vector's storage.

The performance ranking of the three is: `emplace_back` > `push_back(std::move(...))` > `push_back(lvalue)`. In day-to-day coding, if you have a ready-made object to put into a container, move it in with `std::move`; if you have constructor arguments, use `emplace_back` and construct in place.

## The swap Idiom: A Classic Application of Move Semantics

Since C++11, `std::swap` has been reimplemented on top of move semantics<RefLink :id="2" preview="cppreference std::swap — three moves, conditionally noexcept since C++11" />. The core logic exchanges the contents of two objects through three moves:

```cpp
// Simplified implementation of std::swap (since C++11)
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

Three move operations swap the two objects. For classes that manage resources indirectly through pointers (heap memory from `new`, file descriptors, and so on), each move is just a pointer transfer, so the whole swap costs O(1)—independent of how much resource the objects manage. But mind the precondition: this conclusion relies on the resources being held indirectly. If your object stores its data directly inside itself, the way `std::array<int, 1000>` does (no indirection), then moving and copying are equivalent—swap is still O(n). Compare that with C++03's swap, which for types with indirectly held resources needs one copy construction plus two copy assignments, at a cost of O(n).

In sorting algorithms, swap is one of the most frequent operations. `std::sort` calls swap heavily internally to shuffle elements into position, and efficient moves drive the cost of each such adjustment during the sort from O(n) down to O(1). One point deserves special mention: `noexcept` has no direct effect on `std::sort` itself—sort uses `std::move` and `std::swap` directly and does not care whether the move operations are `noexcept` (it only requires the type to be move-constructible and move-assignable). The scenario where `noexcept` truly matters is `std::vector` reallocation: when a vector has to relocate its old elements into new memory, it picks its strategy through `std::move_if_noexcept`—if the move operations are `noexcept`, it moves; otherwise it falls back to copying, to preserve the strong exception guarantee<RefLink :id="3" preview="cppreference std::move_if_noexcept — move unless the move ctor may throw" />. Let's prove it with the verification program below:

```cpp
// noexcept_sort_vs_realloc_verify.cpp -- verify how noexcept affects sort and vector reallocation
// The fully compilable version is at code/examples/vol2/noexcept_sort_vs_realloc.cpp

#include <iostream>
#include <vector>
#include <algorithm>
#include <string>
#include <cstring>

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

// ThrowingType is identical to NoexceptType; the only difference is that its move operations lack noexcept
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
    // ThrowingType's reallocation falls back to copying, because move_if_noexcept does not select its move
    {
        ThrowingType::reset();
        std::vector<ThrowingType> vec;
        for (int i = 0; i < 200; ++i) vec.emplace_back(i);
        std::cout << "非noexcept扩容: 拷贝=" << ThrowingType::copy_count
                  << " 移动=" << ThrowingType::move_count << "\n";
    }
}
```

The verification program is right below—click "Try It Yourself" to run it directly (the fully compilable version is exactly `code/examples/vol2/noexcept_sort_vs_realloc.cpp`):

<OnlineCompilerDemo allow-run
  title="How noexcept Affects sort vs vector Reallocation"
  source-path="code/examples/vol2/noexcept_sort_vs_realloc.cpp"
  description="Counts copies and moves: std::sort does not distinguish noexcept, but during reallocation vector falls back to copying non-noexcept types via move_if_noexcept."
  run-options="-O2 -std=c++17"
/>

The data could not be clearer. `std::sort` uses only moves in both cases (23,516 of them on GCC 16—the exact count varies with the standard library implementation), completely ignoring `noexcept`. Vector reallocation is another story entirely: the `noexcept` type moves during reallocation (255 moves), while the non-`noexcept` type falls back entirely to copying during reallocation (255 copies). If you `push_back` into a `vector` frequently without `reserve`-ing ahead of time, moves lacking `noexcept` turn every reallocation into a full copy—this is where `noexcept` genuinely affects performance.

Writing a custom swap correctly calls for attention to ADL (Argument-Dependent Lookup). The standard approach is to provide a non-member `swap` function in the class's namespace, then have users call it the `using std::swap; swap(a, b);` way. ADL then finds your swap first, and falls back to `std::swap` when it finds nothing<RefLink :id="4" preview="cppreference Swappable named requirement — ADL two-step for customizing swap" />.

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

Here we used the copy-and-swap idiom to implement the assignment operator, and a `friend swap` to provide the efficient exchange. The `swap` itself does nothing more than exchange two pointers and two integers—a negligible cost.

## Performance Comparison: A Copy-vs-Move Benchmark

We've laid on plenty of theory; numbers persuade best. Let's build a benchmark comparing the actual time cost of copying versus moving. This time we separate the construction overhead out on its own, so you can see just how fast a pure move operation really is.

```cpp
// move_benchmark.cpp -- copy vs move performance comparison (construction overhead separated out)
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

/// @brief Helper template that measures a function's execution time
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
    constexpr std::size_t kDataSize = 1000000;   // 1 million doubles, about 8MB
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

The benchmark is right below—click "Try It Yourself" to run it. The absolute numbers on your machine will differ from the author's; the order-of-magnitude relationship is what matters:

<OnlineCompilerDemo allow-run
  title="Measure It Yourself: move_benchmark.cpp"
  source-path="code/examples/vol2/move_benchmark.cpp"
  description="Measure copy vs move timing online (construction overhead separated out). One stable run by the author: pure copy 458.1 ms, pure move within noise (about -2.7 ms)."
  run-options="-O2 -std=c++17"
/>

This result is more convincing than quoting a single "speedup" number. Take one stable run from the author (GCC 16.1.1, -O2, x86_64 WSL2) and read it line by line: constructing a `BigData` (allocating about 8 MB of memory and filling it with data) took 47 ms—the fixed overhead shared by both test groups. Add a copy, and total time shoots up to 505 ms—458 ms of it pure copying, because a separate block of memory has to be allocated and the 8 MB of data copied over byte by byte. Add a move, and total time is 45 ms, virtually indistinguishable from pure construction—the move operation itself simply doesn't register at this data scale.

> 💡 **A note on measurement noise**: the "pure move" time jitters around zero—this run it was -2.7 ms; at another moment it might be a small single-digit positive value. Both are normal. A high-resolution timer picks up tiny variations from system scheduling and cache state, and the move's own overhead is far smaller than those variations, so it drowns in the noise. What matters is that it isn't even in the same order of magnitude as those hundreds of milliseconds of pure copying.

What does a move operation actually do? It copies the handful of pointer-sized fields inside `std::vector` (the pointer to the heap buffer, the size, the capacity), then nulls out the source object's pointer—a few CPU instructions in total, on the order of nanoseconds, negligible next to the 47 ms construction. That is exactly why we isolate construction on its own: without the separation, the "move time" you see is really 47 ms of construction plus a few nanoseconds of moving, and set against 505 ms of construct-plus-copy it would only yield a "roughly ten times faster" conclusion—a number diluted by construction that actually buries the fact that moving is nearly free.

Don't expect a performance gain from types without move semantics. "Moving" a `std::array<int, 1000>` is equivalent to copying it—`std::array` stores its data directly inside the object, with no pointer to transfer. Move semantics delivers real gains only for types that manage indirect resources (dynamic memory, file handles, and the like).

That wraps up the standard library side, with the gains measured. The next article turns to your own types: how the Rule of Five lands in practice, which classes should forbid copying and keep only moves, and how resource handles change hands safely in embedded settings.

<ReferenceCard title="References">
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
