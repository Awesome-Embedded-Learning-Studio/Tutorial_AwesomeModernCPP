---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: 掌握移动语义的核心机制，实现零拷贝资源转移
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 0: 右值引用'
reading_time_minutes: 13
related:
- 规则五：特殊成员函数的配套关系
- RVO 与 NRVO
- 完美转发
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: 移动构造与移动赋值
---
# 移动构造与移动赋值

上一篇结尾说好了，这一篇咱们亲手给管理资源的类写移动构造和移动赋值。笔者头一回亲手写它们的时候，错得可不少：源对象的指针忘了置空，自赋值的检查也漏了，`noexcept` 该不该加也拿不准……把当时犯过的错一并摊开讲给您，帮您写的时候一次避开。

咱们从一个简单但足够真实的场景入手：自己动手实现一个动态缓冲区类，再一步步把移动构造、移动赋值的机制弄懂。规则五和 copy-and-swap 的配套写法，留给下一篇专门讲。

## 为什么需要移动——从拷贝的代价说起

假设您在写一个文本处理工具，需要频繁地在函数之间传递大块文本数据。咱们从最朴素的动态缓冲区实现看起：

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

现在咱们来做一个实验：创建一个 1MB 的缓冲区，然后您把它传进一个函数。

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

咱们调用 `process_buffer(large)` 的时候，到底发生了什么？参数 `buf` 是按值传递的，编译器创建它靠的就是 `Buffer` 的拷贝构造函数，代价也就跟着来了：分配 1MB 新内存，再把 `large` 里的数据逐字节拷贝过去。函数返回的时候，`return buf;` 又触发了一次拷贝构造，这才有了 `result`。等到函数收尾的时候，`buf` 也跟着析构了一次。咱们把整个过程算下来，做了**两次 1MB 的内存分配，两次 1MB 的内存拷贝，外加一次 1MB 的内存释放**。而咱们真正需要的，只是把数据从 `main` 里的 `large` 转移到 `result` 里。

> 笔者估计，写惯了老 C++ 的朋友看到这样写已经满面红光了。相信屏幕前的您，也会一样绷不住的。

拷贝语义的问题，到这里就露出来了：您明明不再需要源对象了，拷贝构造函数还是忠实地复制每一个字节。等源对象析构的时候，它又老老实实地把那块内存释放掉。资源分配了又释放，数据拷贝了又丢弃——纯粹的浪费。

## 移动构造函数——资源所有权的转移

轮到移动构造函数出场了。它要做的事情，一句话就能说清：一个字节的数据都不复制，只把资源的所有权转过去<RefLink :id="1" preview="cppreference Move constructor — transfer instead of copy" />。落到管理动态内存的类上，动作就是把源对象的指针"偷"过来，再把源对象置空了事。咱们直接看代码：

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

咱们逐行来看这个移动构造函数。签名 `Buffer(Buffer&& other)` 里的 `&&` 表明它只接受右值参数。函数体里干的事情不多：把 `other` 的三个成员直接搬过来，也就是三个指针/整数的赋值，代价低到咱们几乎可以忽略。剩下的一步，是把 `other` 的成员清零。置空的这一步，需要咱们多看一眼。假如咱们不把 `other.data_` 置空，`other` 析构的时候，`delete[] other.data_` 会把刚转过来的那块内存释放掉。这下 `this` 手里捏着的，就成了悬空指针，再去访问它的时候，等来的就是崩溃。

现在咱们用 `std::move` 来触发移动构造：

```cpp
Buffer large(1024 * 1024);
large.append("Hello, World!", 13);

Buffer moved_to = std::move(large);  // 调用移动构造函数
// large.data_ 现在是 nullptr，但 large 仍然可以安全析构
// moved_to 持有了原来那 1MB 的内存
```

这一趟下来发生了什么？咱们数一数：把 `other` 的成员搬过来、置空，几个指针/整数的赋值就完事。没有 `new` 的分配，没有 `memcpy` 的复制，也没有 `delete` 的释放。O(n) 的拷贝，就这么变成了 O(1) 的指针转移。对 1MB 的缓冲区来说，一边要分配 1MB 的内存、再拷贝 1MB 的数据，另一边咱们只搬几个寄存器，您说差距大不大。

## 移动赋值运算符——比移动构造多一步

移动赋值运算符比移动构造函数多了一步麻烦。构造的时候，目标对象的初始化还没发生，谈不上持有什么旧资源。赋值的时候，目标对象已经在了，手里可能还攥着一份现成的资源。所以咱们得把旧的放掉，才好去接管新的<RefLink :id="2" preview="cppreference Move assignment operator — release current resource, take over the source" />。

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

咱们把镜头对准函数体开头的 `delete[] data_`。这一行放掉的，就是上一段里目标对象正攥着的现成资源。咱们要是不放掉它，内存就直接泄漏了。`if (this != &other)` 的自赋值检查，咱们也得提一嘴。`x = std::move(x)` 这样的代码，正常开发的时候几乎不会有人写。可真写出来了，坏起事来不含糊：`delete[] data_` 把自己的资源放掉了，接下来又从已经悬空的 `other`（其实就是它自己）里取指针，这就直接构成了 UAF（use-after-free，释放后的使用）。咱们多加一道检查，用几行代码换一个确定的结局，是值得的。

咱们来看移动赋值在实际代码里的效果：

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

被移动过的那个源对象，它的状态成了"有效但未指定"，英文的说法是 valid but unspecified<RefLink :id="3" preview="cppreference std::move — Notes: moved-from standard-library objects are valid but unspecified" />。您可以让它安全地析构，也可以接着给它赋新的值，但别去读它的值——被移动过的标准库类型，`size()` 这样的调用可能给您 0，也可能给您原来的值，全看库的具体实现。笔者的建议是：移动之后，要么让源对象马上离开作用域，要么给它赋一个明确的新值。在它拿到明确的新值之前，您别再去读它。

## noexcept——移动操作的安全承诺

您可能已经注意到，两个移动操作的身上都挂着 `noexcept`。它不是可有可无的装饰，背后连着实实在在的性能。

原因得从 `std::vector` 的扩容行为里找。容量不够用了，`vector` 得把现有元素转移到新的内存块。走到这一步的时候，它就要掂量元素的移动构造函数了：标了 `noexcept` 的，`vector` 放心地用移动。要是移动构造可能抛异常呢，`vector` 就退回去改用拷贝构造了<RefLink :id="4" preview="cppreference std::move_if_noexcept — how vector reallocation picks move vs copy" />。道理咱们想一下就通——移动挪到一半抛了异常，已经搬走一半的状态很难恢复。拷贝途中抛异常就不一样了，原来的数据还完好无损。

```cpp
// vector 内部逻辑的简化版本
if constexpr (std::is_nothrow_move_constructible_v<T>) {
    // 使用移动构造——快速且安全
} else {
    // 退化为拷贝构造——慢但异常安全
}
```

您可以用 `static_assert` 来验证自己的类是否真的满足 `noexcept` 移动：

```cpp
static_assert(std::is_nothrow_move_constructible_v<Buffer>,
              "Buffer should be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable_v<Buffer>,
              "Buffer should be nothrow move assignable");
```

光讲道理就是纸上谈兵了，咱们真跑一个实验，看看 `vector` 实际怎么选。准备两个结构一模一样的类，唯一的差别是移动构造函数带不带 `noexcept`，然后咱们让 `vector` 扩容。最省事的做法，是用一个模板参数 `NoexceptMove` 切换 `noexcept` 的标记，其余的代码完全相同：

```cpp
// noexcept_vector_realloc.cpp -- noexcept 移动 vs 非 noexcept 移动 在 vector 扩容时的差异
// Standard: C++17

#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// 用模板参数 NoexceptMove 切换移动构造是否标 noexcept，其余代码完全相同
template <bool NoexceptMove>
class TrackedBuffer
{
    char* data_;
    std::size_t capacity_;
    std::string tag_;

public:
    explicit TrackedBuffer(std::size_t cap, std::string tag)
        : data_(new char[cap])
        , capacity_(cap)
        , tag_(std::move(tag))
    {
    }

    ~TrackedBuffer() { delete[] data_; }

    TrackedBuffer(const TrackedBuffer& other)
        : data_(new char[other.capacity_])
        , capacity_(other.capacity_)
        , tag_(other.tag_)
    {
        std::cout << "  [" << tag_ << "] 拷贝构造\n";
    }

    // 唯一的区别：noexcept 标记
    TrackedBuffer(TrackedBuffer&& other) noexcept(NoexceptMove)
        : data_(other.data_)
        , capacity_(other.capacity_)
        , tag_(std::move(other.tag_))
    {
        other.data_ = nullptr;
        other.capacity_ = 0;
        std::cout << "  [" << tag_ << "] 移动构造\n";
    }

    TrackedBuffer& operator=(const TrackedBuffer&) = delete;
    TrackedBuffer& operator=(TrackedBuffer&&) = delete;
};

int main()
{
    using NB = TrackedBuffer<true>;   // 移动构造标了 noexcept
    using TB = TrackedBuffer<false>;  // 移动构造没标 noexcept

    static_assert(std::is_nothrow_move_constructible_v<NB>,
                  "NB 的移动构造是 noexcept");
    static_assert(!std::is_nothrow_move_constructible_v<TB>,
                  "TB 的移动构造不是 noexcept");

    std::cout << "=== noexcept 移动 + vector 扩容 ===\n";
    {
        std::vector<NB> v;
        v.reserve(1);                       // 先预留 1 个槽位
        v.emplace_back(64, "Noexcept版");   // 占住唯一的槽位
        std::cout << "--- 触发扩容 ---\n";
        v.emplace_back(64, "Noexcept版");   // 超出容量，必须扩容搬运
    }

    std::cout << "\n=== 非 noexcept 移动 + vector 扩容 ===\n";
    {
        std::vector<TB> v;
        v.reserve(1);
        v.emplace_back(64, "Throwing版");
        std::cout << "--- 触发扩容 ---\n";
        v.emplace_back(64, "Throwing版");   // 扩容时 vector 不敢用移动，退回拷贝
    }

    return 0;
}
```

实验程序就挂在下面的演示里，您点「动手试一试」直接跑：

<OnlineCompilerDemo
  title="动手验证：noexcept_vector_realloc.cpp"
  source-path="code/examples/vol2/noexcept_vector_realloc.cpp"
  description="在线验证 vector 扩容时的选择，跑起来对照两段输出各自触发的是哪种构造。"
  run-options="-O0 -std=c++17"
  allow-run
/>

两段输出各自只打印了一行，咱们对着看。为什么各只有一行？类里会打印的只有拷贝构造和移动构造，两次 `emplace_back` 的原地构造都不吭声，真正打印出来的，是扩容搬运的那一步。挂 `[Noexcept版]` 标签的行写着**移动构造**，挂 `[Throwing版]` 标签的行写着**拷贝构造**。

## 动手实验——move_semantics_demo.cpp

咱们写一个完整的程序，把移动语义的关键行为都验证一遍。

```cpp
// move_semantics_demo.cpp -- 移动构造与移动赋值演示
// Standard: C++17

#include <cstring>
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

下面的演示里就是完整代码，您点「动手试一试」把它跑起来，咱们还可以切到汇编视图，看移动构造的指针转移：

<OnlineCompilerDemo
  title="动手实验：move_semantics_demo.cpp"
  source-path="code/examples/vol2/02_move_semantics.cpp"
  description="在线运行并对比 Buffer 的拷贝构造 vs 移动构造，以及在 vector 中的行为差异。"
  run-options="-O0 -std=c++17"
  allow-run
  allow-x86-asm
/>

把第 2、3 步在内存里的动作做成了动画，您可以播放、暂停，也可以按步进键一步一步地看，把指针交接的那一步看个清楚：

<Anim id="copy-vs-move" />

咱们把"移动构造（指针转移）"和"拷贝构造 X 字节"摆在一起，对比一目了然：拷贝要分配内存再复制数据，移动只是三个指针的赋值。第 5 步的 `vector` 操作还有看头。`push_back` 收到左值的时候，发生的是拷贝。传 `std::move` 包出来的右值，发生的是移动。`emplace_back` 更进一步、直接在 `vector` 的内存里原位构造，连移动都省了。数据量大了之后，三种写法的性能差距会非常明显。

咱们还会注意到，析构的时候没有"释放 0 字节"的输出。那些就是被移动过的对象，它们的 `data_` 已经是 `nullptr`，析构函数里的 `if (data_)` 检查跳过了 `delete[]`。`vector` 里的三个元素各自独立析构：头一个是 `c` 的拷贝（1024 字节），第二个是 `c` 移动过来的（1024 字节），第三个是 `emplace_back` 原位构造的（512 字节）。

移动构造和移动赋值到这里就写齐了。可一个管理资源的类，光有移动还不够——析构函数、拷贝构造、拷贝赋值和移动操作怎么联动，漏掉一个会出什么事？下一篇咱们把“规则五”（Rule of Five）完整过一遍，再看两个配套的写法。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="Move Constructor"
    url="https://en.cppreference.com/w/cpp/language/move_constructor"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="Move Assignment Operator"
    url="https://en.cppreference.com/w/cpp/language/move_assignment"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="std::move"
    chapter="Notes: moved-from state"
    url="https://en.cppreference.com/w/cpp/utility/move"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="std::move_if_noexcept"
    url="https://en.cppreference.com/w/cpp/utility/move_if_noexcept"
  />
  <ReferenceItem
    :id="5"
    author="Howard E. Hinnant, Peter Dimov, Dave Abrahams"
    title="A Proposal to Add Move Semantics Support to the C++ Language (N1377)"
    publisher="WG21 / ISO C++ Committee"
    :year="2002"
    url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2002/n1377.htm"
  />
</ReferenceCard>
