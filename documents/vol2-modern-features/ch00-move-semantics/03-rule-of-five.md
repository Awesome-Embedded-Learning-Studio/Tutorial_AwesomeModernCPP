---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: 规则五的联动规则、copy-and-swap 惯用法，以及只许移动不许拷贝的类型设计
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 0: 移动构造与移动赋值'
reading_time_minutes: 10
related:
- RVO 与 NRVO
- 移动语义实战：标准库容器与性能实测
title: 五个大规则：特殊成员函数的配套关系
tags:
- host
- cpp-modern
- intermediate
- 移动语义
---
# 五个大规则：特殊成员函数的配套关系

上一篇咱们给 `Buffer` 写齐了移动构造和移动赋值。可一个管理资源的类，特殊成员函数不止这两个。我们有**析构函数、拷贝构造、拷贝赋值跟移动操作**。他们是一套联动的机制，动了其中一个，其余的都得跟着配套。这一篇，我们就好好陪他们玩玩！

## 从规则三到规则五

C++ 的老话里有"规则三"（Rule of Three）：咱们要是给一个类自定义了**析构函数、拷贝构造函数、拷贝赋值运算符**里的任何一个，那它八成三个都少不了。C++11 把移动构造函数和移动赋值运算符也加了进来，名单就变成了"规则五"（Rule of Five）<RefLink :id="1" preview="cppreference The rule of three / five / zero" />。

您要是只声明了析构函数、一个移动操作都没声明，编译器是**不会**自动生成移动构造函数和移动赋值运算符的。少了移动构造，`std::move` 交出来的右值还能匹配谁？咱们很容易在这里犯迷糊：明明写了 `std::move`，实际调用的还是拷贝构造函数。`std::move` 本身什么东西都不移动，它只是一个 `static_cast` 到右值引用的类型转换。最终决定调用移动构造还是拷贝构造的，是类的定义。类要是没有移动构造函数的话，右值引用会完美匹配到 `const T&` 的拷贝构造函数上去。

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

这里的后果，比单纯的"低效"还要严重。隐式生成的拷贝构造函数做的是浅拷贝，也就是逐个成员地复制指针，于是 `a` 和 `b` 的 `data_` 会指向同一块内存。两边析构的时候，`delete[]` 被调用了两次，直接触发的就是 double free，也就是同一块内存被释放了两次。咱们可以借 type trait（编译期探测类型性质的工具）把这个行为验证出来<RefLink :id="2" preview="cppreference std::is_move_constructible — true via copy ctor fallback even without a real move ctor" />：

```cpp
static_assert(!std::is_trivially_move_constructible_v<OnlyDestructor>,
              "没有真正的移动构造函数");
static_assert(std::is_move_constructible_v<OnlyDestructor>,
              "但 is_move_constructible 为 true——退回到拷贝构造");
```

看着矛盾？咱们拆开看就不矛盾了。`is_move_constructible` 的 true 是有来头的：编译器可以拿拷贝构造函数来"满足"移动构造的需求，右值本来就是允许绑定到 `const T&` 上的。可这不代表真的有一个移动构造函数在做指针转移。完整的验证代码如下：

```cpp
// rule_of_five_fallback.cpp -- 只有析构函数时，std::move 退化为拷贝构造
// Standard: C++17

#include <iostream>
#include <type_traits>
#include <utility>

// 只定义了析构函数，没有声明任何拷贝/移动操作
class OnlyDestructor
{
    char* data_;

public:
    explicit OnlyDestructor(std::size_t n)
        : data_(new char[n])
    {
    }

    ~OnlyDestructor() { delete[] data_; }
    // 注意：这里既没有声明移动构造，也没有声明拷贝构造
};

// 编译期验证：它"能移动构造"，但不是因为真有移动构造函数
static_assert(!std::is_trivially_move_constructible_v<OnlyDestructor>,
              "没有真正的（平凡的）移动构造函数");
static_assert(std::is_move_constructible_v<OnlyDestructor>,
              "但 is_move_constructible 为 true —— 编译器退回到拷贝构造来满足");

int main()
{
    std::cout << "is_trivially_move_constructible_v: "
              << std::is_trivially_move_constructible_v<OnlyDestructor>
              << "  (没有真正的移动构造)\n";
    std::cout << "is_move_constructible_v:           "
              << std::is_move_constructible_v<OnlyDestructor>
              << "  (但能用拷贝构造蒙混过关)\n";

    // 真要执行 OnlyDestructor b = std::move(a)，隐式拷贝构造做浅拷贝，
    // a 和 b 的 data_ 指向同一块内存，两者析构时 double free。
    // 这里不真的跑（会崩），编译期 static_assert 已经给出结论。
    return 0;
}
```

验证程序也备好了，您点「动手试一试」同样能直接跑：

<OnlineCompilerDemo
  title="动手验证：rule_of_five_fallback.cpp"
  source-path="code/examples/vol2/rule_of_five_fallback.cpp"
  description="在线验证只定义析构函数的类：is_move_constructible 为 true，但 is_trivially_move_constructible 为 0——它没有真正的移动构造。"
  run-options="-O0 -std=c++17"
  allow-run
/>

咱们留意一下，`static_assert` 在编译期就把答案给出来了，运行期的那两行打印，不过是再确认了一遍。您真去执行 `OnlyDestructor b = std::move(a)` 的话，等来的就是前面说过的那次 double free。

对管理资源的类来说，最安全的做法是**五个特殊成员函数要么全部自定义，要么全部 `= default`**<RefLink :id="3" preview="C++ Core Guidelines C.21 — if you define or delete any copy, move, or destructor function, define or delete them all" />。您要是用智能指针来管理资源，通常用 `= default` 让编译器生成正确的版本就够了，这正是现代 C++ 推荐的方式。但像笔者这里手动管理原始指针的类，就必须老老实实地写齐五个——下面的示例把打头的普通构造函数也编了号，真正的五个特殊成员落在编号 2 到 6 上：

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

整段看着是有点长的，其实咱们写来写去是同一个模式：拷贝操作做深拷贝，移动操作做的是指针转移，外加把源对象置空的一步。

## copy-and-swap 惯用法——减少重复代码

您要是觉得拷贝赋值、移动赋值各写一份太啰嗦，有个经典的惯用法能把两份合成一份：**让拷贝赋值和移动赋值共用一个实现**，靠值传递的语义自动选择拷贝还是移动<RefLink :id="1" preview="cppreference The rule of three / five — copy-and-swap idiom example" />。

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

我们要看的，就是在参数的接收方式上：`operator=(Buffer other)` 是按值接收的。您传一个左值进来，`other` 就是用拷贝构造创建出来的。您传一个右值（比如 `std::move(x)`），`other` 就换移动构造来创建了。接下来 `swap` 把 `this` 和 `other` 的内容对调，函数结束的时候 `other` 析构，旧资源就顺手释放掉了。

咱们再回头看这个惯用法，它的优点很实在：代码量少、异常安全，自赋值也自动处理掉了。代价是多一次 `swap` 的操作，对极致性能的场景可能有微小影响。真拿 `-O2` 对比汇编看的话，咱们会发现两条路径的指令数几乎打平，copy-and-swap 多出来的，是 `swap` 带来的几次额外内存读写，而不是指令条数。对管理动态内存的类来说，`new`/`delete` 的开销远大于这点寄存器操作，copy-and-swap 的额外代价在实际中几乎测不出来。

> 咱们把对拍的具体情况放在这儿：copy-and-swap 的 `operator=` 函数体里只剩 `swap`、大约 13 条 `movq`，对应的三个成员各交换一次，`delete` 被推迟到了参数析构才发生。独立移动赋值的 `operator=` 呢，要自己 `delete[]` 旧资源、做自赋值的检查，编译器还会用一条 SSE 的 `movdqu`，把两个 `size_t` 合并成 16 字节一次就搬走了。

## 通用示例——文件句柄的移动

移动语义能管的不只是动态内存，其他资源的所有权一样能转手。咱们看一个最典型的例子：文件句柄。操作系统对同一个文件的打开数量有限制，您要是不小心拷贝了一个持有文件句柄的对象，就可能闹出句柄泄漏的麻烦，或者重复关闭的事故。

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

咱们在这个例子里能看到一个常见的设计取向：**不可拷贝，但移动是允许的**。文件句柄在物理上只有一份而已，"拷贝"出第二份是不应该的——真拷贝了，您手里就会有两个抢着关闭同一个文件的对象。移动就名正言顺了：`open_log` 在函数里创建文件句柄，随后把所有权转交到您手里，函数内部的临时对象不再持有任何资源。

示例也放在下面了，您点「动手试一试」跑一遍，留意收尾的析构输出：

<OnlineCompilerDemo
  title="动手验证：file_handle_move.cpp"
  source-path="code/examples/vol2/16_file_handle_move.cpp"
  description="在线验证文件句柄的移动。跑起来看析构时的输出，数一数「关闭文件」打印了几次。"
  run-options="-O0 -std=c++17"
  allow-run
/>

输出里"关闭文件"只出现了一次，咱们想想为什么。`log` 和 `moved_log` 明明都经历了析构。原因不复杂：`log` 的 `file_` 在移动后被置空了，它析构函数里的 `if (file_)` 检查不通过，自然就不会再去重复关闭了。

到了下一篇，咱们去看编译器在背后帮咱们省下的大头，说的就是返回值优化（RVO）和命名返回值优化（NRVO）。它能让函数返回大对象的代价直接归零。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="The Rule of Three / Five / Zero"
    chapter="Includes the copy-and-swap idiom"
    url="https://en.cppreference.com/w/cpp/language/rule_of_three"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="std::is_move_constructible"
    url="https://en.cppreference.com/w/cpp/types/is_move_constructible"
  />
  <ReferenceItem
    :id="3"
    author="Bjarne Stroustrup / Herb Sutter (eds.)"
    title="C++ Core Guidelines — C.21: If You Define or Delete Any Copy, Move, or Destructor Function, Define or Delete Them All"
    publisher="isocpp.org"
    url="https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rc-five"
  />
</ReferenceCard>
