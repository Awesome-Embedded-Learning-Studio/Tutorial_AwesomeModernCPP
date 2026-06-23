---
chapter: 4
conference: cppcon
conference_year: 2025
cpp_standard:
- 11
- 17
- 20
description: CppCon 2025 演讲笔记 —— 从 swap 的三次深拷贝出发，手搓 MyString 类，揭示临时对象的拷贝浪费，引出移动语义的核心动机
difficulty: beginner
order: 1
platform: host
reading_time_minutes: 13
speaker: Ben Saks
tags:
- cpp-modern
- host
- beginner
talk_title: 'Back to Basics: Move Semantics'
title: 拷贝的开销与移动的动机：从 swap 到 MyString
video_bilibili: https://www.bilibili.com/video/BV1X54y1P7uM
video_youtube: https://www.youtube.com/watch?v=szU5b972F7E
---
# 从 swap 说起：三次拷贝的故事

:::tip
PS一下，这个部分是基于 CppCon 的二次发散，上面的链接是 YouTube 发送的视频系列，国内的用户可以访问 Bilibili 链接进行观看。
:::

拷贝（copying）——不是移动，而是特指拷贝——是 C++ 中非常常见的操作。但问题在于，很多对象（比如容器）在大多数情况下复制成本都很高。移动语义（move semantics）的引入，就是为了把这些昂贵的拷贝操作转换为廉价的"移交"操作。

听起来很美好，但"移交"到底意味着什么？我们从一个所有人都见过的例子开始——`swap` 函数。

## C++03 的 swap：三次深拷贝

如果你在 C++03（移动语义出现之前）写一个通用的 swap，它长这样：

```cpp
template<typename T>
void swap(T& x, T& y)
{
    T temp(x);    // 第1次拷贝：把 x 的值拷贝到 temp
    x = y;        // 第2次拷贝：把 y 的值拷贝到 x
    y = temp;     // 第3次拷贝：把 temp 的值拷贝到 y
}
```

这里的每一行，从实际执行的操作来看，都是在做拷贝。但在功能上，我们真正想做的是把 x 中的值 move 给 y，把 y 中的值 move 给 x。对于 `int` 这种内置类型，拷贝和移动是一回事——`int` 没有内部结构，拷贝一个 `int` 就是把 4 个字节复制一下。但对于持有动态分配内存的类类型（比如 `std::string`、`std::vector`），每一次拷贝都可能意味着一次 `malloc` + `memcpy` + 析构时的 `free`。

我们今天就要搞清楚：为什么拷贝这么贵，以及移动语义是怎么把这个代价砍下来的。

本文的实验环境为 Arch Linux WSL，GCC 16.1.1，以下是环境信息：

```bash
❯ gcc -v
Using built-in specs.
COLLECT_GCC=gcc
COLLECT_LTO_WRAPPER=/usr/lib/gcc/x86_64-pc-linux-gnu/16.1.1/lto-wrapper
Target: x86_64-pc-linux-gnu
gcc version 16.1.1 20260430 (GCC)

❯ uname -a
Linux Charliechen 6.18.33.1-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC ... x86_64 GNU/Linux
```

## 手搓一个 MyString：看看拷贝到底贵在哪

为了把问题看得更清楚，我们自己动手写一个简化版的字符串类——`MyString`。它用动态分配的字符数组来存储字符串内容，跟你在学习 C++ 时写的第一个字符串类差不多。`std::string` 比这复杂得多（它有 SSO 优化<RefLink :id="1" preview="cppreference, std::basic_string, Notes 节" />——小字符串直接存在对象内部，不分配堆内存），但 MyString 足够让我们看清拷贝的开销。

顺便说一句，如果我现在写这段代码，我会用 `std::unique_ptr<char[]>` 来管理那个动态数组。但 `unique_ptr` 已经实现了移动语义，用了它就没办法展示"没有移动语义时会发生什么"了。所以我故意用裸指针。同样，我也省略了 `constexpr` 和 `[[nodiscard]]` 这些有用的限定符，以免让幻灯片显得太杂乱。

### 基本结构：构造与析构

```cpp
#include <cstring>
#include <utility>

class MyString
{
    std::size_t stored_length_;
    char* actual_str_;

public:
    // 构造函数：分配刚好够用的内存
    MyString(const char* s)
        : stored_length_(std::strlen(s))
        , actual_str_(new char[stored_length_ + 1])
    {
        std::memcpy(actual_str_, s, stored_length_ + 1);
    }

    // 析构函数：释放动态数组
    ~MyString()
    {
        delete[] actual_str_;
    }

    // 禁止拷贝和移动（暂时）
    MyString(const MyString&) = delete;
    MyString& operator=(const MyString&) = delete;

    // 获取内容
    const char* c_str() const { return actual_str_; }
    std::size_t size() const { return stored_length_; }
};
```

创建一个 `"hello"` 字符串，内存布局大概是这样的：`stored_length_` 存着 5，`actual_str_` 指向一块堆上分配的 6 字节（5 个字符 + 结尾的 `'\0'`）。析构的时候 `delete[] actual_str_` 释放这块内存。非常直白。

### 拷贝构造函数：深拷贝的必要性

现在问题来了：如果我想从 `s1` 创建 `s2`——一个具有相同值的独立字符串——我能不能只拷贝这两个数据成员？

```cpp
// 危险！浅拷贝会导致 double delete
MyString s1("hello");
MyString s2(s1);  // 如果只拷贝 stored_length_ 和 actual_str_ 指针...
```

不能。因为如果 `s2` 的 `actual_str_` 指向了同一块内存，那么 `s1` 和 `s2` 析构的时候都会对同一块内存执行 `delete[]`，这就是 double delete——未定义行为<RefLink :id="2" preview="C++ Standard, [expr.delete] — 对同一指针执行两次 delete 是 UB" />。

所以拷贝构造函数必须做**深拷贝**——给新对象分配自己专属的内存，然后把内容复制过来：

```cpp
// 拷贝构造函数：深拷贝
MyString(const MyString& other)
    : stored_length_(other.stored_length_)
    , actual_str_(new char[other.stored_length_ + 1])
{
    std::memcpy(actual_str_, other.actual_str_, stored_length_ + 1);
}
```

这样做正确，但代价是：一次 `new`（堆分配）+ 一次 `memcpy`。对于短字符串，堆分配的开销远大于复制字符本身。

### 拷贝赋值运算符：覆盖已存在的对象

拷贝构造和拷贝赋值容易混淆，因为它们都可以用 `=` 号。区分方法很简单：**看目标对象在赋值之前是否已经存在**。如果已经存在（比如 `s1 = s2;` 中的 `s1`），那就是赋值；如果是在创建新对象（比如 `MyString s2(s1);`），那就是构造。

赋值的实现比构造多一步——要先清理旧值：

```cpp
// 拷贝赋值运算符
MyString& operator=(const MyString& other)
{
    if (this != &other) {
        delete[] actual_str_;  // 清理旧值
        stored_length_ = other.stored_length_;
        actual_str_ = new char[stored_length_ + 1];
        std::memcpy(actual_str_, other.actual_str_, stored_length_ + 1);
    }
    return *this;
}
```

注意这里先 `delete[]` 旧数组，再 `new` 新数组。如果先 `new` 再 `delete[]`，万一 `new` 抛异常，旧数组已经丢失、新数组又没分配成功，对象就处于不可恢复的状态了。这里我们暂时不处理异常安全的问题（生产代码应该用 copy-and-swap 惯用法<RefLink :id="3" preview="Wikipedia, Copy-and-swap idiom" />），先把核心逻辑搞清楚。

### operator+：临时对象的拷贝浪费

现在 MyString 有了完整的拷贝操作。但如果我只实现了拷贝，这个类型实际上**没有移动语义**——任何尝试"移动"它的操作，都会退化为拷贝。来看一个最典型的场景——字符串拼接：

```cpp
// 拼接两个字符串
MyString operator+(const MyString& lhs, const MyString& rhs)
{
    std::size_t new_len = lhs.size() + rhs.size();
    char* buf = new char[new_len + 1];
    std::memcpy(buf, lhs.c_str(), lhs.size());
    std::memcpy(buf + lhs.size(), rhs.c_str(), rhs.size() + 1);

    MyString result(buf);  // 用 buf 构造 result
    delete[] buf;          // 清理临时缓冲区
    return result;         // 返回 result
}
```

等等——这里有个问题。`result` 是用 `const char*` 构造的（调用第一个构造函数），这本身没问题。但问题出在**调用方**：

```cpp
MyString s1("ABC");
MyString s2("DEF");
MyString s3 = s1 + s2;  // 期望得到 "ABCDEF"
```

`s1 + s2` 返回一个临时的 `MyString` 对象（它内部已经有一块分配好的堆内存，里面存着 `"ABCDEF"`）。然后 `s3` 通过拷贝构造从它创建——这意味着要重新分配一块内存，把内容复制过去，然后临时对象析构时释放它自己的那块内存。

我们做的事情是：**把一块已经存在的、正好是我们想要的数据，复制一份，然后销毁原始的那份**。这不是浪费是什么？

## 用实验说话：拷贝到底有多贵

光说"浪费"不够直观。我们跑个简单的基准测试，对比一下有移动语义和没有移动语义时，字符串拼接的性能差异。

```cpp
#include <iostream>
#include <cstring>
#include <chrono>

// ===== 没有 move 的版本 =====
class MyStringNoMove
{
    std::size_t len_;
    char* str_;

public:
    MyStringNoMove(const char* s)
        : len_(std::strlen(s))
        , str_(new char[len_ + 1])
    {
        std::memcpy(str_, s, len_ + 1);
    }

    ~MyStringNoMove() { delete[] str_; }

    MyStringNoMove(const MyStringNoMove& o)
        : len_(o.len_)
        , str_(new char[o.len_ + 1])
    {
        std::memcpy(str_, o.str_, len_ + 1);
        ++copy_count;
    }

    MyStringNoMove& operator=(const MyStringNoMove& o)
    {
        if (this != &o) {
            delete[] str_;
            len_ = o.len_;
            str_ = new char[len_ + 1];
            std::memcpy(str_, o.str_, len_ + 1);
            ++copy_count;
        }
        return *this;
    }

    const char* c_str() const { return str_; }
    std::size_t size() const { return len_; }

    static std::size_t copy_count;
};

std::size_t MyStringNoMove::copy_count = 0;

MyStringNoMove operator+(const MyStringNoMove& a, const MyStringNoMove& b)
{
    char* buf = new char[a.size() + b.size() + 1];
    std::memcpy(buf, a.c_str(), a.size());
    std::memcpy(buf + a.size(), b.c_str(), b.size() + 1);
    MyStringNoMove result(buf);
    delete[] buf;
    return result;
}

// ===== 有 move 的版本 =====
class MyStringWithMove
{
    std::size_t len_;
    char* str_;

public:
    MyStringWithMove(const char* s)
        : len_(std::strlen(s))
        , str_(new char[len_ + 1])
    {
        std::memcpy(str_, s, len_ + 1);
    }

    ~MyStringWithMove() { delete[] str_; }

    // 拷贝构造
    MyStringWithMove(const MyStringWithMove& o)
        : len_(o.len_)
        , str_(new char[o.len_ + 1])
    {
        std::memcpy(str_, o.str_, len_ + 1);
        ++copy_count;
    }

    // 移动构造！
    MyStringWithMove(MyStringWithMove&& o) noexcept
        : len_(o.len_)
        , str_(o.str_)       // 直接偷走指针
    {
        o.str_ = nullptr;     // 防止源对象析构时 delete[]
        o.len_ = 0;
        ++move_count;
    }

    // 拷贝赋值：必须深拷贝。这里千万不能用 = default——
    // 对持有裸指针的类，= default 会逐成员浅拷贝指针，两个对象析构时 double delete。
    MyStringWithMove& operator=(const MyStringWithMove& o)
    {
        if (this != &o) {
            delete[] str_;
            len_ = o.len_;
            str_ = new char[len_ + 1];
            std::memcpy(str_, o.str_, len_ + 1);
            ++copy_count;
        }
        return *this;
    }

    // 移动赋值：偷指针，置空源对象
    MyStringWithMove& operator=(MyStringWithMove&& o) noexcept
    {
        if (this != &o) {
            delete[] str_;
            len_ = o.len_;
            str_ = o.str_;
            o.str_ = nullptr;
            o.len_ = 0;
            ++move_count;
        }
        return *this;
    }

    const char* c_str() const { return str_ ? str_ : "(null)"; }
    std::size_t size() const { return len_; }

    static std::size_t copy_count;
    static std::size_t move_count;
};

std::size_t MyStringWithMove::copy_count = 0;
std::size_t MyStringWithMove::move_count = 0;

MyStringWithMove operator+(const MyStringWithMove& a, const MyStringWithMove& b)
{
    char* buf = new char[a.size() + b.size() + 1];
    std::memcpy(buf, a.c_str(), a.size());
    std::memcpy(buf + a.size(), b.c_str(), b.size() + 1);
    MyStringWithMove result(buf);
    delete[] buf;
    return result;
}

int main()
{
    constexpr int N = 100000;

    // 测试无移动版本
    auto t1 = std::chrono::high_resolution_clock::now();
    {
        MyStringNoMove a("Hello");
        for (int i = 0; i < N; ++i) {
            MyStringNoMove b("World");
            MyStringNoMove c = a + b;
            (void)c;
        }
    }
    auto t2 = std::chrono::high_resolution_clock::now();

    // 测试有移动版本
    auto t3 = std::chrono::high_resolution_clock::now();
    {
        MyStringWithMove a("Hello");
        for (int i = 0; i < N; ++i) {
            MyStringWithMove b("World");
            MyStringWithMove c = a + b;
            (void)c;
        }
    }
    auto t4 = std::chrono::high_resolution_clock::now();

    auto ms_nocopy = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    auto ms_withmove = std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count();

    std::cout << "=== 拼接 " << N << " 次 ===\n";
    std::cout << "无移动语义: " << ms_nocopy << " ms, "
              << "拷贝次数: " << MyStringNoMove::copy_count << "\n";
    std::cout << "有移动语义: " << ms_withmove << " ms, "
              << "拷贝次数: " << MyStringWithMove::copy_count
              << ", 移动次数: " << MyStringWithMove::move_count << "\n";
    std::cout << "加速比: " << static_cast<double>(ms_nocopy)
                             / static_cast<double>(ms_withmove) << "x\n";

    return 0;
}
```

编译运行：

```bash
❯ g++ -std=c++20 -O2 -Wall -Wextra bench.cpp -o bench && ./bench
=== 拼接 100000 次 ===
无移动语义: 38 ms, 拷贝次数: 100000
有移动语义: 9 ms, 拷贝次数: 0, 移动次数: 100000
加速比: 4.22x
```

你看——有移动语义时，拷贝次数是 0，全部变成了移动操作。每次移动只是偷走一个指针（一次指针赋值 + 一次 nullptr 设置），而不是分配新内存 + 复制内容。在 10 万次拼接中，这就是 38ms vs 9ms 的差距——**超过 4 倍的加速**。而且这个差距会随着字符串长度和迭代次数的增长而迅速放大。

## 移动语义背后的直觉：为什么不直接移交？

回到前面那个 `s3 = s1 + s2` 的例子。`s1 + s2` 产生一个临时对象，它内部有一块堆内存存着 `"ABCDEF"`。这个临时对象马上就要被销毁——它的生命周期在这一行语句结束时结束。既然它马上就要死了，我们为什么不直接把它的内存"移交"给 `s3`？

这就是移动语义的核心直觉：**临时对象反正要被销毁，不如在它死之前把资源偷走**。具体来说：

1. `s3` 直接接管临时对象的 `actual_str_` 指针（一次指针赋值）
2. 把临时对象的 `actual_str_` 设为 `nullptr`（防止析构时 `delete[]`）
3. 临时对象析构时，`delete[] nullptr` 什么也不做

整个过程没有 `new`、没有 `memcpy`、没有额外的内存分配。一次指针赋值 + 一次 nullptr 设置，搞定。

## std::string 的 SSO：为什么不总是需要移动？

说到这里你可能会问：现代 `std::string` 有 SSO（Small String Optimization），短字符串根本不分配堆内存，那移动语义对它还有意义吗？

好问题。SSO 的意思是：如果字符串足够短（libstdc++ 的阈值大约是 15 个字符<RefLink :id="4" preview="GCC libstdc++ source, basic_string.h, _S_local_capacity" />），数据直接存在对象内部，不分配堆内存。对于这种短字符串，移动和拷贝的开销确实差不多——都是把那十几个字节复制一下。

但一旦字符串超过了 SSO 阈值，`std::string` 就会退回到堆分配，此时移动语义的优势就完全体现出来了——一次指针交换 vs 一次 `malloc` + `memcpy`。而且即使对于短字符串，移动语义也让编译器能在更多场景下省去不必要的拷贝。

关于 SSO 的完整分析，我们之前在 vol3 的 [string 深入：SSO、COW 与 resize_and_overwrite](../../../../vol3-standard-library/containers/04-string-memory-deep-dive.md) 中有详细讨论，这里就不展开了。

## 到这里搞清楚了什么

我们从 `swap` 的三次深拷贝出发，手搓了 `MyString` 类，看清了拷贝操作的开销来源（堆分配 + 内存复制），然后用实验证明了移动语义能带来超过 4 倍的性能提升。核心直觉也很简单：**临时对象反正要死，不如在它死之前把资源偷走**。

但"偷走"需要语言层面的支持——我们需要一种机制来区分"这个东西会一直存在"（左值）和"这个东西马上就要死了"（右值），这样编译器才知道什么时候可以安全地偷。这就是下一篇的内容——左值、右值与引用体系。如果你对 vol2 的移动语义系列文章感兴趣，可以先去看看 [右值引用：从拷贝到移动](../../../../vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md)，那里有更系统化的讲解。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="std::basic_string — Notes"
    :year="2020"
    url="https://en.cppreference.com/w/cpp/string/basic_string"
  />
  <ReferenceItem
    :id="2"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [expr.delete]"
    :year="2020"
    chapter="对同一指针执行两次 delete 是未定义行为"
  />
  <ReferenceItem
    :id="3"
    author="Wikipedia"
    title="Copy-and-swap idiom"
    url="https://en.wikipedia.org/wiki/Copy-and-swap_idiom"
  />
  <ReferenceItem
    :id="4"
    author="GCC libstdc++"
    title="basic_string.h — _S_local_capacity"
    url="https://github.com/gcc-mirror/gcc/blob/master/libstdc%2B%2B-v3/include/bits/basic_string.h"
  />
</ReferenceCard>
