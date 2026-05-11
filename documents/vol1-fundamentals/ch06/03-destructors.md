---
title: "析构函数与资源管理"
description: "理解析构函数的调用时机，初步认识 RAII 原则和 Rule of Three 的设计思路"
chapter: 6
order: 3
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
  - "构造函数"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# 析构函数与资源管理

构造函数负责把对象带入一个合法的状态——分配内存、打开文件、初始化硬件。但所有这些资源都有一个共同的问题：它们必须在某个时刻被归还。`malloc` 了不 `free`、`fopen` 了不 `fclose`、锁了互斥量不解锁——程序就会慢慢泄漏资源，最终耗尽系统配额或者陷入死锁。

C++ 解决这个问题的手段是析构函数（destructor）。构造函数和析构函数形成了一组完美的对称：一个在对象诞生时自动执行，另一个在对象死亡时自动执行。这种"构造时获取、析构时释放"的模式有一个响亮的名字——RAII，它是 C++ 资源管理的基石。

这一章我们把析构函数从头到尾拆清楚——语法、调用时机、RAII 的核心思想，以及一个在设计上绕不开的经典准则：Rule of Three。

## 析构函数的语法

析构函数的声明方式非常简单：在类名前面加一个波浪号 `~`，没有参数，没有返回类型。一个类只能有一个析构函数，不支持重载。

```cpp
class FileWriter {
private:
    FILE* file_handle;

public:
    FileWriter(const char* path, const char* mode)
        : file_handle(std::fopen(path, mode))
    {
        if (file_handle == nullptr) {
            std::cerr << "Failed to open: " << path << std::endl;
        }
    }

    ~FileWriter() {
        if (file_handle != nullptr) {
            std::fclose(file_handle);
            std::cout << "File closed by destructor" << std::endl;
        }
    }

    void write(const char* data) {
        if (file_handle) { std::fputs(data, file_handle); }
    }
};
```

析构函数不能接受参数，不可能重载；也没有返回值。这些限制很好理解——析构函数由运行时自动调用，调用者不需要传递任何东西。

如果你没定义析构函数，编译器会生成一个默认版本，按成员声明的逆序析构非静态成员。只包含基本类型的类不需要手写析构，但如果类管理了外部资源——动态内存、文件句柄、网络连接——你就必须自己写析构函数来释放它们。（这很正常，因为编译器也不知道你要如何析构你的资源）

## 析构函数什么时候被调用

理解调用时机是正确使用 RAII 的前提。**栈对象**在离开作用域时自动析构，无论正常返回、提前 `return` 还是异常展开（stack unwinding）：

```cpp
void process() {
    FileWriter writer("log.txt", "w");
    writer.write("Processing started\n");
}   // writer 在这里析构，文件自动关闭
```

**堆对象**只有显式 `delete` 时才析构——这是 C++ 资源泄漏的主要来源之一：

```cpp
void leaky() {
    FileWriter* writer = new FileWriter("log.txt", "w");
    writer->write("Oops\n");
    // 忘了 delete —— 析构不调用，文件永远不会关闭
}
```

> **踩坑预警**：`new` 出来的对象忘了 `delete`，析构函数永远不会执行。即使你记住了在正常路径上 `delete`，只要中间抛了异常，`delete` 就会被跳过。现代 C++ 强烈建议用智能指针或栈对象代替裸 `new`/`delete`。

**成员对象**的析构发生在包含类的析构函数体执行完毕之后，顺序与构造严格相反。我们写一个小程序来验证：

```cpp
#include <iostream>

struct Tracer {
    const char* name;
    explicit Tracer(const char* n) : name(n) {
        std::cout << "  [" << name << "] constructed" << std::endl;
    }
    ~Tracer() {
        std::cout << "  [" << name << "] destructed" << std::endl;
    }
};

struct Container {
    Tracer member_a;
    Tracer member_b;
    Container() : member_a("member_a"), member_b("member_b") {
        std::cout << "  [Container] ctor body" << std::endl;
    }
    ~Container() {
        std::cout << "  [Container] dtor body" << std::endl;
    }
};

int main() {
    std::cout << "=== begin ===" << std::endl;
    {
        Tracer local("local");
        Container container;
        Tracer* heap = new Tracer("heap");
        delete heap;
    }
    std::cout << "=== end ===" << std::endl;
}
```

运行输出：

```text
=== begin ===
  [local] constructed
  [member_a] constructed
  [member_b] constructed
  [Container] ctor body
  [heap] constructed
  [heap] destructed
  [Container] dtor body
  [member_b] destructed
  [member_a] destructed
  [local] destructed
=== end ===
```

构造是 `local -> member_a -> member_b -> Container body`，析构严格反过来——"后构造的先析构"保证了资源以正确的层次被释放。

## RAII——C++ 资源管理的核心思想

RAII 全称 Resource Acquisition Is Initialization，核心理念就一句话：**把资源的生命周期绑定到对象的生命周期上**。构造时获取资源，析构时释放资源。因为析构函数在对象离开作用域时一定会被调用（即使发生异常），资源一定会被正确释放。

我们来看一个实用的例子——测量代码块执行时间的 `ScopedTimer`：

```cpp
#include <chrono>
#include <iostream>

class ScopedTimer {
private:
    const char* label_;
    std::chrono::steady_clock::time_point start_;

public:
    explicit ScopedTimer(const char* label)
        : label_(label), start_(std::chrono::steady_clock::now())
    {
        std::cout << "[" << label_ << "] started" << std::endl;
    }

    ~ScopedTimer() {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_);
        std::cout << "[" << label_ << "] elapsed: "
                  << us.count() << " us" << std::endl;
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
};

void heavy_computation() {
    ScopedTimer timer("heavy_computation");
    for (int i = 0; i < 1000000; ++i) {
        volatile int x = i * i;
    }
}  // timer 在这里析构，自动打印耗时
```

你不需要记住在函数末尾"停止计时器"——析构函数自动替你完成。多条 `return` 路径、异常——每一条路径上计时器都会被正确销毁。这就是 RAII 的威力：**它让"不漏"成为默认行为，而不是需要靠纪律维持的"记住去做"**。

> **踩坑预警**：RAII 的前提是对象必须活在栈上（或者全局/静态对象），而不是裸 `new` 出来的堆对象。如果你 `new` 了一个 RAII 对象却忘了 `delete`，析构函数照样不会调用——RAII 救不了你。现代 C++ 的建议是：**尽量让对象活在栈上**，如果必须用堆，就用智能指针。

## Rule of Three——一个设计预警信号

Rule of Three（三之法则）是一个经典的设计准则：**如果你的类需要自定义以下三者中的任何一个，你几乎一定需要同时自定义另外两个**——析构函数、拷贝构造函数、拷贝赋值运算符。

这三个函数共同决定了对象"怎么被复制"和"怎么被销毁"。写了析构函数通常意味着类管理着需要手动释放的资源，而编译器默认生成的拷贝操作只做浅拷贝——指针成员被复制后，两个对象指向同一块资源，析构时 double free。

```cpp
class NaiveBuffer {
    int* data_;
    std::size_t size_;
public:
    explicit NaiveBuffer(std::size_t n) : size_(n), data_(new int[n]()) {}
    ~NaiveBuffer() { delete[] data_; }
    // 没有自定义拷贝——编译器生成的版本做浅拷贝
};

void bug_demo() {
    NaiveBuffer a(10);
    NaiveBuffer b = a;  // 浅拷贝：b.data_ == a.data_
    // 作用域结束时 double free —— 未定义行为！
}
```

修复方法之一是直接禁止拷贝：

```cpp
class SafeBuffer {
    int* data_;
    std::size_t size_;
public:
    explicit SafeBuffer(std::size_t n) : size_(n), data_(new int[n]()) {}
    ~SafeBuffer() { delete[] data_; }
    SafeBuffer(const SafeBuffer&) = delete;
    SafeBuffer& operator=(const SafeBuffer&) = delete;
};
```

这里我们先预览一下概念。等到讲完移动语义之后，Rule of Three 会扩展成 Rule of Five。目前你只需要记住：**一旦手写了析构函数，就停下来想一下——你的类能不能被安全地拷贝？如果不能，就把它删掉**。

## 虚析构函数——多态的隐形陷阱

如果类会被继承，而且使用者通过基类指针操作派生类对象，那么基类的析构函数必须是 `virtual` 的。否则 `delete` 基类指针时，派生类的析构函数会被完全跳过。

```cpp
class Base {
public:
    ~Base() { std::cout << "~Base" << std::endl; }  // 非 virtual！
};

class Derived : public Base {
    int* resource_;
public:
    Derived() : resource_(new int[100]) {}
    ~Derived() { delete[] resource_; std::cout << "~Derived" << std::endl; }
};

void leak_demo() {
    Base* ptr = new Derived();
    delete ptr;  // 只调用 ~Base()，~Derived() 被跳过 → 内存泄漏
}
```

输出只有 `~Base`——`resource_` 指向的 400 字节内存悄无声息地泄漏了。修复只需在基类析构函数前加 `virtual`：

```cpp
class Base {
public:
    virtual ~Base() { std::cout << "~Base" << std::endl; }
};
```

> **踩坑预警**：这条规则的适用条件是类会被当作多态基类使用。安全经验是：**只要你的类有 `virtual` 函数，析构函数就应该是 `virtual` 的**。反过来，没有 `virtual` 函数的类不需要虚析构——加了反而给每个对象增加虚函数表指针的开销。这个话题在下一章讲继承和多态时会深入展开。

## 实战：析构函数在行动

现在我们动手写一段完整代码，把 `ScopedTimer` 和 `FileWriter` 串起来演示 RAII 的实际效果：

```cpp
// destructor.cpp
// 编译：g++ -std=c++17 -o destructor destructor.cpp

#include <chrono>
#include <cstdio>
#include <iostream>

/// @brief 作用域计时器
class ScopedTimer {
    const char* label_;
    std::chrono::steady_clock::time_point start_;
public:
    explicit ScopedTimer(const char* label)
        : label_(label), start_(std::chrono::steady_clock::now())
    { std::cout << "[" << label_ << "] started" << std::endl; }

    ~ScopedTimer() {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_);
        std::cout << "[" << label_ << "] finished: "
                  << us.count() << " us" << std::endl;
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
};

/// @brief 自动管理 FILE* 的文件写入器
class FileWriter {
    FILE* handle_;
    const char* path_;
public:
    FileWriter(const char* path, const char* mode)
        : handle_(std::fopen(path, mode)), path_(path)
    {
        if (!handle_) std::cerr << "Error: cannot open " << path << std::endl;
    }

    ~FileWriter() {
        if (handle_) {
            std::fclose(handle_);
            std::cout << "[" << path_ << "] closed" << std::endl;
        }
    }

    void write_line(const char* text) {
        if (handle_) { std::fputs(text, handle_); std::fputc('\n', handle_); }
    }

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;
};

int main() {
    std::cout << "--- RAII demo ---" << std::endl;
    ScopedTimer total("total");

    {
        ScopedTimer phase("phase 1: file writing");
        FileWriter writer("raii_demo.txt", "w");
        writer.write_line("Hello from RAII!");
        writer.write_line("No manual fclose needed.");
    }

    {
        ScopedTimer phase("phase 2: computation");
        volatile int sum = 0;
        for (int i = 0; i < 1000000; ++i) { sum += i; }
    }

    std::cout << "--- end of main ---" << std::endl;
    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -o destructor destructor.cpp && ./destructor
```

输出：

```text
--- RAII demo ---
[total] started
[phase 1: file writing] started
[raii_demo.txt] closed
[phase 1: file writing] finished: 123 us
[phase 2: computation] started
[phase 2: computation] finished: 4567 us
--- end of main ---
[total] finished: 4789 us
```

内层的 `ScopedTimer` 和 `FileWriter` 先析构，外层 `total` 最后析构。你可以验证文件内容：

```bash
cat raii_demo.txt
# Hello from RAII!
# No manual fclose needed.
```

内容正确，我们没有手写 `fclose`——析构函数替我们完成了全部清理。

## 练习

**练习 1：作用域日志计时器**。写一个 `ScopedLogger` 类，构造时记录时间戳（格式 `HH:MM:SS`），析构时打印"elapsed X seconds"。提示：使用 `<ctime>` 中的 `std::time` 和 `std::localtime`。

**练习 2：简易文件句柄**。实现一个 `FileHandle` 类，构造时打开文件，析构时自动关闭。提供 `read_line()` 方法（返回 `std::string`）和 `is_valid()` 方法。用 Rule of Three 的思路想想：这个类需要禁用拷贝吗？为什么？

## 小结

这一章我们围绕析构函数，把语法、调用时机以及在资源管理中的核心地位过了一遍。析构函数在对象离开作用域或被 `delete` 时自动调用。RAII 把资源的获取和释放绑定到对象生命周期上，让"不泄漏"成为默认行为。Rule of Three 提醒我们：手写析构函数时要重新审视拷贝语义。虚析构函数是多态场景下的硬性要求。

下一篇我们来看类的另一个重要机制——静态成员。
