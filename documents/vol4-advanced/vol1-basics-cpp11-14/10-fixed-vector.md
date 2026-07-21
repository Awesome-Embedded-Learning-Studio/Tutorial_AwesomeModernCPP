---
title: "综合项目:fixed_vector<T, N>"
description: "把本卷前 9 篇学的全串起来,实现一个编译期定长、连续存储、零动态分配的 vector。完整代码 + 实测,再和 C++26 的 std::inplace_vector 以及 EASTL/Boost/Folly 的同类实现对比"
chapter: 12
order: 10
tags:
  - host
  - cpp-modern
  - intermediate
  - 模板
  - 容器
  - vector
  - 零开销抽象
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20, 26]
reading_time_minutes: 20
prerequisites:
  - "CRTP:用奇递归模板做静态多态"
  - "非类型模板参数:从整数到 C++20 的浮点与类类型"
  - "类模板:成员、依赖名与惰性实例化"
related:
  - "模板导论:从一份代码配方说起"
---

# 综合项目:fixed_vector&lt;T, N&gt;

走到这里,本卷前 9 篇的概念该一起练手了。咱们实现一个 `fixed_vector<T, N>`:一个编译期固定容量、连续存储、**零动态分配**的 vector。它综合用到类模板、非类型模板参数、迭代器,如果您愿意还能用 CRTP 给迭代器加接口。这个练习不是空想,标准库的 `std::inplace_vector`(C++26)就是它的「官方版」,工业界早有 EASTL 的 `fixed_vector`、Boost 的 `static_vector`、Folly 的 `small_vector` 在用同一套思路。咱们写一个教学简化版,讲清每一块设计,最后和标准库对照。

## 目标:一个什么样的容器

先定清楚 `fixed_vector` 要满足什么。

第一,容量 `N` 在编译期固定,作为非类型模板参数。第二,元素连续存储,能用 `operator[]` 随机访问,裸指针能当迭代器用。第三,**不分配堆内存**,所有元素放在对象自身的存储里,这在嵌入式、实时系统、禁止异常分配的环境里特别有用。第四,元素数可以动态变化(从 0 到 N),这点和「编译期就构造所有元素」的 `std::array` 不同,`fixed_vector` 是按需构造。

这套目标和 `std::inplace_vector` 完全一致。cppreference 对 `inplace_vector` 的定义就是「a dynamically-resizable array with contiguous inplace storage」,容量编译期固定等于 N,元素存在对象自身内。咱们的 `fixed_vector` 是它的教学缩影。

## 实现骨架

模板签名是 `template <typename T, std::size_t N>`,一个类型参数加一个非类型参数。存储用 `std::array<T, N>` 当后端,省得自己管对齐和原始内存;再用一个 `size_` 记录当前实际元素数。

```cpp
#include <array>
#include <cstddef>
#include <stdexcept>

template <typename T, std::size_t N>
class FixedVector {
    std::array<T, N> data_{};   // 定长存储,栈上,无堆分配
    std::size_t size_ = 0;
public:
    static constexpr std::size_t capacity_v = N;
    // ... 成员函数
};
```

`data_` 是 `std::array<T, N>`,它本身就是连续存储,`size_` 跟踪当前塞了多少元素。`capacity_v` 是一个静态常量,对外暴露容量,这正是非类型参数 `N` 的典型用法。

## push_back 与边界处理

`push_back` 在尾部加一个元素。关键边界是容量耗尽:超过 `N` 时怎么办。标准库的 `inplace_vector` 在这种情况抛 `std::bad_alloc`(注意是 `bad_alloc`,不是 `out_of_range`,这是 `inplace_vector` 的规定)。咱们的教学版图省事抛 `std::out_of_range`,语义清楚就行。

```cpp
constexpr void push_back(const T& value) {
    if (size_ >= N) {
        throw std::out_of_range("FixedVector full");
    }
    data_[size_++] = value;
}
```

注意整个函数标了 `constexpr`,在 C++20 里这意味着 `push_back` 能在编译期执行(只要 `T` 的操作都是常量表达式)。`fixed_vector` 全套成员都能做成 `constexpr`,这是它和 `std::array` 一样适合编译期计算的好处。

## 元素访问与迭代器

`operator[]` 直接转发给底层 `std::array`,不做边界检查(和 `std::vector::operator[]` 一致,要检查用 `at()`)。

```cpp
constexpr T& operator[](std::size_t i) { return data_[i]; }
constexpr const T& operator[](std::size_t i) const { return data_[i]; }
constexpr std::size_t size() const { return size_; }
```

迭代器是这份实现里最优雅的部分。因为元素连续存储,裸指针 `T*` 天生就是一个满足随机访问迭代器要求的类型(支持 `*`、`++`、`+n`、比较)。所以 `begin()` 和 `end()` 直接返回指针,不用自己定义迭代器类。

```cpp
constexpr T* begin() { return data_.data(); }
constexpr T* end() { return data_.data() + size_; }
constexpr const T* begin() const { return data_.data(); }
constexpr const T* end() const { return data_.data() + size_; }
```

`data_.data()` 返回底层数组的首元素指针,`end()` 指向「当前最后一个元素的下一个」。有了这组 `begin/end`,范围 for 循环、`std::sort`、`std::find` 这些标准算法都能直接用在 `fixed_vector` 上,因为它们要的只是迭代器接口,而裸指针正好满足。这是 STL「迭代器统一容器和算法」设计哲学的直接体现。

## 完整代码与实测

把上面的拼起来,加个 `main` 跑一遍。

```cpp
#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>

template <typename T, std::size_t N>
class FixedVector {
    std::array<T, N> data_{};
    std::size_t size_ = 0;
public:
    static constexpr std::size_t capacity_v = N;

    constexpr void push_back(const T& value) {
        if (size_ >= N) throw std::out_of_range("FixedVector full");
        data_[size_++] = value;
    }
    constexpr T& operator[](std::size_t i) { return data_[i]; }
    constexpr const T& operator[](std::size_t i) const { return data_[i]; }
    constexpr std::size_t size() const { return size_; }

    constexpr T* begin() { return data_.data(); }
    constexpr T* end() { return data_.data() + size_; }
    constexpr const T* begin() const { return data_.data(); }
    constexpr const T* end() const { return data_.data() + size_; }
};

int main() {
    FixedVector<int, 8> v;
    for (int i = 1; i <= 5; ++i) v.push_back(i * 10);

    std::cout << "size = " << v.size() << " capacity = " << decltype(v)::capacity_v << "\n";
    std::cout << "elements: ";
    for (auto x : v) std::cout << x << " ";
    std::cout << "\n";
    std::cout << "v[2] = " << v[2] << "\n";
    std::cout << "sizeof(FixedVector<int,8>) = " << sizeof(FixedVector<int, 8>) << "\n";
    std::cout << "sizeof(int*) = " << sizeof(int*) << " (对比:动态 vector 至少含 3 个指针)\n";
    return 0;
}
```

```bash
$ g++ -Wall -Wextra -std=c++20 fixed_vector.cpp -o fixed_vector && ./fixed_vector
size = 5 capacity = 8
elements: 10 20 30 40 50
v[2] = 30
sizeof(FixedVector<int,8>) = 40
sizeof(int*) = 8 (对比:动态 vector 至少含 3 个指针)
```

几条关键结果对一下。`size = 5 capacity = 8`:塞了 5 个,容量 8。范围 for 遍历输出 `10 20 30 40 50`,说明裸指针迭代器工作正常。`v[2] = 30`,`operator[]` 随机访问没问题。最有说服力的是 `sizeof(FixedVector<int,8>) = 40`:8 个 `int` 占 32 字节,加上 `size_` 的 8 字节,正好 40,里面**没有任何堆指针**。对比之下,`std::vector` 至少含三个指针(数据指针、容量、大小),外加一次堆分配。

## 零动态分配,为什么重要

`sizeof` 没有堆指针,意味着 `fixed_vector` 的所有存储都在对象自身里。这带来几个实际好处。

性能可预测。没有堆分配,就没有分配器的开销和内存碎片,构造析构是确定的。在实时系统、游戏引擎的热点路径上,一次 `std::vector` 的堆分配可能就是几微秒的抖动,`fixed_vector` 完全没有。

异常安全边界清晰。`fixed_vector` 只在容量耗尽时抛异常(`push_back` 到 N 上限),不像 `std::vector` 扩容时可能因为分配失败抛 `bad_alloc`。在禁用异常或禁用堆的环境(很多嵌入式项目)里,`fixed_vector` 能用,`std::vector` 不能。

缓存友好。元素连续存储且在对象自身内,访问模式对 CPU 缓存非常友好,这点和 `std::array`、`std::vector` 一样。

## 和 std::inplace_vector(C++26)对比

`std::inplace_vector` 是这套思路的标准库版本,特性测试宏 `__cpp_lib_inplace_vector`(现行值 `202603L`),对应 **C++26**(早期提案瞄准过 C++23,最终落定 C++26)。它的设计和咱们的 `fixed_vector` 高度一致:编译期固定容量、连续存储、无堆分配、按需构造元素。

标准库版本比咱们的教学版完整得多。它有一整套成员函数:`emplace_back`、`try_push_back`(满了不抛异常,返回空的 `std::optional<reference>`)、`unchecked_push_back`(不检查,调用者自己保证没满,用于性能关键路径)、`insert`、`erase`、`resize` 等等。满了之后的策略也更细致:`push_back` 满了抛 `std::bad_alloc`,`try_push_back` 满了返回空 optional,`unchecked_push_back` 假定没满直接塞。这套「抛异常 / try / unchecked」三档 API,是工业级容器设计的成熟范式。

工业界在这之前早有同类实现。EASTL(EA 的 STL 替代)有 `fixed_vector`,Boost.Container 有 `static_vector`,Folly(Facebook)有 `small_vector`(带 small buffer 优化)。它们各有侧重,但核心都是「连续存储 + 编译期或半编译期容量 + 避免堆分配」。咱们的 `FixedVector` 把最核心的骨架抽出来讲明白,理解了它,再去读这些工业实现就轻松了。

## 可以接着扩展的方向

这个教学版还差几块,留作您练手的方向。

加 `try_push_back` 和 `unchecked_push_back`,对标 `inplace_vector` 的三档 API。`try_push_back` 返回 `std::optional<reference>`,满了返回空 optional(不抛异常);`unchecked_push_back` 假定没满,省掉检查。

用对齐的原始内存替代 `std::array`,实现「按需构造」。现在 `std::array<T, N>` 会默认构造所有 N 个元素,哪怕您只用 3 个;真正的 `inplace_vector` 用 `alignas(T)` 的原始字节数组,只在 `push_back` 时 placement new 构造元素,省掉无用构造。这块涉及 std::optional、placement new、手动析构,是更接近标准库实现的练手。

给迭代器加 CRTP 接口。如果您想让 `fixed_vector` 的迭代器支持一些自定义行为(比如调试模式下的边界检查),可以用 CRTP 写一个迭代器基类。这会把本卷第 9 篇的 CRTP 和这里的迭代器结合起来。

---

走到这里,本卷第一部分「模板基础(C++11-14)」就完整了。从一份代码配方讲起,经过函数模板的编译模型、类模板的惰性实例化、特化与偏特化的模式匹配、非类型参数、两阶段名字查找与 ADL、隐藏友元和 Barton-Nackman、别名模板、CRTP 静态多态,最后用 `fixed_vector` 把它们全焊在一起。第二部分(现代模板技术,C++17)会接着讲类型萃取、SFINAE、`if constexpr`、变参模板、折叠表达式、完美转发,把元编程的工具箱补齐;第三部分(C++20-23)再上 concepts、requires 和反射,把 TMP 从黑魔法变成人能写的代码。模板这条路,咱们才刚上道。
