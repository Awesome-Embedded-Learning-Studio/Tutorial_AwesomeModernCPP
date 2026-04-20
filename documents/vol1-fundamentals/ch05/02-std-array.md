---
title: "std::array"
description: "掌握 std::array 的用法和与 C 数组的对比，学会使用现代 C++ 的固定大小容器"
chapter: 5
order: 2
difficulty: beginner
reading_time_minutes: 10
platform: host
prerequisites:
  - "C 风格数组"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# std::array

C 风格数组能干活吗？当然能——我们从学 C 语言的第一天起就在用它。但说句实话，C 数组用起来实在太容易踩坑了：传给函数就退化成指针、丢失长度信息、不能直接赋值、不能作为函数返回值、没有边界检查。这些问题不是"写的时候小心一点"就能避免的，它们是 C 数组设计上的固有缺陷。

`std::array` 就是为了解决这些问题而生的。它在栈上分配内存，和 C 数组一样紧凑高效，但拥有真正的值语义——可以拷贝、赋值、传参、返回，而且始终知道自己的大小。接下来我们就来看看，为什么从 C++11 开始，固定大小的数组应该优先使用 `std::array`。

> **学习目标**
> 完成本章后，你将能够：
>
> - [ ] 正确声明和使用 `std::array`
> - [ ] 理解值语义与数组退化的区别
> - [ ] 用 `std::array` 配合 STL 算法完成常见操作
> - [ ] 在需要与 C API 交互时获取底层指针

## 环境说明

- 平台：Linux x86\_64（WSL2 也可以）
- 编译器：GCC 13+ 或 Clang 17+
- 编译选项：`-Wall -Wextra -std=c++17`

## std::array 基本用法

`std::array` 定义在 `<array>` 头文件中，需要两个模板参数：元素类型和固定大小。大小必须是编译期常量——和 C 数组一样，`std::array` 不会动态增长，它就是一块固定大小的连续内存。

```cpp
#include <array>
#include <iostream>

int main()
{
    std::array<int, 5> arr = {1, 2, 3, 4, 5};

    std::cout << "大小:     " << arr.size() << "\n";
    std::cout << "为空?     " << (arr.empty() ? "是" : "否") << "\n";
    std::cout << "最大大小: " << arr.max_size() << "\n";

    return 0;
}
```

```text
大小:     5
为空?     否
最大大小: 5
```

`size()`、`max_size()`、`empty()` 这几个函数对固定大小的 `std::array` 来说看起来有点多余。它们存在的意义在于统一接口——让 `std::array` 和 `std::vector` 等容器拥有相同的访问方式，泛型代码不需要关心底层到底是固定大小还是动态大小。

> `std::array<int, 0>` 是合法的，此时 `empty()` 返回 `true`。但说实话，大小为 0 的 `std::array` 在实际代码里极少出现。如果你需要一个"可能为空"的容器，请用 `std::vector`。

## 访问元素

`std::array` 提供了多种元素访问方式。最常用的 `[]` 和安全的 `at()`，以及直接获取首尾元素和底层指针的便捷接口：

```cpp
#include <array>
#include <iostream>

int main()
{
    std::array<int, 5> arr = {10, 20, 30, 40, 50};

    std::cout << "arr[0]     = " << arr[0] << "\n";      // 无边界检查
    std::cout << "arr.at(2)  = " << arr.at(2) << "\n";   // 越界抛异常
    std::cout << "front      = " << arr.front() << "\n";
    std::cout << "back       = " << arr.back() << "\n";

    int* p = arr.data();                                   // 获取裸指针
    std::cout << "data()[3]  = " << p[3] << "\n";

    return 0;
}
```

```text
arr[0]     = 10
arr.at(2)  = 30
front      = 10
back       = 50
data()[3]  = 40
```

`[]` 和 `at()` 的区别很重要：`arr[10]` 在这个 5 元素的数组上是未定义行为——可能读到垃圾、可能崩溃、也可能表面上没事但数据已被悄悄破坏。而 `arr.at(10)` 会抛出 `std::out_of_range` 异常，让你有机会优雅地处理错误。

> 在开发阶段，建议对可能越界的下标使用 `at()`。发布版本里可以换回 `[]` 来避免异常开销——不过现代编译器在非越界情况下 `at()` 的额外开销几乎为零。或者全程用 `[]`，然后靠 AddressSanitizer 来抓越界 bug。

`data()` 返回指向底层元素存储的裸指针，在与接受 `int*` 参数的 C 库函数交互时直接传入即可。

## 值语义——这才是 std::array 的杀手级优势

前面铺垫了这么多基本用法，接下来才是 `std::array` 真正甩开 C 数组的地方：它拥有**值语义**。你可以像操作 `int` 或 `std::string` 一样操作它——拷贝、赋值、传参、返回，全都没问题。

```cpp
#include <array>
#include <iostream>

// 直接返回 std::array——C 数组做不到这一点
std::array<int, 5> make_array()
{
    std::array<int, 5> result = {1, 2, 3, 4, 5};
    return result;
}

// 按值传参——不会丢失大小信息
void print_array(std::array<int, 5> arr)
{
    for (int x : arr) {
        std::cout << x << " ";
    }
    std::cout << "\n函数内大小: " << arr.size() << "\n";
}

int main()
{
    auto arr1 = make_array();
    auto arr2 = arr1;  // 直接拷贝——C 数组做不到

    arr2[0] = 99;
    std::cout << "arr1[0] = " << arr1[0] << "\n";  // 1，不受 arr2 影响
    std::cout << "arr2[0] = " << arr2[0] << "\n";  // 99

    print_array(arr1);
    print_array(arr2);

    return 0;
}
```

```text
arr1[0] = 1
arr2[0] = 99
1 2 3 4 5
函数内大小: 5
99 2 3 4 5
函数内大小: 5
```

上面每一行都是 C 数组做不到的事。C 数组不能直接赋值（`a = b` 编译都过不了），不能作为函数返回值，作为函数参数时会退化成指针从而丢失长度。`std::array` 之所以能做到这些，是因为它是一个类，封装了内部的 C 数组，并提供了拷贝构造函数和拷贝赋值运算符。编译器知道如何复制这个对象，也知道它的大小——从根本上消除了数组退化问题。

> 按值传递 `std::array` 会拷贝整个数组内容。如果数组很大（比如 `std::array<int, 10000>`），应该用 `const` 引用：`void process(const std::array<int, 10000>& arr)`。对于小数组，按值传递的开销基本可以忽略。

## C 数组 vs std::array 正面交锋

我们把 C 数组和 `std::array` 在常见操作上做一个直接对比：

| 操作 | C 数组 | std::array |
|------|--------|------------|
| 声明 | `int arr[5];` | `std::array<int, 5> arr;` |
| 获取大小 | `sizeof(arr)/sizeof(arr[0])`（传参后失效） | `arr.size()`（始终有效） |
| 赋值 | 不支持 | `arr2 = arr1` |
| 拷贝 | 手动 `memcpy` | `auto copy = arr;` |
| 传参 | 退化为指针，丢失大小 | 按值保留大小，或传引用 |
| 返回值 | 不可能 | 可以 |
| 边界检查 | 无 | `arr.at(i)` 抛异常 |
| 获取裸指针 | 自动退化 | `arr.data()`（显式） |
| 零开销 | 是 | 是 |

最后一行是关键：**`std::array` 和 C 数组在内存布局和运行时性能上完全等价**。所有额外能力——`size()`、`at()`、`data()`、值语义——全是编译期的零开销抽象。运行时没有任何额外的内存分配或函数调用开销。

> 如果你有兴趣，可以加 `-O2` 编译一个分别使用 C 数组和 `std::array` 的遍历程序，对比汇编输出——两者生成的指令几乎一模一样。零开销抽象不是一句空话。

## 填充、交换与遍历

`std::array` 还提供了几个实用的操作，配合 STL 算法更是如虎添翼：

```cpp
#include <algorithm>
#include <array>
#include <iostream>

int main()
{
    std::array<int, 5> a = {1, 2, 3, 4, 5};
    std::array<int, 5> b = {10, 20, 30, 40, 50};

    // fill —— 全部设为同一值
    a.fill(0);
    std::cout << "fill 后: ";
    for (int x : a) { std::cout << x << " "; }
    std::cout << "\n";

    // swap —— 交换两个 array 的内容
    a = {1, 2, 3, 4, 5};
    a.swap(b);
    std::cout << "swap 后 a: ";
    for (int x : a) { std::cout << x << " "; }
    std::cout << "\n";

    // 配合 <algorithm>
    std::array<int, 5> c = {5, 3, 1, 4, 2};
    std::sort(c.begin(), c.end());
    std::cout << "排序后: ";
    for (int x : c) { std::cout << x << " "; }
    std::cout << "\n";

    return 0;
}
```

```text
fill 后: 0 0 0 0 0
swap 后 a: 10 20 30 40 50
排序后: 1 2 3 4 5
```

`fill()` 在需要重置缓冲区时非常方便，一行搞定。`swap()` 的底层是逐元素交换，时间复杂度 O(n)。`std::sort`、`std::find`、`std::reverse`——整个 STL 算法库都可以直接用在 `std::array` 上，传 `begin()` 和 `end()` 即可。

## 实战：用 std::array 重写 C 数组代码

我们把之前用 C 数组完成的操作用 `std::array` 重新实现一遍，直观感受改进在哪里：

```cpp
#include <algorithm>
#include <array>
#include <iostream>

// 函数签名清晰——类型和大小一目了然，不需要额外传长度
void print_stats(const std::array<int, 5>& data)
{
    std::cout << "元素个数: " << data.size() << "\n";

    auto [min_it, max_it] = std::minmax_element(data.begin(), data.end());
    std::cout << "最小值: " << *min_it << "\n";
    std::cout << "最大值: " << *max_it << "\n";

    int sum = 0;
    for (int x : data) { sum += x; }
    std::cout << "平均: " << static_cast<double>(sum) / data.size() << "\n";
}

int main()
{
    std::array<int, 5> scores = {85, 92, 78, 96, 88};

    std::cout << "原始数据: ";
    for (int x : scores) { std::cout << x << " "; }
    std::cout << "\n\n";

    print_stats(scores);

    std::sort(scores.begin(), scores.end());
    std::cout << "\n排序后: ";
    for (int x : scores) { std::cout << x << " "; }

    auto it = std::find(scores.begin(), scores.end(), 88);
    if (it != scores.end()) {
        std::cout << "\n找到 88，下标: " << (it - scores.begin());
    }

    std::reverse(scores.begin(), scores.end());
    std::cout << "\n反转后: ";
    for (int x : scores) { std::cout << x << " "; }
    std::cout << "\n";

    return 0;
}
```

编译运行：

```bash
g++ -Wall -Wextra -std=c++17 std_array.cpp -o std_array && ./std_array
```

```text
原始数据: 85 92 78 96 88

元素个数: 5
最小值: 78
最大值: 96
平均: 87.8

排序后: 78 85 88 92 96
找到 88，下标: 2
反转后: 96 92 88 85 78
```

改进是全方位的：`print_stats` 的参数是 `const std::array<int, 5>&`，类型和大小一目了然；所有 STL 算法直接使用——排序、查找、反转、求极值，都是一行调用；而且永远不会遇到数组退化丢失长度的问题。

> 如果你在代码中看到 `void func(int arr[], int n)` 这种 C 风格签名，建议改成 `void func(const std::array<int, N>& arr)`（大小固定）或 `void func(std::span<int> arr)`（大小运行时确定）。两种方式都不会丢失长度信息，比手传 `n` 安全得多。

## 小结

- `std::array<T, N>` 在栈上分配，和 C 数组一样紧凑，但没有数组退化问题
- 访问元素用 `[]`（无检查）或 `at()`（越界抛异常），与 C API 交互用 `data()`
- 拥有真正的值语义——可以拷贝、赋值、传参、返回，这是相比 C 数组最大的优势
- `fill()`、`swap()` 和迭代器接口让它与 STL 算法无缝配合
- 零开销抽象——运行时性能和 C 数组完全等价

### 常见错误

| 错误 | 原因 | 解决方法 |
|------|------|----------|
| 不初始化就读取 | 局部 `std::array` 不初始化时元素值未定义 | `std::array<int, N> arr = {};` 或 `arr.fill(0)` |
| `arr[arr.size()]` 越界 | 下标范围是 `[0, size())` | 用 `arr.at()` 做边界检查 |
| 对大数组按值传参 | 拷贝整个数组内容 | 使用 `const` 引用传递 |
| 试图动态改变大小 | `std::array` 大小编译期固定 | 需要动态大小请用 `std::vector` |

## 练习

### 练习一：重写 C 数组练习

把之前用 C 数组写的练习全部用 `std::array` 重写：声明、初始化、遍历、传参、查找最大值。体会两种写法在清晰度和安全性上的差别。

### 练习二：成绩排序与统计

创建 `std::array<int, 8>` 存放一组成绩，使用 `std::sort` 排序，然后输出最高分、最低分和平均分。所有统计操作要求使用 `<algorithm>` 中的函数。

### 练习三：判断元素是否存在

编写 `bool contains(const std::array<int, 5>& arr, int value)`，用 `std::find` 判断数组中是否包含指定值。在 `main` 中分别测试存在和不存在的值。

---

> **下一站**：`std::array` 搞定了固定大小的容器，但字符串呢？C 风格字符串的那些坑——手动管理 `'\0'`、容易越界、没有值语义——和 C 数组如出一辙。接下来我们就认识 `std::string`，看看现代 C++ 是如何优雅地处理字符串的。
