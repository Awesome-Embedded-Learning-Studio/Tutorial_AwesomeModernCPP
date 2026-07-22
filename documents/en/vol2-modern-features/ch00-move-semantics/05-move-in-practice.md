---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: Practical applications and performance comparison of move semantics in
  the standard library and custom types
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
title: 'Move Semantics in Practice: From STL to Custom Types'
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/05-move-in-practice.md
  source_hash: 481e3b2203f2796f969188b454875172afe7daca708bee63f0606c7d8a2fd4a7
  translated_at: '2026-06-16T03:55:01.012477+00:00'
  engine: anthropic
  token_count: 5730
---
# Move Semantics in Practice: From STL to Custom Types

In the previous four articles, we walked through the theoretical foundations of move semantics from start to finish: value categories, rvalue references, move constructors and move assignment, RVO/NRVO, and perfect forwarding. Now it's time to put theory into practice—let's see how much performance difference move semantics actually makes in real code, and how to use it correctly with STL containers and custom types. This article includes plenty of code and real-world measurements, so we recommend following along and typing it out yourself to feel the difference between copying and moving firsthand.

## Move Semantics in STL Containers — Ubiquitous Benefits

Standard library containers are among the biggest beneficiaries of move semantics. Since C++11, all standard containers have implemented move constructors and move assignment, meaning passing containers between functions no longer requires element-by-element copying.

First, let's look at `std::vector::push_back`. It has two overloads: one accepting a `const T&` (copy), and one accepting a `T&&` (move). When you pass an lvalue, the copy version is called; when you pass an rvalue, the move version is called.

```cpp
#include <iostream>
#include <vector>
#include <string>

struct Reporter {
    std::string name;
    Reporter(std::string n) : name(std::move(n)) { std::cout << " ctor\n"; }
    Reporter(const Reporter& other) : name(other.name) { std::cout << " copy\n"; }
    Reporter(Reporter&& other) noexcept : name(std::move(other.name)) { std::cout << " move\n"; }
};

int main() {
    std::vector<Reporter> vec;
    Reporter r("obj");

    std::cout << "1. Copy:\n";
    vec.push_back(r);          // lvalue -> copy

    std::cout << "\n2. Move:\n";
    vec.push_back(std::move(r)); // rvalue (cast) -> move

    std::cout << "\n3. Emplace:\n";
    vec.emplace_back("obj");   // direct construction -> no copy/move
}
```

Compile and run:

```bash
g++ -std=c++17 main.cpp -o main && ./main
```

Output:

```text
1. Copy:
 copy

2. Move:
 move

3. Emplace:
 ctor
```

The effects of the three methods are clear at a glance. `push_back(r)` triggers a copy—all 10,000 elements of `r` are fully replicated. `push_back(std::move(r))` triggers a move—only the internal pointer of `r` is transferred, leaving `r`'s `vector` empty. `emplace_back` saves even the move—it constructs the `vector` object directly in the container's storage.

The performance ranking is: `emplace_back` > `move` > `copy`. In daily coding, if you have an existing object to put into a container, use `std::move` to move it in; if you have the constructor arguments, use `emplace_back` to construct it in-place directly.

## The swap Idiom — A Classic Application of Move Semantics

`std::swap` was reimplemented in C++11 based on move semantics. The core logic is to exchange the contents of two objects via three move operations:

```cpp
namespace std {
    template<typename T>
    void swap(T& a, T& b) noexcept(is_nothrow_move_constructible_v<T> &&
                                    is_nothrow_move_assignable_v<T>) {
        T tmp = std::move(a); // move construct
        a = std::move(tmp);   // move assign
        b = std::move(tmp);   // move assign
    }
}
```

Three move operations complete the exchange of two objects. For classes that manage resources indirectly via pointers (memory allocated by `new`, file descriptors, etc.), each move is just a pointer transfer, so the cost of the entire swap is O(1)—independent of the size of the resources the object manages. However, note the prerequisite: this conclusion relies on "resources being held indirectly." If your object stores data directly inside itself like `std::array` (no indirection), then moving and copying are equivalent—swap remains O(n). In contrast, C++03's `swap` for types holding indirect resources required one copy construction and two copy assignments, costing O(n).

In sorting algorithms, `swap` is one of the most frequent operations. `std::sort` internally calls `swap` extensively to adjust element positions; efficient move operations reduce the cost of each adjustment from O(n) to O(1). `noexcept` actually has no direct effect on `std::sort` itself—`sort` uses `std::move` and `std::swap` internally and doesn't care whether the move is `noexcept` (as long as the type is MoveConstructible and MoveAssignable). Where `noexcept` really shines is during `std::vector` reallocation: when a `vector` needs to move old elements to new memory, it uses `std::is_nothrow_move_constructible_v` to choose its strategy—if the move operation is `noexcept`, it uses move; otherwise, it falls back to copy to guarantee strong exception safety. Let's use the following verification program to prove this:

```cpp
#include <algorithm>
#include <iostream>
#include <vector>
#include <utility>

template<bool NoExceptMove>
struct Counter {
    static size_t move_count;
    static size_t copy_count;

    Counter() = default;

    // Copy
    Counter(const Counter&) { ++copy_count; }
    Counter& operator=(const Counter&) { ++copy_count; return *this; }

    // Move
    Counter(Counter&&) noexcept(NoExceptMove) { ++move_count; }
    Counter& operator=(Counter&&) noexcept(NoExceptMove) { ++move_count; return *this; }
};

template<bool NoExceptMove>
size_t Counter<NoExceptMove>::move_count = 0;

template<bool NoExceptMove>
size_t Counter<NoExceptMove>::copy_count = 0;

int main() {
    using NoExcept = Counter<true>;
    using ThrowMove = Counter<false>;

    std::vector<NoExcept> vec1(1000);
    std::vector<ThrowMove> vec2(1000);

    std::cout << "Before sort:\n";
    std::cout << "  noexcept move: moves=" << NoExcept::move_count << ", copies=" << NoExcept::copy_count << "\n";
    std::cout << "  throwing move: moves=" << ThrowMove::move_count << ", copies=" << ThrowMove::copy_count << "\n";

    NoExcept::move_count = NoExcept::copy_count = 0;
    ThrowMove::move_count = ThrowMove::copy_count = 0;

    std::sort(vec1.begin(), vec1.end());
    std::sort(vec2.begin(), vec2.end());

    std::cout << "After sort:\n";
    std::cout << "  noexcept move: moves=" << NoExcept::move_count << ", copies=" << NoExcept::copy_count << "\n";
    std::cout << "  throwing move: moves=" << ThrowMove::move_count << ", copies=" << ThrowMove::copy_count << "\n";

    NoExcept::move_count = NoExcept::copy_count = 0;
    ThrowMove::move_count = ThrowMove::copy_count = 0;

    vec1.resize(2000); // Trigger reallocation
    vec2.resize(2000); // Trigger reallocation

    std::cout << "After resize (reallocation):\n";
    std::cout << "  noexcept move: moves=" << NoExcept::move_count << ", copies=" << NoExcept::copy_count << "\n";
    std::cout << "  throwing move: moves=" << ThrowMove::move_count << ", copies=" << ThrowMove::copy_count << "\n";
}
```

Compile and run (GCC 16.1.1, -std=c++17 -O2, x86_64):

```bash
g++ -std=c++17 -O2 main.cpp -o main && ./main
```

Output:

```text
Before sort:
  noexcept move: moves=0, copies=0
  throwing move: moves=0, copies=0
After sort:
  noexcept move: moves=23516, copies=0
  throwing move: moves=23516, copies=0
After resize (reallocation):
  noexcept move: moves=255, copies=0
  throwing move: moves=0, copies=255
```

The data is very clear. `std::sort` uses moves in both cases (23,516 times), completely ignoring `noexcept`. But `std::vector` reallocation is a different story: the `noexcept` type uses moves during reallocation (255 moves), while the non-`noexcept` type falls back entirely to copies (255 copies). If you frequently `push_back` to a `vector` but haven't pre-reserved space, a non-`noexcept` move turns every reallocation into a full copy—this is where `noexcept` truly impacts performance.

The correct way to write a custom `swap` involves attention to ADL (Argument-Dependent Lookup). The standard practice is to provide a non-member `swap` function in the class's namespace, then let users call it via `using std::swap; swap(a, b);`. This way, ADL will prioritize finding your custom version, falling back to `std::swap` if not found.

```cpp
#include <algorithm> // for std::swap
#include <iostream>
#include <string>

class Buffer {
public:
    Buffer() : data_(nullptr), size_(0), capacity_(0) {}
    explicit Buffer(size_t size) : data_(new int[size]), size_(size), capacity_(size) {}

    ~Buffer() { delete[] data_; }

    // Copy constructor
    Buffer(const Buffer& other)
        : data_(new int[other.size_]), size_(other.size_), capacity_(other.capacity_) {
        std::copy(other.data_, other.data_ + size_, data_);
    }

    // Copy assignment
    Buffer& operator=(const Buffer& other) {
        if (this != &other) {
            Buffer tmp(other); // copy
            swap(tmp);         // swap
        }
        return *this;
    }

    // Move constructor
    Buffer(Buffer&& other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // Move assignment
    Buffer& operator=(Buffer&& other) noexcept {
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

    // Custom swap (non-member friend)
    friend void swap(Buffer& a, Buffer& b) noexcept {
        using std::swap;
        swap(a.data_, b.data_);
        swap(a.size_, b.size_);
        swap(a.capacity_, b.capacity_);
    }

private:
    int* data_;
    size_t size_;
    size_t capacity_;
};
```

Here we use the copy-and-swap idiom to implement the assignment operator, and a custom `swap` to provide efficient swapping. `swap` itself only exchanges two pointers and two integers—the cost is negligible.

## Performance Comparison — Copy vs. Move Benchmark

We've covered a lot of theory, but numbers are the most persuasive. Let's do a benchmark comparing the actual time taken by copying versus moving. This time, we'll separate the construction overhead so you can see just how fast a pure move operation is.

```cpp
// move_benchmark.cpp -- Copy vs Move performance comparison (isolating construction overhead)
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

/// @brief Helper template to measure a function's execution time
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
    constexpr std::size_t kDataSize = 1000000;   // 1M doubles, ~8MB
    constexpr int kIterations = 100;

    std::cout << "Data size: " << kDataSize * sizeof(double) / 1024
              << " KB\n";
    std::cout << "Iterations: " << kIterations << "\n\n";

    // Test 0: construction only (baseline)
    auto construct_time = measure_ms([&]() {
        BigData source(kDataSize);
        (void)source;
    }, kIterations);

    std::cout << "Construction only (baseline): " << construct_time << " ms\n";

    // Test 1: construction + copy
    auto copy_time = measure_ms([&]() {
        BigData source(kDataSize);
        BigData copy = source;  // copy ctor
        (void)copy;
    }, kIterations);

    std::cout << "Construction + Copy:          " << copy_time << " ms\n";

    // Test 2: construction + move
    auto move_time = measure_ms([&]() {
        BigData source(kDataSize);
        BigData moved = std::move(source);  // move ctor
        (void)moved;
    }, kIterations);

    std::cout << "Construction + Move:          " << move_time << " ms\n\n";

    // Isolate the pure copy/move cost
    double actual_copy = copy_time - construct_time;
    double actual_move = move_time - construct_time;

    std::cout << "=== Isolated actual cost ===\n";
    std::cout << "Pure copy: " << actual_copy << " ms\n";
    std::cout << "Pure move: " << actual_move << " ms\n";

    if (actual_move > 0.01) {
        std::cout << "Speedup: " << actual_copy / actual_move << "x\n";
    } else {
        std::cout << "Move cost is within measurement noise (near zero)\n";
    }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -o move_bench move_benchmark.cpp
./move_bench
```

Output on my machine (GCC 16.1.1, -O2, x86_64 WSL2, one stable run):

```text
Data size: 7812 KB
Iterations: 100

Construction only (baseline): 47.3 ms
Construction + Copy:          505.3 ms
Construction + Move:          44.6 ms

=== Isolated actual cost ===
Pure copy: 458.1 ms
Pure move: -2.7 ms
Move cost is within measurement noise (near zero)
```

This is more persuasive than just reporting a "speedup factor." Let's go line by line: constructing a `BigData` (allocating ~8MB and filling it) took 47ms, the fixed overhead shared by both groups. Adding a copy pushes the total to 505ms—the pure copy portion is 458ms, because it has to allocate a fresh block and copy 8MB byte by byte. Adding a move gives a total of 45ms, essentially identical to pure construction—meaning the move operation itself is unmeasurable at this scale.

> 💡 **Note on Measurement Noise**: The "pure move" time jitters around zero—one run gives -2.7 ms, the next might be a small positive number. That's expected: high-precision timers pick up tiny differences in scheduling and cache state, and the move's own overhead is far smaller than those differences, so it's drowned in noise. What matters is that it's nowhere near the hundreds of milliseconds a pure copy takes.

So what does the move actually do? It copies a few pointer-sized fields inside `std::vector` (the heap pointer, size, capacity) and nulls the source pointer—a handful of CPU instructions, nanoseconds, negligible next to 47ms of construction. That's why we isolate construction: without isolating it, the "move time" you'd read is 47ms of construction plus nanoseconds of moving, and set against 505ms of construction plus copy that only gives a "roughly 10x faster" number—a figure diluted by construction that actually hides the fact that the move itself is nearly free.

> ⚠️ **Warning**: Don't expect performance improvements on types without move semantics. "Moving" and "copying" are equivalent for `std::array`—because its data is stored directly inside the object, there are no pointers to transfer. Move semantics only provides tangible benefits for types that manage indirect resources (dynamic memory, file handles, etc.).

## Best Practices for Move Semantics in Custom Types

Here are several battle-tested best practices for applying your knowledge of move semantics to your own classes.

For classes managing dynamic resources (memory allocated by `new`, files opened by `fopen`, or similar resource handles), you should implement the full Rule of Five: custom destructor, copy constructor, move constructor, copy assignment, and move assignment. In move constructor and move assignment, nullify the source object's resource pointers to ensure the destructor doesn't release transferred resources. As long as the move operation is guaranteed not to throw exceptions, you should mark it `noexcept` (in most cases move operations are just pointer copies and won't throw).

For classes holding only basic types and standard library containers, you can usually use `= default` to let the compiler generate move operations. `std::vector`, `std::string`, and `std::unique_ptr` all have efficient move semantics. The compiler-generated move constructor will invoke each member's move constructor (for class members) or perform a direct copy (for scalar members) in declaration order. This complies with the C++ standard (see C++17 [class.copy.ctor]).

```cpp
struct DataPoint {
    std::string name;
    std::vector<double> values;
    int id;

    // Compiler-generated move operations are efficient enough
    DataPoint(const DataPoint&) = default;
    DataPoint(DataPoint&&) = default;
    DataPoint& operator=(const DataPoint&) = default;
    DataPoint& operator=(DataPoint&&) = default;
};
```

For classes wrapping exclusive resources (file handles, network connections, locks), you should **disable copy and enable move**. Copying makes no sense—you cannot "duplicate" a TCP connection or a mutex. But moving is reasonable—you can transfer ownership of the connection from one object to another.

```cpp
#include <iostream>
#include <utility>

class FileHandle {
public:
    explicit FileHandle(const char* filename) : fd_(fopen(filename, "r")) {
        if (!fd_) throw std::runtime_error("Failed to open file");
    }

    ~FileHandle() {
        if (fd_) fclose(fd_);
    }

    // Disable copy
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    // Enable move
    FileHandle(FileHandle&& other) noexcept : fd_(other.fd_) {
        other.fd_ = nullptr;
    }

    FileHandle& operator=(FileHandle&& other) noexcept {
        if (this != &other) {
            if (fd_) fclose(fd_);
            fd_ = other.fd_;
            other.fd_ = nullptr;
        }
        return *this;
    }

private:
    FILE* fd_;
};
```

## Embedded Practical Application — Moving Resource Handles

Although this tutorial series focuses on general C++, move semantics has very practical application scenarios in embedded development. In resource-constrained embedded systems, avoiding unnecessary copies not only improves performance but sometimes guarantees functional correctness—for example, ownership of a DMA buffer must be unique, and peripheral access permissions must not be shared.

Below is a simplified but realistic DMA buffer management class, demonstrating how move semantics ensures the uniqueness of resource ownership:

```cpp
#include <iostream>
#include <memory>

class DmaBuffer {
public:
    explicit DmaBuffer(size_t size)
        : size_(size), data_(new uint8_t[size]), owned_(true) {
        std::cout << "Allocated " << size_ << " bytes\n";
    }

    ~DmaBuffer() {
        if (owned_ && data_) {
            std::cout << "Freed " << size_ << " bytes\n";
            delete[] data_;
        }
    }

    // Move constructor
    DmaBuffer(DmaBuffer&& other) noexcept
        : size_(other.size_), data_(other.data_), owned_(other.owned_) {
        other.data_ = nullptr;
        other.owned_ = false;
    }

    // Move assignment
    DmaBuffer& operator=(DmaBuffer&& other) noexcept {
        if (this != &other) {
            if (owned_ && data_) delete[] data_;
            size_ = other.size_;
            data_ = other.data_;
            owned_ = other.owned_;
            other.data_ = nullptr;
            other.owned_ = false;
        }
        return *this;
    }

    // Disable copy
    DmaBuffer(const DmaBuffer&) = delete;
    DmaBuffer& operator=(const DmaBuffer&) = delete;

    uint8_t* data() { return data_; }
    size_t size() { return size_; }

private:
    size_t size_;
    uint8_t* data_;
    bool owned_;
};

DmaBuffer create_buffer() {
    DmaBuffer buf(1024);
    return buf; // NRVO or move
}

int main() {
    DmaBuffer main_buf = create_buffer(); // Move from return value

    std::cout << "Buffer ready at " << static_cast<void*>(main_buf.data()) << "\n";

    // Transfer ownership to peripheral driver
    // DmaBuffer peripheral_buf = std::move(main_buf);
}
```

Output:

```text
Allocated 1024 bytes
Buffer ready at 0x55b9e1e2aeb0
Freed 1024 bytes
```

Notice that throughout the entire lifecycle, only one 1024-byte buffer is allocated—created inside `create_buffer`, to `main_buf` (via NRVO or move), and then potentially to a peripheral driver (via move constructor). There is no extra memory allocation, no data copying, and never a situation where two objects manipulate the same DMA buffer simultaneously—because copying is explicitly disabled by `= delete`.

## Exercise — Implement a Move-Supporting Dynamic Array

Reading theory is good, but writing code is better. This exercise requires you to implement a simplified dynamic array class supporting both copy and move semantics. This class doesn't need to be as complex as `std::vector`, but it needs to handle resource management correctly.

Requirements: Class name `DynArray`, storing data in a `new`-allocated `int` array. Support `push_back` to add elements, resizing when necessary (can simply double capacity). Implement the full Rule of Five. Mark move operations `noexcept`. Implement `size()` and `capacity()`. Write test code to verify copy and move behavior.

Here is the reference implementation framework:

```cpp
#include <algorithm>
#include <iostream>

class DynArray {
public:
    DynArray() : data_(nullptr), size_(0), capacity_(0) {}

    ~DynArray() { /* TODO: Free memory */ }

    // Copy constructor
    DynArray(const DynArray& other) { /* TODO */ }

    // Move constructor
    DynArray(DynArray&& other) noexcept { /* TODO */ }

    // Copy assignment
    DynArray& operator=(const DynArray& other) { /* TODO */ }

    // Move assignment
    DynArray& operator=(DynArray&& other) noexcept { /* TODO */ }

    void push_back(int value) {
        if (size_ >= capacity_) {
            size_t new_cap = (capacity_ == 0) ? 1 : capacity_ * 2;
            int* new_data = new int[new_cap];
            std::copy(data_, data_ + size_, new_data);
            delete[] data_;
            data_ = new_data;
            capacity_ = new_cap;
        }
        data_[size_++] = value;
    }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }

    void print() const {
        std::cout << "[";
        for (size_t i = 0; i < size_; ++i) {
            std::cout << data_[i] << (i < size_ - 1 ? ", " : "");
        }
        std::cout << "]\n";
    }

private:
    int* data_;
    size_t size_;
    size_t capacity_;
};
```

If you get stuck, refer to the `Buffer` class implementation earlier—the logic is almost identical. The key points are: `delete[]` in the destructor, transfer pointers and nullify the source in the move constructor, allocate new memory and copy data in the copy constructor, and `delete[]` current data before taking over new data in move assignment.

Complete reference implementation:

```cpp
#include <algorithm>
#include <iostream>

class DynArray {
public:
    DynArray() : data_(nullptr), size_(0), capacity_(0) {}

    ~DynArray() {
        delete[] data_;
    }

    // Copy constructor
    DynArray(const DynArray& other)
        : data_(new int[other.capacity_]), size_(other.size_), capacity_(other.capacity_) {
        std::copy(other.data_, other.data_ + size_, data_);
    }

    // Move constructor
    DynArray(DynArray&& other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // Copy assignment
    DynArray& operator=(const DynArray& other) {
        if (this != &other) {
            delete[] data_;
            data_ = new int[other.capacity_];
            size_ = other.size_;
            capacity_ = other.capacity_;
            std::copy(other.data_, other.data_ + size_, data_);
        }
        return *this;
    }

    // Move assignment
    DynArray& operator=(DynArray&& other) noexcept {
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

    void push_back(int value) {
        if (size_ >= capacity_) {
            size_t new_cap = (capacity_ == 0) ? 1 : capacity_ * 2;
            int* new_data = new int[new_cap];
            std::copy(data_, data_ + size_, new_data);
            delete[] data_;
            data_ = new_data;
            capacity_ = new_cap;
        }
        data_[size_++] = value;
    }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }

    void print() const {
        std::cout << "[";
        for (size_t i = 0; i < size_; ++i) {
            std::cout << data_[i] << (i < size_ - 1 ? ", " : "");
        }
        std::cout << "]\n";
    }

private:
    int* data_;
    size_t size_;
    size_t capacity_;
};

int main() {
    DynArray arr1;
    arr1.push_back(10);
    arr1.push_back(20);
    arr1.push_back(30);

    std::cout << "arr1: ";
    arr1.print();

    // Test copy
    DynArray arr2 = arr1;
    arr2.push_back(40);
    std::cout << "arr2 (copy): ";
    arr2.print();

    // Test move
    DynArray arr3 = std::move(arr1);
    std::cout << "arr3 (moved from arr1): ";
    arr3.print();
    std::cout << "arr1 after move: size=" << arr1.size() << ", cap=" << arr1.capacity() << "\n";

    // Test move assignment
    DynArray arr4;
    arr4 = std::move(arr3);
    std::cout << "arr4 (move assigned from arr3): ";
    arr4.print();
}
```

Compile and run:

```bash
g++ -std=c++17 main.cpp -o main && ./main
```

Expected output:

```text
arr1: [10, 20, 30]
arr2 (copy): [10, 20, 30, 40]
arr3 (moved from arr1): [10, 20, 30]
arr1 after move: size=0, cap=0
arr4 (move assigned from arr3): [10, 20, 30]
```

After copy construction, `arr2` owns an independent copy of the data; modifying `arr2` does not affect `arr1`. After move construction, `arr3` takes over all data from `arr1`, leaving `arr1` in an empty state (size=0, capacity=0). Afterwards, `arr4` can regain a valid object via move assignment, proving that the moved-from object is indeed in a "valid but unspecified" state—it can be safely assigned a new value or destructed, but you shouldn't rely on its current value.

## Run Online

Run the two examples and verify the key claims of this article yourself:

<OnlineCompilerDemo
  title="push_back vs emplace_back: copy, move, in-place construction"
  source-path="code/examples/vol2/push_back_emplace.cpp"
  description="Trace the construction, copy, move, and destruction logs of Heavy objects to compare push_back(lvalue), push_back(rvalue), and emplace_back."
/>

<OnlineCompilerDemo
  title="How noexcept affects sort vs vector reallocation"
  source-path="code/examples/vol2/noexcept_sort_vs_realloc.cpp"
  description="Count copies and moves: std::sort doesn't distinguish noexcept, but vector reallocation falls back to copy for non-noexcept move types via move_if_noexcept."
/>

That wraps up the chapter on move semantics. From the binding rules of rvalue references, to the implementation of move constructors, through RVO/NRVO and perfect forwarding, and finally to the performance measurements in this article—I hope that from now on, when you see `std::move`, you're not just copy-pasting it, but actually know what it does and why.

Following the thread of resource ownership, the next chapter covers smart pointers: RAII turns all the manual `delete`s and ownership transfers from this chapter into something the compiler manages for you.
