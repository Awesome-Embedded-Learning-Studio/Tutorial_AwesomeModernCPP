---
title: "移动构造与移动赋值"
description: "掌握移动语义的核心机制，实现零拷贝资源转移"
chapter: 0
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 移动语义
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 18
prerequisites:
  - "Chapter 0: 右值引用"
related:
  - "RVO 与 NRVO"
  - "完美转发"
---

# 移动构造与移动赋值

上一篇我们把值类别和右值引用的底子打好了。现在该干正事了——让我们的类真正学会"移动"而不是"拷贝"。说实话，笔者第一次手写移动构造函数的时候犯了不少错：忘记置空源对象的指针、忘记处理自赋值、搞不清楚什么时候该加 `noexcept`……这篇文章就把自己踩过的坑一并分享出来，争取让大家少走弯路。

我们先从一个简单但足够真实的场景入手：自己实现一个动态缓冲区类，然后用它来一步步搞懂移动构造、移动赋值、以及所谓的"五个重要规则"（Rule of Five）。

## 我们为什么需要移动——从拷贝的代价说起

假设你在写一个文本处理工具，需要频繁地在函数之间传递大块文本数据。先看一个最朴素的动态缓冲区实现：

```cpp
class Buffer {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    explicit Buffer(std::size_t capacity)
        : data_(new char[capacity])
        , size_(0)
        , capacity_(capacity)
    {
    }

    // 拷贝构造：深拷贝
    Buffer(const Buffer& other)
        : data_(new char[other.capacity_])
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        std::memcpy(data_, other.data_, size_); // 直接平凡的拷贝数据
    }

    // 拷贝赋值：深拷贝
    Buffer& operator=(const Buffer& other)
    {
        if (this != &other) {
            delete[] data_;
            data_ = new char[other.capacity_];
            size_ = other.size_;
            capacity_ = other.capacity_;
            std::memcpy(data_, other.data_, size_);
        }
        return *this;
    }

    ~Buffer()
    {
        delete[] data_;
    }

    void append(const char* str, std::size_t len)
    {
        if (size_ + len <= capacity_) {
            std::memcpy(data_ + size_, str, len);
            size_ += len;
        }
    }

    const char* data() const { return data_; }
    std::size_t size() const { return size_; }
};
```

现在我们来做一个实验：创建一个 1MB 的缓冲区，然后把它传进一个函数。

```cpp
#include <iostream>

Buffer process_buffer(Buffer buf)
{
    std::cout << "处理中，大小: " << buf.size() << " 字节\n";
    return buf;
}

int main()
{
    Buffer large(1024 * 1024);  // 1MB
    large.append("Hello, World!", 13);

    Buffer result = process_buffer(large);  // 拷贝！
    return 0;
}
```

调用 `process_buffer(large)` 时发生了什么？参数 `buf` 是按值传递的，所以编译器调用 `Buffer` 的拷贝构造函数来创建 `buf`——这意味着分配 1MB 新内存，然后把 `large` 里的数据逐字节拷贝过去。函数返回时，`return buf;` 又触发一次拷贝构造来创建 `result`。加上函数结束时 `buf` 的析构——整个过程做了**两次 1MB 的内存分配、两次 1MB 的内存拷贝、一次 1MB 的内存释放**。而我们真正需要的只是把数据从 `main` 里的 `large` 转移到 `result` 里。（我估计老C++人看到这样写已经满面红光了，相信屏幕前的你也会绷不住）

这就是拷贝语义的根本问题：当你不再需要源对象的时候，拷贝构造函数仍然忠实地复制每一个字节，然后源对象析构时又老老实实地释放掉原来的那块内存。资源分配了又释放，数据拷贝了又丢弃——纯粹的浪费。

## 移动构造函数——资源所有权的转移

移动构造函数的核心思想非常简单：不复制数据，只转移资源的所有权。对于管理了动态内存的类，这意味着把指针从源对象"偷"过来，然后把源对象的指针置空，防止它析构时释放这块内存。

```cpp
class Buffer {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    // ... 前面的构造函数和析构函数不变 ...

    // 移动构造函数
    Buffer(Buffer&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
};
```

我们逐行来看这个移动构造函数。签名 `Buffer(Buffer&& other)` 中的 `&&` 表明这是一个移动构造函数——它只接受右值参数。函数体里我们做了三件事：把 `other` 的三个成员直接拷贝到 `this` 里（三个指针/整数的赋值，代价极低），然后把 `other` 的指针置空。最后一步至关重要——如果我们不把 `other.data_` 置空，`other` 析构时 `delete[] other.data_` 会把刚转移过来的内存释放掉，`this` 就持有了悬空指针，后面访问必崩。

现在我们用 `std::move` 来触发移动构造：

```cpp
Buffer large(1024 * 1024);
large.append("Hello, World!", 13);

Buffer moved_to = std::move(large);  // 调用移动构造函数
// large.data_ 现在是 nullptr，但 large 仍然可以安全析构
// moved_to 持有了原来那 1MB 的内存
```

整个过程做了什么？三次指针/整数的赋值——完事。没有 `new`，没有 `memcpy`，没有 `delete`。从 O(n) 的拷贝操作变成了 O(1) 的指针转移。对于 1MB 的缓冲区来说，这就是"分配 1MB 内存加拷贝 1MB 数据"和"赋值三个寄存器"之间的差距。

## 移动赋值运算符——比移动构造多一步

移动赋值运算符比移动构造函数稍微复杂一点，因为赋值的目标对象可能已经持有资源——我们必须先释放旧资源，再接管新资源。

```cpp
class Buffer {
    // ... 前面的代码不变 ...

    // 移动赋值运算符
    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other) {
            // 第一步：释放当前持有的资源
            delete[] data_;

            // 第二步：接管 other 的资源
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;

            // 第三步：置空 other
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }
};
```

注意第一步 `delete[] data_`——这是移动赋值和移动构造的关键区别。移动构造时目标对象还没初始化，不存在旧资源需要释放；移动赋值时目标对象已经存在，如果不先释放旧资源就会内存泄漏。`if (this != &other)` 的自赋值检查也是必要的——虽然 `x = std::move(x)` 这种代码在正常开发中几乎不会出现，但标准库组件（比如 `std::swap`）的通用实现可能会产生等价的操作，加一道保险是负责任的做法。

来看移动赋值在实际代码中的效果：

```cpp
Buffer a(1024);
a.append("Hello", 5);

Buffer b(2048);
b.append("World", 5);

a = std::move(b);  // 移动赋值
// a 原来的 1KB 缓冲区被 delete[] 释放
// a 接管了 b 的 2KB 缓冲区
// b.data_ 变为 nullptr
```

> ⚠️ **踩坑预警**：移动后的源对象处于"有效但未指定"（valid but unspecified）的状态。这意味着你可以安全地对它赋新值、让它析构，但不应该读取它的值——比如 `moved_from.size()` 可能返回 0，也可能返回原来的值，取决于具体实现。笔者的建议是：移动之后立即让源对象离开作用域，或者给它赋一个明确的新值，永远不要让"已移动"的对象在你的代码里游荡。

## noexcept——移动操作的安全承诺

你可能注意到两个移动操作都被标记了 `noexcept`。这不是可有可无的装饰——它有实实在在的性能影响。

原因在于 `std::vector` 的扩容行为。当 `vector` 需要增长容量时，它必须把现有元素转移到新的内存块。如果元素的移动构造函数是 `noexcept` 的，`vector` 会放心地使用移动；如果移动构造函数可能抛异常，`vector` 会退而使用拷贝构造——因为在移动过程中如果抛异常，已经移动了一半的状态很难恢复，但拷贝过程中抛异常，原来的数据还完好无损。

```cpp
// vector 内部逻辑的简化版本
if constexpr (std::is_nothrow_move_constructible_v<T>) {
    // 使用移动构造——快速且安全
} else {
    // 退化为拷贝构造——慢但异常安全
}
```

你可以用 `static_assert` 来验证自己的类是否真的满足 `noexcept` 移动：

```cpp
static_assert(std::is_nothrow_move_constructible_v<Buffer>,
              "Buffer should be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable_v<Buffer>,
              "Buffer should be nothrow move assignable");
```

这不是纸上谈兵的理论——我们可以写一个实验来验证 `vector` 的实际行为。准备两个结构相同的 `Buffer` 类，唯一的区别是移动构造函数有没有 `noexcept`，然后让 `vector` 扩容。结果非常清楚：

```text
=== noexcept 移动 + vector 扩容 ===
--- 触发扩容 ---
  [Noexcept版] 移动构造    <-- vector 放心地移动

=== 非 noexcept 移动 + vector 扩容 ===
--- 触发扩容 ---
  [Throwing版] 拷贝构造    <-- vector 退回拷贝，确保异常安全
```

GCC 15、`-std=c++17 -O2` 下编译运行，行为完全符合预期。完整代码见 `noexcept_vector_realloc.cpp`。

## 规则五（Rule of Five）

C++ 有一个经典的"规则三"（Rule of Three）：如果你的类需要自定义析构函数、拷贝构造函数或拷贝赋值运算符中的任何一个，那它很可能三个都需要。C++11 把移动构造函数和移动赋值运算符加进来，变成了"规则五"。

如果你只声明了析构函数但没有声明移动操作，编译器是**不会**自动生成移动构造函数和移动赋值运算符的，那咋办呢？它会退而求其次使用拷贝操作。这经常让新手困惑：明明用了 `std::move`，但实际调用的还是拷贝构造函数。`std::move` 本身并不移动任何东西——它只是一个 `static_cast` 到右值引用的类型转换。最终决定调用移动构造还是拷贝构造的，是类的定义。如果类没有移动构造函数，右值引用会完美匹配到 `const T&` 的拷贝构造函数上去。

```cpp
class OnlyDestructor {
    char* data_;

public:
    OnlyDestructor(std::size_t n) : data_(new char[n]) {}
    ~OnlyDestructor() { delete[] data_; }

    // 没有声明移动构造函数！
    // 编译器也不会隐式生成（因为有自定义析构函数）
};

OnlyDestructor a(100);
OnlyDestructor b = std::move(a);  // 退化为拷贝构造！
                                    // 隐式拷贝构造做浅拷贝 -> 双重 delete
```

这里的后果比"低效"更严重——因为隐式生成的拷贝构造函数做的是浅拷贝（逐成员复制指针），`a` 和 `b` 的 `data_` 会指向同一块内存。当两者析构时，`delete[]` 被调用两次，直接触发 double free。我们可以用 type trait 来验证这个行为：

```cpp
static_assert(!std::is_trivially_move_constructible_v<OnlyDestructor>,
              "没有真正的移动构造函数");
static_assert(std::is_move_constructible_v<OnlyDestructor>,
              "但 is_move_constructible 为 true——退回到拷贝构造");
```

看起来矛盾？不矛盾。`is_move_constructible` 为 true 是因为编译器可以用拷贝构造函数来"满足"移动构造的需求（右值可以绑定到 `const T&`），但这并不意味着存在一个真正的移动构造函数来做指针转移。完整的验证代码在 `rule_of_five_fallback.cpp` 中。

对于管理资源的类，最安全的做法是**五个特殊成员函数要么全部自定义，要么全部 = default**。如果你用智能指针来管理资源，通常可以用 `= default` 让编译器生成正确的版本——这正是现代 C++ 推荐的方式。但对于我们这种手动管理原始指针的类，就必须老老实实写齐五个：

```cpp
class Buffer {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    // 1. 构造函数
    explicit Buffer(std::size_t capacity)
        : data_(new char[capacity])
        , size_(0)
        , capacity_(capacity)
    {
    }

    // 2. 析构函数
    ~Buffer()
    {
        delete[] data_;
    }

    // 3. 拷贝构造
    Buffer(const Buffer& other)
        : data_(new char[other.capacity_])
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        std::memcpy(data_, other.data_, size_);
    }

    // 4. 移动构造
    Buffer(Buffer&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // 5. 拷贝赋值
    Buffer& operator=(const Buffer& other)
    {
        if (this != &other) {
            delete[] data_;
            data_ = new char[other.capacity_];
            size_ = other.size_;
            capacity_ = other.capacity_;
            std::memcpy(data_, other.data_, size_);
        }
        return *this;
    }

    // 6. 移动赋值
    Buffer& operator=(Buffer&& other) noexcept
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
};
```

看起来有点长，但逻辑都是重复的——拷贝操作做深拷贝，移动操作做指针转移加源对象置空。

## copy-and-swap 惯用法——减少重复代码

如果你觉得写四个赋值运算符（拷贝赋值 + 移动赋值）太啰嗦，有一个经典的惯用法可以帮你简化。核心思路是：**让拷贝赋值和移动赋值共用一个实现**，利用值传递的语义来自动选择拷贝或移动。

```cpp
class Buffer {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    explicit Buffer(std::size_t capacity = 0)
        : data_(capacity ? new char[capacity] : nullptr)
        , size_(0)
        , capacity_(capacity)
    {
    }

    ~Buffer() { delete[] data_; }

    // 拷贝构造
    Buffer(const Buffer& other)
        : data_(other.capacity_ ? new char[other.capacity_] : nullptr)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        if (data_) {
            std::memcpy(data_, other.data_, size_);
        }
    }

    // 移动构造
    Buffer(Buffer&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // 统一的赋值运算符——通过值传递自动选择拷贝或移动
    Buffer& operator=(Buffer other) noexcept
    {
        swap(*this, other);
        return *this;
    }

    friend void swap(Buffer& a, Buffer& b) noexcept
    {
        using std::swap;
        swap(a.data_, b.data_);
        swap(a.size_, b.size_);
        swap(a.capacity_, b.capacity_);
    }
};
```

这里 `operator=(Buffer other)` 按值接收参数——如果你传一个左值进来，`other` 通过拷贝构造创建；如果你传一个右值（比如 `std::move(x)`），`other` 通过移动构造创建。然后 `swap` 交换 `this` 和 `other` 的内容，函数结束时 `other` 析构，自动释放旧资源。

这个惯用法的优点是代码量少、异常安全、自动处理自赋值。缺点是多了一次 swap 操作（三次指针交换），对于极致性能场景可能有微小影响。不过在绝大多数场景下，这点开销完全可以忽略不计——用 GCC 15 在 `-O2` 下对比汇编可以发现，copy-and-swap 的移动赋值路径比独立移动赋值运算符多出约三条寄存器移动指令（即 swap 的代价），但没有额外的函数调用或内存操作。对于管理动态内存的类来说，`new`/`delete` 的开销远大于这三条寄存器指令，所以 copy-and-swap 的额外代价在实际中几乎无法测量。

## 通用示例——文件句柄的移动

除了动态内存，移动语义在管理其他资源的类上同样威力巨大。文件句柄就是典型的例子——操作系统对同一文件的打开数量有限制，如果你不小心拷贝了持有文件句柄的对象，就可能导致句柄泄漏或者重复关闭。

```cpp
#include <cstdio>
#include <utility>
#include <iostream>

class FileHandle {
    std::FILE* file_;
    std::string path_;

public:
    explicit FileHandle(const char* path, const char* mode)
        : file_(std::fopen(path, mode))
        , path_(path)
    {
        if (!file_) {
            throw std::runtime_error("Failed to open file: " + path_);
        }
    }

    ~FileHandle()
    {
        if (file_) {
            std::fclose(file_);
            std::cout << "  关闭文件: " << path_ << "\n";
        }
    }

    // 禁止拷贝——文件句柄不可共享
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    // 允许移动——文件句柄可以转移所有权
    FileHandle(FileHandle&& other) noexcept
        : file_(other.file_)
        , path_(std::move(other.path_))
    {
        other.file_ = nullptr;  // 防止 other 析构时关闭文件
    }

    FileHandle& operator=(FileHandle&& other) noexcept
    {
        if (this != &other) {
            if (file_) {
                std::fclose(file_);  // 关闭当前文件
            }
            file_ = other.file_;
            path_ = std::move(other.path_);
            other.file_ = nullptr;
        }
        return *this;
    }

    std::FILE* get() const { return file_; }
    const std::string& path() const { return path_; }
};

/// @brief 工厂函数：打开日志文件
FileHandle open_log(const std::string& name)
{
    return FileHandle(name.c_str(), "a");
}

int main()
{
    auto log = open_log("app.log");
    std::fprintf(log.get(), "Application started\n");

    // 把日志文件的所有权转移给另一个变量
    FileHandle moved_log = std::move(log);
    std::fprintf(moved_log.get(), "Log handle moved\n");

    // log.get() 现在返回 nullptr，不要再使用它
    return 0;
}
```

这个例子展示了一个常见的设计模式：**不可拷贝但可移动**。文件句柄在物理上只有一份，不应该被"拷贝"出第二份——拷贝会导致两个对象都试图关闭同一个文件。但移动是合理的：`open_log` 创建了文件句柄，然后把所有权转移给调用者，函数内部的临时对象不再持有任何资源。

运行这段程序，你会看到：

```text
  关闭文件: app.log
```

注意只有一次"关闭文件"输出——尽管 `log` 和 `moved_log` 都经历了析构，但 `log` 的 `file_` 在移动后被置空了，所以它的析构函数里的 `if (file_)` 检查不通过，不会重复关闭。

## 动手实验——move_semantics_demo.cpp

我们写一个完整的程序来验证移动语义的所有关键行为。

```cpp
// move_semantics_demo.cpp -- 移动构造与移动赋值演示
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>
#include <vector>

class Buffer
{
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    explicit Buffer(std::size_t capacity)
        : data_(new char[capacity])
        , size_(0)
        , capacity_(capacity)
    {
        std::cout << "  [Buffer] 分配 " << capacity << " 字节\n";
    }

    ~Buffer()
    {
        if (data_) {
            std::cout << "  [Buffer] 释放 " << capacity_ << " 字节\n";
            delete[] data_;
        }
    }

    Buffer(const Buffer& other)
        : data_(new char[other.capacity_])
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        std::memcpy(data_, other.data_, size_);
        std::cout << "  [Buffer] 拷贝构造 " << capacity_ << " 字节\n";
    }

    Buffer(Buffer&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        std::cout << "  [Buffer] 移动构造（指针转移）\n";
    }

    Buffer& operator=(const Buffer& other)
    {
        if (this != &other) {
            delete[] data_;
            data_ = new char[other.capacity_];
            size_ = other.size_;
            capacity_ = other.capacity_;
            std::memcpy(data_, other.data_, size_);
            std::cout << "  [Buffer] 拷贝赋值 " << capacity_ << " 字节\n";
        }
        return *this;
    }

    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other) {
            delete[] data_;
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
            std::cout << "  [Buffer] 移动赋值（指针转移）\n";
        }
        return *this;
    }

    void append(const char* str, std::size_t len)
    {
        if (size_ + len <= capacity_) {
            std::memcpy(data_ + size_, str, len);
            size_ += len;
        }
    }

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }
};

int main()
{
    std::cout << "=== 1. 创建两个缓冲区 ===\n";
    Buffer a(1024);
    a.append("Hello", 5);
    Buffer b(2048);
    b.append("World", 5);
    std::cout << '\n';

    std::cout << "=== 2. 拷贝构造 ===\n";
    Buffer c = a;
    std::cout << "  c.size() = " << c.size() << "\n\n";

    std::cout << "=== 3. 移动构造 ===\n";
    Buffer d = std::move(b);
    std::cout << "  d.size() = " << d.size() << "\n";
    std::cout << "  b.capacity() = " << b.capacity() << "\n\n";

    std::cout << "=== 4. 移动赋值 ===\n";
    a = std::move(d);
    std::cout << "  a.size() = " << a.size() << "\n";
    std::cout << "  d.capacity() = " << d.capacity() << "\n\n";

    std::cout << "=== 5. vector 中的移动 ===\n";
    std::vector<Buffer> buffers;
    buffers.reserve(4);
    std::cout << "  push_back 左值:\n";
    buffers.push_back(c);             // 拷贝
    std::cout << "  push_back std::move:\n";
    buffers.push_back(std::move(c));  // 移动
    std::cout << "  emplace_back 原位构造:\n";
    buffers.emplace_back(512);        // 直接在 vector 中构造
    std::cout << '\n';

    std::cout << "=== 6. 程序结束 ===\n";
    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o move_demo move_semantics_demo.cpp
./move_demo
```

预期输出：

```text
=== 1. 创建两个缓冲区 ===
  [Buffer] 分配 1024 字节
  [Buffer] 分配 2048 字节

=== 2. 拷贝构造 ===
  [Buffer] 拷贝构造 1024 字节
  c.size() = 5

=== 3. 移动构造 ===
  [Buffer] 移动构造（指针转移）
  d.size() = 5
  b.capacity() = 0

=== 4. 移动赋值 ===
  [Buffer] 移动赋值（指针转移）
  a.size() = 5
  d.capacity() = 0

=== 5. vector 中的移动 ===
  push_back 左值:
  [Buffer] 拷贝构造 1024 字节
  push_back std::move:
  [Buffer] 移动构造（指针转移）
  emplace_back 原位构造:
  [Buffer] 分配 512 字节

=== 6. 程序结束 ===
  [Buffer] 释放 1024 字节
  [Buffer] 释放 1024 字节
  [Buffer] 释放 512 字节
  [Buffer] 释放 2048 字节
```

输出中"移动构造（指针转移）"和"拷贝构造 X 字节"的对比一目了然——拷贝需要分配内存加复制数据，移动只是三个指针的赋值。第 5 步的 vector 操作更值得注意：`push_back` 传入左值时发生拷贝，传入 `std::move` 的右值时发生移动，而 `emplace_back` 直接在 vector 的内存中原位构造，连移动都省了。这三个操作的性能差异在大数据量的场景下会非常明显。

注意到析构时没有"释放 0 字节"的输出——那些就是被移动过的对象，它们的 `data_` 是 `nullptr`，析构函数里的 `if (data_)` 检查跳过了 `delete[]`。vector 中的三个元素各自独立析构——第一个是 `c` 的拷贝（1024 字节），第二个是 `c` 移动过来的（1024 字节），第三个是 `emplace_back` 原位构造的（512 字节）。

## 小结

这一篇我们把移动构造函数和移动赋值运算符从头到尾拆解了一遍。移动操作的核心是**资源所有权转移**——不复制数据，只偷指针，然后把源对象置空。移动赋值比移动构造多一步：要先释放目标对象持有的旧资源。所有移动操作都应该标记 `noexcept`，这直接影响 `std::vector` 等容器在扩容时的行为。如果你的类管理了资源，记住规则五：析构函数、拷贝构造、移动构造、拷贝赋值、移动赋值，五个要么全写，要么全 `= default`。

下一篇我们来看编译器在背后帮我们做的另一件大事——返回值优化（RVO 和 NRVO），它能让函数返回大对象的代价直接归零。
