---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: Putting the Rule of Five into practice in your own classes, when = default fits, delete-copy-keep-move designs, and safely moving embedded resource handles
difficulty: intermediate
order: 7
platform: host
prerequisites:
- 'Chapter 0: Move Construction and Move Assignment'
- 'Chapter 0: Move Semantics in Practice: Standard Library Containers and Performance Benchmarks'
reading_time_minutes: 12
related:
- 'Perfect Forwarding: Keeping Value Categories Intact'
title: 'Move Semantics in Practice: Custom Types and Resource Handles'
tags:
- host
- cpp-modern
- intermediate
- 移动语义
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/07-move-custom-types.md
  source_hash: feb5c6878070796bf2387c28ee1d1dee1cb1dbaeb01debc880066ab37545ed38
  translated_at: '2026-11-23T01:11:40+00:00'
  engine: glm-5.3-fence-fix
  token_count: 13172
---
# Move Semantics in Practice: Custom Types and Resource Handles

The previous article covered the wins from standard library containers, with measured numbers to back them up; this one turns to your own types. Three things: how the Rule of Five actually lands on your own classes, which types should use `= default` and which should ban copying and keep only moving, and how to hand resource handles over safely in embedded scenarios. At the end there is a hands-on exercise that runs through the whole chapter's content.

## Move Best Practices for Your Own Types

To apply the move semantics knowledge you've learned to your own classes, here are a few best practices proven in real projects.

For a class that manages dynamic resources (memory from `new`, a file opened with `fopen`, or a similar resource handle), you should implement the complete Rule of Five: a user-defined destructor, copy construction, move construction, copy assignment, and move assignment <RefLink :id="1" preview="C++ Core Guidelines C.21 — if you define or delete any copy, move, or destructor function, define or delete them all" />. In the move constructor and move assignment, null out the source object's resource pointer, so the source object's destructor won't release resources that have already been transferred. Whenever the move operations are guaranteed not to throw, mark them `noexcept` (in the vast majority of cases a move is nothing more than copying a pointer, and it cannot throw).

What does each of these five members manage? Let's take the SimpleVector we'll write in the end-of-article exercise as an example and draw it as one picture:

![SimpleVector's Rule of Five: the responsibilities of the five special members compared with = default](./07-move-custom-types-rulefive.drawio)

For a class that holds only fundamental types and standard library containers, you can usually use `= default` and let the compiler generate the move operations. Standard library components like `std::string`, `std::vector`, and `std::map` all have efficient move semantics, and a compiler-generated move constructor invokes each member's move constructor (for class-type members) or plain-copies it (for scalar members), one by one in declaration order. This is what the C++ standard specifies (see C++17 [class.copy.ctor]) <RefLink :id="2" preview="cppreference The rule of three / five / zero — implicitly-declared and defaulted move operations" />.

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

For a class that wraps an exclusive resource (a file handle, a network connection, a lock), you should **disable copying and enable moving**. Copying makes no sense—you can't "copy" a TCP connection or a mutex. Moving, though, is reasonable—you can transfer control of the connection from one object to another.

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

## Embedded in Practice: Moving Resource Handles

Although this tutorial series focuses on general-purpose C++, move semantics has very real applications in embedded development. On a resource-constrained embedded system, avoiding unnecessary copies doesn't just improve performance—sometimes it is even a guarantee of functional correctness. For instance, a DMA buffer's ownership must be unique, and access rights to a peripheral cannot be shared.

Below is a simplified but realistic DMA buffer management class, showing how move semantics ensures uniqueness of resource ownership:

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

This example is right below—hit "Try it yourself" and run it directly:

<OnlineCompilerDemo
  title="Hands-On Verification: dma_buffer_move.cpp"
  source-path="code/examples/vol2/19_dma_buffer_move.cpp"
  description="Verify DMA buffer ownership transfer online. The output shows exactly one allocation and one release—there is a single buffer in circulation the whole time."
  run-options="-std=c++17"
  allow-run
/>

Notice that the 1024-byte buffer is allocated exactly once over its entire lifetime—created inside `receive_dma`, arriving in `rx_buf` in `main` (via NRVO or a move), then in `process_buf` (via move construction). At every moment there is exactly one buffer in circulation. No extra memory allocations, no data copies, and never two objects operating on the same DMA buffer at the same time—because copying is banned with `= delete`.

## Exercise: Implement a Dynamic Array That Supports Move

You can read all the theory you want—nothing beats writing it once with your own hands. This exercise asks you to implement a simplified dynamic array class that supports both copy semantics and move semantics. The class doesn't need to be as complex as `std::vector`, but it does need to handle resource management correctly.

The requirements are as follows: the class is named `SimpleVector`, storing its data in an `int` array allocated with `new[]`. Support `push_back(int)` to append elements, growing the capacity when needed (simply doubling works). Implement the complete Rule of Five. Mark the move operations `noexcept`. Implement `size()` and `operator[]`. Write some test code that verifies the copy and move behavior.

Here is the reference implementation skeleton:

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

If you get stuck, go look at the `Buffer` class implementation in [Move Construction and Move Assignment](02-move-semantics.md)—the logic is almost exactly the same. The key points: `delete[] data_` in the destructor; in the move constructor, transfer the pointer and null out the source object's pointer; in the copy constructor, allocate new memory and copy the data over; in move assignment, `delete[]` the current data first, then take over the new data.

The complete reference implementation:

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

The reference solution is right below—hit "Try it yourself" and run it directly (write your own first; compare only once you're stuck):

<OnlineCompilerDemo
  title="Hands-On Verification: simple_vector_solution.cpp"
  source-path="code/examples/vol2/20_simple_vector_solution.cpp"
  description="Run the exercise's reference solution online. Focus on the two lines after move construction: a's size and capacity both drop to zero, and a fresh assignment brings it back to life."
  run-options="-std=c++17"
  allow-run
/>

After copy construction, `b` owns an independent copy of the data, and modifying `b` leaves `a` untouched. After move construction, `c` has taken over all of `a`'s data, and `a` is left in an empty state (size=0, capacity=0). Afterwards, `a` can regain a valid object through move assignment, which proves that a moved-from object really is in a "valid but unspecified" state <RefLink :id="3" preview="cppreference std::move — Notes: moved-from objects are valid but unspecified" />—it can safely be assigned a new value or destroyed, but you should not rely on its current value.

And with that, the move semantics chapter is complete. From the binding rules of rvalue references, to implementing move construction, on to RVO/NRVO and perfect forwarding, and finally down to the performance benchmarks in this article—from now on, when you see `std::move`, I hope you won't just be copying it on faith, but will know clearly what it is doing and why.

Following the thread of resource ownership, the next chapter talks about smart pointers: RAII turns the manual `delete` calls and ownership transfers of this chapter into things the compiler manages automatically.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Bjarne Stroustrup / Herb Sutter (eds.)"
    title="C++ Core Guidelines — C.21: If You Define or Delete Any Copy, Move, or Destructor Function, Define or Delete Them All"
    publisher="isocpp.org"
    url="https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rc-five"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="The Rule of Three / Five / Zero"
    chapter="Implicitly-declared and defaulted move operations"
    url="https://en.cppreference.com/w/cpp/language/rule_of_three"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="std::move"
    chapter="Notes: moved-from state"
    url="https://en.cppreference.com/w/cpp/utility/move"
  />
</ReferenceCard>
