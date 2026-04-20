---
title: "动态内存管理"
description: "掌握 new/delete 使用与陷阱，理解 RAII 核心地位"
chapter: 12
order: 2
difficulty: intermediate
reading_time_minutes: 12
platform: host
prerequisites:
  - "内存布局"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
---

# 动态内存管理

上一章我们把程序的内存空间拆成了栈、堆、静态区、代码段四大块，搞清楚了数据"住在哪里"和"活多久"。但有一个悬念没有展开：堆上的动态内存到底怎么管？`new` 和 `delete` 背后做了什么？为什么前面几乎所有章节都在念叨"用智能指针，别裸写 `delete`"？

这一章我们来正面回答。动态内存是 C++ 给我们的最大自由度——你可以在运行时按需申请任意大小的内存，完全不受栈空间限制。但这份自由也带来了最沉重的责任：每一块 `new` 出来的内存都必须被正确地 `delete`，否则就是泄漏；每一次 `delete` 都必须对应正确的 `new`，否则就是未定义行为。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 正确使用 `new`/`delete` 和 `new[]`/`delete[]`，避免不匹配错误
> - [ ] 用 AddressSanitizer 检测内存泄漏
> - [ ] 理解 RAII 如何将堆资源绑定到栈对象的生命周期上
> - [ ] 熟练使用 `unique_ptr`、`shared_ptr`、`weak_ptr` 及其工厂函数
> - [ ] 了解 `placement new` 的存在与适用场景

## 从 new/delete 说起

C++ 用 `new` 和 `delete` 替代了 C 的 `malloc` 和 `free`。简单来说，`new` 是 `malloc` 加上构造函数调用的封装；`delete` 则先调用析构函数，再回收内存。这个区别正是 C++ 动态内存管理与 C 的根本分水岭。

分配单个对象时，对于类类型，`new` 会自动调用构造函数，`delete` 会自动调用析构函数：

```cpp
class Sensor {
public:
    Sensor()  { std::cout << "Sensor 初始化\n"; }
    ~Sensor() { std::cout << "Sensor 关闭\n"; }
    void read() { std::cout << "读取数据\n"; }
};

Sensor* s = new Sensor();  // 输出: Sensor 初始化
s->read();                  // 输出: 读取数据
delete s;                   // 输出: Sensor 关闭
```

分配数组时必须用 `new[]`，释放时必须用对应的 `delete[]`：

```cpp
int* arr = new int[10];
for (int i = 0; i < 10; ++i) {
    arr[i] = i * i;
}
delete[] arr;  // 注意：是 delete[]，不是 delete
```

> **踩坑预警**：`delete` 和 `delete[]` 不匹配是经典中的经典错误。用 `delete` 去释放 `new[]` 分配的数组，行为是未定义的。对于 `int` 这类基本类型，某些平台可能"碰巧"不出问题；但对于类类型的数组，`delete`（不带 `[]`）只会调用第一个元素的析构函数，其余元素的析构函数根本不会被调用——如果析构函数负责释放嵌套的动态内存，后果就是资源泄漏。养成铁律一般的习惯：`new` 对 `delete`，`new[]` 对 `delete[]`，宁可多写一个 `[]`，也不要心存侥幸。

## 内存泄漏——沉默的杀手

内存泄漏到底有多阴险？来看一个最简单的场景：

```cpp
void leak_example()
{
    int* p = new int(42);
    if (some_condition()) {
        return;  // 提前返回，delete 永远不会执行
    }
    delete p;
}
```

函数在中途 `return` 了，`delete` 被跳过，那 4 个字节的内存就永远丢失了。但更阴险的场景是异常：如果代码在 `new` 和 `delete` 之间抛出了异常，控制流直接跳转到 `catch` 块，`delete` 被完全绕过。这种泄漏在测试阶段往往不暴露，但在生产环境中，某个罕见条件触发异常，内存就开始一点一点流失。

### 用 AddressSanitizer 抓泄漏

好消息是，现代编译器提供了强大的运行时检测工具。AddressSanitizer（ASan）是 GCC 和 Clang 内置的内存错误检测器，编译时加上 `-fsanitize=address` 就能自动检测泄漏、越界、use-after-free 等问题。

```cpp
// leak_demo.cpp
// 编译: g++ -std=c++17 -O0 -fsanitize=address -g leak_demo.cpp
#include <iostream>

void create_leak()
{
    int* p = new int(42);
    std::cout << "分配了内存，值为: " << *p << "\n";
    // 故意不 delete
}

int main()
{
    create_leak();
    std::cout << "函数返回了，但内存没有释放\n";
    return 0;
}
```

编译运行后，ASan 在程序退出时报告：

```text
=================================================================
==12345==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 4 byte(s) in 1 object(s) allocated from:
    #0 0x401234 in operator new(unsigned long)
    #1 0x401156 in create_leak() leak_demo.cpp:7
    #2 0x401178 in main leak_demo.cpp:14

SUMMARY: AddressSanitizer: 4 byte(s) leaked in 1 allocation(s).
=================================================================
```

> **踩坑预警**：ASan 会显著降低程序运行速度（通常慢 2-5 倍）并增加内存占用（大约 3-5 倍），所以只应在调试和测试阶段使用。生产构建中一定要去掉 `-fsanitize=address`。此外，ASan 与某些并行调试工具可能冲突，如果遇到奇怪的段错误，试试去掉 ASan 看看是否是工具本身的问题。

## RAII 把堆资源绑到栈上

裸用 `new`/`delete` 的核心问题在于：你必须手动保证每一块内存都被恰好释放一次，无论是正常返回、提前 `return` 还是异常退出。C++ 给出的答案是 RAII——Resource Acquisition Is Initialization。核心思路就是把堆资源的生命周期绑定到一个栈对象上：在构造函数里 `new`，在析构函数里 `delete`，利用栈离开作用域时析构函数自动调用的机制来保证释放。

```cpp
class AutoInt {
public:
    explicit AutoInt(int value) : ptr_(new int(value)) {}
    ~AutoInt() {
        delete ptr_;
        std::cout << "AutoInt 析构，内存已释放\n";
    }

    // 禁止拷贝（后面会解释原因）
    AutoInt(const AutoInt&) = delete;
    AutoInt& operator=(const AutoInt&) = delete;

    int& operator*() { return *ptr_; }
private:
    int* ptr_;
};

void safe_function()
{
    AutoInt value(42);
    std::cout << *value << "\n";
    risky_operation();  // 即使这里抛出异常
    // 析构函数也会在栈展开时被自动调用
}
```

`AutoInt` 的析构函数保证了 `delete` 一定会被执行——不管 `safe_function` 是正常返回还是因为异常退出。但现实中我们不会为每种类型都手写一个 `AutoXxx` 包装类，标准库已经替我们做好了，而且做得更完善。这就是智能指针。

## 智能指针——RAII 的标准答案

C++11 引入了三种智能指针，全部定义在 `<memory>` 头文件中，分别对应不同的所有权语义。

### unique_ptr——独占所有权

`std::unique_ptr` 表达的是"唯一所有权"：一块内存同一时刻只能被一个 `unique_ptr` 持有。它不可拷贝，但可以移动——通过 `std::move` 把所有权从一个 `unique_ptr` 转移到另一个：

```cpp
auto p = std::make_unique<int>(42);   // C++14 的 make_unique
std::cout << *p << "\n";              // 42

// auto p2 = p;                       // 编译错误！unique_ptr 不可拷贝
auto p2 = std::move(p);              // OK：所有权转移，p 变为 nullptr
std::cout << *p2 << "\n";            // 42
// 离开作用域，p2 析构，内存自动释放
```

`std::make_unique`（C++14）比直接 `std::unique_ptr<int>(new int(42))` 更安全——它将分配和构造合并在一个不可中断的步骤中，避免边缘情况下的泄漏。C++11 项目可以直接写 `std::unique_ptr<int>(new int(42))`。

`unique_ptr` 还支持自定义删除器和数组版本。自定义删除器让你在释放内存时执行自定义操作，这在嵌入式开发中非常有用——比如把内存归还给内存池而不是标准堆：

```cpp
auto pool_deleter = [](int* p) {
    std::cout << "归还到内存池\n";
    ::operator delete(p);
};
std::unique_ptr<int, decltype(pool_deleter)> p(new int(42), pool_deleter);
// p 析构时，pool_deleter 被调用，而不是默认的 delete
```

数组版本则替代 `new[]`/`delete[]`：`auto arr = std::make_unique<int[]>(10);` 会自动提供 `operator[]`，离开作用域自动调用 `delete[]`。

### shared_ptr——共享所有权

`std::shared_ptr` 允许多个指针共享同一块内存的所有权。内部通过引用计数追踪——每拷贝一次加一，每销毁一个减一，计数归零时自动释放。

```cpp
auto p1 = std::make_shared<int>(42);
std::cout << p1.use_count() << "\n";  // 1

auto p2 = p1;  // 拷贝，共享所有权
std::cout << p1.use_count() << "\n";  // 2

{
    auto p3 = p1;
    std::cout << p1.use_count() << "\n";  // 3
}  // p3 析构，计数减为 2

std::cout << p1.use_count() << "\n";  // 2
// p1 和 p2 离开作用域后，计数归零，内存释放
```

`std::make_shared` 比 `std::shared_ptr<int>(new int(42))` 更高效——它只需一次分配就能同时分配控制块和对象本身，后者需要两次。除非需要自定义删除器，否则应优先使用它。

> **踩坑预警**：`shared_ptr` 的引用计数本身是线程安全的（原子操作），但指向的对象的并发访问不是——多个线程同时读写 `*p` 依然是数据竞争。另外，`shared_ptr` 有性能开销：控制块的内存开销、引用计数的原子操作开销、对象和控制块可能不在同一缓存行上导致的缓存不友好。如果你的所有权语义是唯一的，请使用 `unique_ptr`，不要"为了安全"而滥用 `shared_ptr`。

### weak_ptr——打破循环引用

`shared_ptr` 有一个经典的陷阱：循环引用。如果对象 A 持有指向 B 的 `shared_ptr`，对象 B 也持有指向 A 的 `shared_ptr`，两者的引用计数永远不会归零，内存永远不会被释放。

`std::weak_ptr` 就是用来解决这个问题的。它是一种"观察者"——可以从 `shared_ptr` 构造，但不增加引用计数。要访问 `weak_ptr` 指向的对象，需要先调用 `lock()` 提升为 `shared_ptr`：

```cpp
struct Node {
    std::shared_ptr<Node> next;
    std::weak_ptr<Node> prev;  // 用 weak_ptr 打破循环
    int value;
    explicit Node(int v) : value(v) {}
    ~Node() { std::cout << "Node(" << value << ") 析构\n"; }
};

auto n1 = std::make_shared<Node>(1);
auto n2 = std::make_shared<Node>(2);
n1->next = n2;       // n2 的引用计数变为 2
n2->prev = n1;       // n1 的引用计数不变（weak_ptr 不增加计数）

// 通过 weak_ptr 访问前驱节点
if (auto locked = n2->prev.lock()) {
    std::cout << "前驱节点值: " << locked->value << "\n";  // 1
}
// n1、n2 正常析构，没有泄漏
```

如果 `prev` 也是 `shared_ptr`，`n1` 和 `n2` 会形成循环引用——即使外部的 `n1` 和 `n2` 离开作用域，它们互相持有的 `shared_ptr` 会让引用计数始终为 1，永远不会析构。换成 `weak_ptr` 之后，循环被打破，两个节点都能正常释放。

## placement new——在指定地址构造对象

普通的 `new` 会自动在堆上找内存，而 `placement new` 则是"你来指定地址，我只负责调用构造函数"。分配内存完全由你自己负责。

```cpp
#include <new>  // placement new 需要这个头文件

alignas(int) unsigned char buffer[sizeof(int)];
int* p = new (buffer) int(42);  // 在 buffer 上构造一个 int
std::cout << *p << "\n";        // 42

// 不能用 delete！因为内存不是 new 分配的
p->~int();  // 显式调用析构函数（对于 int 是空操作）
```

`placement new` 在上位机开发中用得不多，但在嵌入式系统中非常有价值——它允许你在预分配的内存池或共享内存中构造 C++ 对象。注意三点：缓冲区对齐必须满足对象要求（`alignas` 保证了这一点）；内存不是 `new` 分配的，不能调用 `delete`，只能显式调用析构函数；显式调用析构函数在 C++ 中非常罕见，几乎只出现在这个场景中。

## 动手实践——裸指针 vs 智能指针

我们把前面的内容整合到一个完整示例中——裸指针、智能指针和自定义删除器的对比。

```cpp
// dynamic.cpp
// 编译（泄漏检测）:
//   g++ -std=c++17 -O0 -fsanitize=address -g dynamic.cpp -o dynamic
// 编译（正常）:
//   g++ -std=c++17 -O0 -g dynamic.cpp -o dynamic

#include <iostream>
#include <memory>

void raw_pointer_demo()
{
    std::cout << "=== 裸指针版本 ===\n";
    int* p = new int(42);
    std::cout << "值: " << *p << "\n";

    int* arr = new int[5];
    for (int i = 0; i < 5; ++i) { arr[i] = i * 10; }

    // 模拟提前返回（取消注释以观察泄漏）:
    // if (true) return;

    delete p;
    delete[] arr;
    std::cout << "手动释放完成\n";
}

void smart_pointer_demo()
{
    std::cout << "\n=== 智能指针版本 ===\n";
    auto p = std::make_unique<int>(42);
    std::cout << "值: " << *p << "\n";
    auto arr = std::make_unique<int[]>(5);
    for (int i = 0; i < 5; ++i) { arr[i] = i * 10; }
    // 不管以何种方式离开（正常返回、提前 return、异常）
    // 析构函数都会自动释放内存
    std::cout << "离开作用域时自动释放\n";
}

void custom_deleter_demo()
{
    std::cout << "\n=== 自定义删除器 ===\n";
    auto deleter = [](int* ptr) {
        std::cout << "自定义删除器被调用，值为: " << *ptr << "\n";
        delete ptr;
    };
    std::unique_ptr<int, decltype(deleter)> p(new int(99), deleter);
    std::cout << "值: " << *p << "\n";
}

int main()
{
    raw_pointer_demo();
    smart_pointer_demo();
    custom_deleter_demo();
    std::cout << "\n程序结束\n";
    return 0;
}
```

正常编译运行，输出如下：

```text
=== 裸指针版本 ===
值: 42
手动释放完成

=== 智能指针版本 ===
值: 42
离开作用域时自动释放

=== 自定义删除器 ===
值: 99
自定义删除器被调用，值为: 99

程序结束
```

如果取消 `raw_pointer_demo` 里的提前返回注释，ASan 会报告两个泄漏点共 24 字节。而 `smart_pointer_demo` 无论如何都不会泄漏——这就是 RAII 的安全感。

## 练习

### 练习 1：转换裸指针为智能指针

将以下代码改写为智能指针版本：单个对象用 `unique_ptr`，共享对象用 `shared_ptr`。

```cpp
class Logger {
public:
    explicit Logger(const std::string& name) : name_(name) {}
    ~Logger() { std::cout << "Logger(" << name_ << ") 析构\n"; }
    void log(const std::string& msg) { std::cout << "[" << name_ << "] " << msg << "\n"; }
private:
    std::string name_;
};

int main()
{
    Logger* logger = new Logger("app");
    logger->log("程序启动");
    Logger* backup = logger;  // 别名，不拥有
    delete logger;
    // backup 此刻是悬空指针！
    return 0;
}
```

### 练习 2：用自定义删除器实现简单内存池

实现一个固定大小的内存池类，用 `unique_ptr` 配合自定义删除器管理从池中分配的对象。提示：删除器不一定要 `delete`，可以调用 `pool.deallocate()` 归还内存。

## 小结

这一章我们从 `new`/`delete` 出发，走过了一条完整的认知路径。裸用 `new`/`delete` 的问题不在于语法复杂，而在于你必须在所有可能的退出路径上都保证 `delete` 被正确执行——正常返回、提前 `return`、异常退出，每一个遗漏都是潜在的内存泄漏。RAII 通过将堆资源的生命周期绑定到栈对象上，从根本上解决了这个问题。

`unique_ptr` 是默认选择——零开销、独占所有权、不可拷贝但可移动。`shared_ptr` 用于真正需要共享所有权的场景，但要注意引用计数的开销和循环引用。`weak_ptr` 是打破循环引用的利器，它观察但不拥有。`make_unique` 和 `make_shared` 是创建智能指针的首选方式。AddressSanitizer 是检测内存问题的利器，开发和测试阶段应该始终开启。

掌握了动态内存管理，下一步我们深入一个相关话题——内存对齐和填充。为什么 `sizeof` 一个只有几个字段的结构体，结果总是比你手工累加字段大小要多出几个字节？答案就藏在对齐规则里。
