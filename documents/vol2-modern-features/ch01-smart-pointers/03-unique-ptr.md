---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: 深入 unique_ptr 的实现原理、用法与最佳实践
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 1: RAII 深入理解'
reading_time_minutes: 17
related:
- shared_ptr 详解
- 自定义删除器
tags:
- host
- cpp-modern
- intermediate
- unique_ptr
- 智能指针
title: unique_ptr 详解：独占所有权的零开销智能指针
---
# unique_ptr 详解：独占所有权的零开销智能指针

上一篇咱们把“所有权”这个模型立了起来：独占、共享、借用各归其位。现在咱们来看独占所有权最直接的落地：`std::unique_ptr`。

这个类的设计哲学可以用一句话概括：**一个对象，一个主人，零开销**。它不搞什么引用计数、不做原子操作、不分配额外的控制块——您给它一个对象，它替您管好；您离开作用域，它替您删掉。就这么简单。（btw，这玩意怎么面试这么爱考。。。总是问这个玩意。。。）

但是要注意，简单 != 肤浅。`unique_ptr` 背后涉及的所有权语义、移动语义、自定义删除器、空基类优化（EBO）等话题，每一条都值得深入理解。所以，让我们开始我们的旅程！

## 独占所有权：为什么不能拷贝

`unique_ptr` 最核心的语义是"**独占**"。请您冷静一下，思考一下独占的含义。

换而言之，在**同一时刻**，**只有一个 `unique_ptr` 拥有对象的所有权**。这意味着它不允许拷贝构造和拷贝赋值，只**允许移动**。我们想一下, 如果我们允许拷贝，两个 `unique_ptr` 都会认为自己拥有对象，离开作用域时两个都会尝试 delete——双重释放，好一个Double Free啊！直接导致未定义行为<RefLink :id="1" preview="cppreference std::unique_ptr — non-copyable, movable-only exclusive ownership" />。

```cpp
#include <memory>
#include <iostream>

struct Widget {
    int value;
    explicit Widget(int v) : value(v) {
        std::cout << "Widget(" << value << ") 构造\n";
    }
    ~Widget() {
        std::cout << "~Widget(" << value << ") 析构\n";
    }
};

void ownership_demo() {
    auto p1 = std::make_unique<Widget>(42);
    // auto p2 = p1;              // 编译错误！unique_ptr 不可拷贝
    auto p2 = std::move(p1);      // OK：所有权从 p1 转移到 p2

    // 此时 p1 == nullptr，p2 拥有对象
    std::cout << "p1: " << p1.get() << "\n";  // 输出: 0 或 nullptr
    std::cout << "p2: " << p2.get() << "\n";  // 输出: 有效地址
    std::cout << "p2->value: " << p2->value << "\n";  // 输出: 42
}   // p2 析构，Widget 自动被 delete
```

这份演示程序就在下面，点“动手试一试”直接跑（`p2` 的地址每次运行都不同，看它非零即可）：

<OnlineCompilerDemo
  title="动手验证：独占所有权的转移"
  source-path="code/examples/vol2/24_unique_ptr_ownership.cpp"
  description="在线观察所有权转移：std::move 之后 p1 归零，p2 接管对象，作用域结束时自动析构。"
  run-options="-std=c++17"
  allow-run
/>

独占拥有、拷贝被拒、移动转交这三段做成了动画，您可以播放、暂停，也可以按步进键单步看，把每一步都看个清楚：

<Anim id="unique-ownership" />

这个"不可拷贝、可移动"的设计完美映射了现实中的所有权转移——就像您把一把钥匙交给别人，您自己就不再拥有那把钥匙了。在代码层面，`std::move` 把 `p1` 内部的裸指针转移给了 `p2`，然后把 `p1` 置空。整个过程没有额外的内存分配，也没有引用计数的开销。

## make_unique vs new：为什么 C++14 要加这个函数

C++11 引入了 `std::unique_ptr` 但忘了提供 `std::make_unique`（这被普遍认为是一个疏忽，感觉是C++委员会那帮人吵架的时候忘记了这个事情了。。。），直到 C++14 咱们标准库才补上<RefLink :id="2" preview="Herb Sutter, GotW #89 Solution: Smart Pointers, 2013" />。那么 `make_unique` 相比直接 `new` 有什么优势？

首先是**异常安全**。考虑下面这个函数调用：

```cpp
// 假设有这样一个函数签名
void process(std::unique_ptr<Widget> ptr, int computed_value);

// 危险写法（C++11 风格）
process(std::unique_ptr<Widget>(new Widget(42)), compute_something());

// 安全写法（C++14 风格）
process(std::make_unique<Widget>(42), compute_something());
```

在危险写法中，C++ 编译器需要在调用 `process` 之前依次完成：`new Widget(42)`、构造 `unique_ptr`、调用 `compute_something()`。想一想，是不是有点危险呢？是的！

因为在 **C++17 之前**，C++ 标准**并不规定函数参数的求值顺序**——编译器可能先 `new`，然后调用 `compute_something()`，最后构造 `unique_ptr`。如果 `compute_something()` 抛出异常，那个 `new` 出来的 `Widget` 就泄漏了——因为 `unique_ptr` 还没来得及接管它。

> PS! 从 **C++17 开始**，标准规定各实参的求值**不得交错**——每个实参（包括 `unique_ptr` 的构造）必须完整求值之后，才能开始求值下一个<RefLink :id="3" preview="cppreference Order of evaluation — C++17: parameters are indeterminately sequenced, order still unspecified" />。左右的先后顺序仍然是未指定的，但这已经足够堵上漏洞：`unique_ptr` 要么已经构造完成，要么还没开始 `new`，不存在"new 了但没人接管"的中间态。因此在 C++17 及更高版本中，危险写法实际上是安全的。不过，`make_unique` 仍然有其他优势（代码简洁、避免重复类型名），并且兼容旧标准，所以仍然是推荐做法。

所以，我们来正视`make_unique`。这玩意**把分配和构造包装在一个函数调用**里，不存在这种"中间态"，因此是异常安全的<RefLink :id="4" preview="cppreference std::make_unique — Notes on exception safety vs direct new" />。

其次是**代码简洁性**。`make_unique` 避免了在代码中出现裸 `new`，减少犯错的可能：

```cpp
// 对比
auto p1 = std::unique_ptr<Widget>(new Widget(42));  // 啰嗦，且容易忘写 unique_ptr
auto p2 = std::make_unique<Widget>(42);              // 简洁，不可能忘记管理
```

不过，咱们 `make_unique` 有一个限制：它不支持**自定义删除器**。如果您需要自定义删除器（比如管理 `FILE*` 或 `malloc` 分配的内存），就必须直接构造 `unique_ptr`。这个问题咱们会在后面的"自定义删除器"章节详细讨论。

## 移动语义与 unique_ptr 的深层关系

`unique_ptr` 和移动语义的关系非常紧密。在 C++11 之前，C++ 只有拷贝语义——把一个对象"复制"一份。但对于 `unique_ptr` 来说，拷贝意味着"两个指针指向同一个对象"，这违背了独占所有权的语义。移动语义的引入恰好解决了这个问题：移动不是"复制"，而是"转移"——源对象放弃所有权，目标对象接管。

这使得 `unique_ptr` 可以放入标准容器中：

```cpp
#include <memory>
#include <vector>
#include <iostream>

struct Sensor {
    int id;
    explicit Sensor(int i) : id(i) {}
};

int main() {
    std::vector<std::unique_ptr<Sensor>> sensors;

    // push_back 需要移动，因为 unique_ptr 不可拷贝
    sensors.push_back(std::make_unique<Sensor>(1));
    sensors.push_back(std::make_unique<Sensor>(2));
    sensors.push_back(std::make_unique<Sensor>(3));

    // vector 扩容时，内部的 unique_ptr 会通过移动构造转移
    // 这也是为什么 unique_ptr 的移动操作标记为 noexcept
    for (const auto& s : sensors) {
        std::cout << "Sensor id: " << s->id << "\n";
    }

    // 从函数返回 unique_ptr 也是通过移动（或 RVO）
    auto make_sensor = [](int id) -> std::unique_ptr<Sensor> {
        return std::make_unique<Sensor>(id);
    };

    auto s = make_sensor(99);
    std::cout << "Created sensor " << s->id << "\n";
}
```

各位，可以看到 `unique_ptr` 的移动构造函数和移动赋值运算符都标记为 `noexcept`。这对 `std::vector` 的行为有直接影响——当 vector 扩容时，如果元素的移动构造是 `noexcept` 的，vector 会优先使用移动；否则会退化为拷贝（但 `unique_ptr` 不可拷贝，所以必须移动）。因此 `noexcept` 的移动操作是 `unique_ptr` 能够安全存入容器的关键保证。

## unique_ptr<T[]>：数组版本

`unique_ptr` 有一个针对数组的偏特化版本 `unique_ptr<T[]>`，它在析构时会调用 `delete[]` 而不是 `delete`。

```cpp
auto arr = std::make_unique<int[]>(64);  // 分配 64 个 int
arr[0] = 42;
arr[1] = 17;
// 析构时自动 delete[]
```

不过说实话，在 C++ 中需要手动管理动态数组的场景已经非常少了。如果您需要一个固定大小的数组，用 `std::array` 或 `std::vector` 几乎总是更好的选择。`unique_ptr<T[]>` 主要用于对接那些返回动态分配数组的 C API，比如：

```cpp
// 假设某个 C API 返回 malloc 分配的数组
extern "C" int* create_buffer(size_t size);
extern "C" void free_buffer(int* buf);

auto buffer = std::unique_ptr<int[], void(*)(int*)>(
    create_buffer(1024),
    [](int* p) { free_buffer(p); }
);
buffer[0] = 42;
```

笔者强烈建议：不要用 `unique_ptr<T[]>` 来替代 `std::vector`。`vector` 提供了 `size()`、迭代器、边界检查（通过 `at()`）等能力，而 `unique_ptr<T[]>` 除了自动释放之外什么都没有<RefLink :id="5" preview="C++ Core Guidelines R.20-24 — smart pointer rules" />。

## 自定义删除器基础

`unique_ptr` 的第二个模板参数就是删除器的类型。默认是 `std::default_delete<T>`，内部就是简单的 `delete ptr`。但您可以替换为任何可调用对象——函数指针、lambda、函数对象，只要是 `void operator()(T*)` 的签名就行。

最常见的场景是管理 C API 返回的资源：

```cpp
#include <cstdio>
#include <memory>

// 函数指针作为删除器
using FilePtr = std::unique_ptr<FILE, decltype(&std::fclose)>;

FilePtr open_file(const char* path, const char* mode) {
    FILE* f = std::fopen(path, mode);
    return FilePtr(f, &std::fclose);
}

// lambda 作为删除器（无捕获 → 无状态 → 零开销）
auto make_closer = []() {
    auto deleter = [](FILE* f) noexcept { if (f) std::fclose(f); };
    return std::unique_ptr<FILE, decltype(deleter)>(std::fopen("/tmp/log", "w"), deleter);
};
```

函数对象（functor）作为删除器也是常见的选择，尤其是当您想让删除器类型有名字的时候：

```cpp
struct FreeDeleter {
    void operator()(void* p) noexcept {
        std::free(p);
    }
};

// 管理 malloc 分配的内存
auto buf = std::unique_ptr<char, FreeDeleter>(
    static_cast<char*>(std::malloc(256))
);
```

关于自定义删除器的更深入讨论（有状态删除器、EBO 优化、`shared_ptr` 中的删除器等），咱们会在"自定义删除器与侵入式引用计数"那篇中专门展开。

## 零开销证明：sizeof 与汇编分析

`unique_ptr` 常被宣传为"零开销抽象"，但这不是营销口号——咱们可以用实际代码来验证。首先是 `sizeof` 对比：

```cpp
#include <memory>
#include <iostream>

struct EmptyDeleter {
    void operator()(int* p) noexcept { delete p; }
};

// 有状态删除器：带数据成员，没法 EBO
struct StatefulDeleter {
    int extra;
    void operator()(int* p) noexcept { delete p; }
};

int main() {
    std::cout << "sizeof(int*):                             " << sizeof(int*) << "\n";
    std::cout << "sizeof(unique_ptr<int>):                  " << sizeof(std::unique_ptr<int>) << "\n";
    std::cout << "sizeof(unique_ptr<int, EmptyDeleter>):    " << sizeof(std::unique_ptr<int, EmptyDeleter>) << "\n";
    std::cout << "sizeof(unique_ptr<int, void(*)(int*)>):   " << sizeof(std::unique_ptr<int, void(*)(int*)>) << "\n";
    std::cout << "sizeof(unique_ptr<int, StatefulDeleter>): " << sizeof(std::unique_ptr<int, StatefulDeleter>) << "\n";
}
```

这份验证程序就在下面，点“动手试一试”直接跑（64 位平台上）：

<OnlineCompilerDemo
  title="动手验证：unique_ptr 的零开销"
  source-path="code/examples/vol2/25_unique_ptr_sizeof.cpp"
  description="在线对比 sizeof：默认删除器与无状态删除器的 unique_ptr 都是 8 字节（和裸指针一样大），函数指针和带状态删除器翻倍到 16 字节。"
  run-options="-std=c++17"
  allow-run
/>

默认删除器和无状态函数对象的 `unique_ptr` 和裸指针一样大——8 字节。这是空基类优化（EBO）的功劳<RefLink :id="6" preview="Bartlomiej Filipek, Empty Base Class Optimisation, no_unique_address and unique_ptr, C++ Stories, 2021" />：`unique_ptr` 内部通常继承自删除器类型，当删除器是空类（没有数据成员）时，编译器把它的大小优化为 0，`unique_ptr` 就只需要存那一个裸指针。一旦删除器带了状态——函数指针要存地址、`StatefulDeleter` 要存 `extra`——EBO 用不上，大小就涨到 16 字节。

而使用函数指针作为删除器时，`unique_ptr` 需要额外存储一个函数指针，所以大小翻倍——16 字节。这就是"零开销"的前提条件：**删除器必须是无状态的**。

咱们再从汇编的角度验证。下面是一个简单的例子：

```cpp
// 用 unique_ptr 管理 int
int use_unique_ptr() {
    auto p = std::make_unique<int>(42);
    return *p;
}

// 等价的裸指针版本
int use_raw_ptr() {
    int* p = new int(42);
    int v = *p;
    delete p;
    return v;
}
```

在开启优化（`-O2`）后，这两个函数生成的汇编几乎完全相同。把上面两个函数存成文件，用 `g++ -std=c++17 -O2 -S` 编译，会看到它们都生成：

```asm
movl    $42, %eax
ret
```

编译器把 `unique_ptr` 的构造和析构直接内联优化掉了，连 `new` 和 `delete` 都被消除了（因为对象的生命周期很短且没有副作用）。这就是 C++ 抽象的威力：您在源码层面获得了安全性和可读性，但在机器码层面没有付出任何代价。

## 其他的一些接口，比如说 release()、reset() 和 get()：三个关键操作

`unique_ptr` 提供了几个手动管理所有权的方法，理解它们的区别非常重要。

`get()` 返回内部裸指针但不转移所有权。这在您需要把指针传给某个只使用但不拥有的函数时很有用：

```cpp
void print_widget(const Widget* w);

auto p = std::make_unique<Widget>(42);
print_widget(p.get());  // 传给只读函数，p 仍然拥有对象
```

`release()` 放弃所有权并返回裸指针——`unique_ptr` 变空了，但对象不会被删除。这相当于"我把对象交给您了，您自己负责释放"：

```cpp
auto p = std::make_unique<Widget>(42);
Widget* raw = p.release();  // p 变为 nullptr，raw 指向对象
// ... 使用 raw ...
delete raw;  // 你必须手动释放
```

`release()` 是一个需要谨慎使用的操作。一旦您调用了它，就回到了裸指针的世界——如果您忘记 `delete`，就会内存泄漏。大多数情况下，使用 `std::move()` 转移所有权给另一个 `unique_ptr` 是更好的选择。

`reset()` 替换当前管理的对象。如果不传参数，就简单地释放当前对象并置空：

```cpp
auto p = std::make_unique<Widget>(1);
p.reset(new Widget(2));  // 释放 Widget(1)，接管 Widget(2)
p.reset();               // 释放 Widget(2)，p 变为 nullptr
```

下一篇转向 `shared_ptr`——完全不同的所有权模型：共享所有权。真正的复杂性从那里才开始。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="std::unique_ptr"
    url="https://en.cppreference.com/w/cpp/memory/unique_ptr"
  />
  <ReferenceItem
    :id="2"
    author="Herb Sutter"
    title="GotW #89 Solution: Smart Pointers"
    publisher="herbsutter.com"
    :year="2013"
    url="https://herbsutter.com/2013/05/29/gotw-89-solution-smart-pointers/"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="Order of Evaluation"
    chapter="Rule 14: function argument evaluation (C++17)"
    url="https://en.cppreference.com/w/cpp/language/eval_order"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="std::make_unique"
    chapter="Notes: exception safety"
    url="https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique"
  />
  <ReferenceItem
    :id="5"
    author="Bjarne Stroustrup / Herb Sutter (eds.)"
    title="C++ Core Guidelines — R.20-R.24: Smart Pointer Rules"
    publisher="isocpp.org"
    url="https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rr-owner"
  />
  <ReferenceItem
    :id="6"
    author="Bartlomiej Filipek"
    title="Empty Base Class Optimisation, no_unique_address and unique_ptr"
    publisher="C++ Stories"
    :year="2021"
    url="https://www.cppstories.com/2021/no-unique-address/"
  />
</ReferenceCard>
