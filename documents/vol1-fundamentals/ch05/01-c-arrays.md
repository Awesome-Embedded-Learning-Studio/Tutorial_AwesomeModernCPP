---
title: "C 风格数组"
description: "掌握 C 风格数组的声明、初始化和多维用法，理解数组退化及其对函数传参的影响"
chapter: 5
order: 1
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
  - "智能指针预告"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# C 风格数组

到目前为止，我们处理数据的方式都是"一个变量存一个值"。但现实世界的数据很少是孤立存在的——一组传感器读数、一串字符、一个矩阵、一张成绩表，这些东西天然就是"一堆相同类型的数据排成一排"。数组（array）就是 C 和 C++ 提供的最原始的、用来存储这种"同类型连续数据"的机制。

说实话，C 风格数组的问题确实不少——不能赋值、不能返回、传参丢长度信息、没有边界检查——但它是理解内存布局的绝佳入口。只有搞明白了这些痛点，才能理解为什么 C++ 要引入 `std::array`。这一章我们把 C 风格数组从里到外拆一遍。

## 声明和初始化——数组长什么样

声明一个数组，核心语法就是在变量名后面加方括号，里面写上元素个数：

```cpp
int scores[5];  // 5 个 int，未初始化（值是不确定的）
```

这段代码告诉编译器：在栈上连续分配 5 个 `int` 的空间。注意，**未初始化的局部数组里装的是垃圾值**——不是零。所以我们几乎总是在声明的同时进行初始化。

```cpp
int scores[5] = {90, 85, 78, 92, 88};
```

这五个值按顺序填入数组的五个位置。如果初始值少于数组大小，剩下的元素会被自动初始化为零：

```cpp
int data[5] = {10, 20};  // data = {10, 20, 0, 0, 0}
```

反过来，初始值多于数组大小则编译直接报错。

如果初始化列表给了足够多的值，数组的大小可以省略，让编译器自己数：

```cpp
int primes[] = {2, 3, 5, 7, 11, 13};  // 编译器推断大小为 6
```

这种写法的好处是以后增减元素时不需要同步修改方括号里的数字。

知道数组有多少个元素，有一个经典公式：

```cpp
int primes[] = {2, 3, 5, 7, 11, 13};
constexpr int kCount = sizeof(primes) / sizeof(primes[0]);  // kCount = 6
```

`sizeof(primes)` 是整个数组占的字节数，`sizeof(primes[0])` 是单个元素占的字节数，相除得到元素个数。这个技巧在 C 代码里随处可见，但后面我们会讲到它的局限性。

## 访问元素——从零开始的世界

C++ 数组的下标从 0 开始。一个大小为 5 的数组，有效下标是 0 到 4。这不是随意的设计选择——`arr[i]` 在底层等价于 `*(arr + i)`，即从数组起始地址向后偏移 `i` 个元素的位置。

```cpp
int scores[5] = {90, 85, 78, 92, 88};

std::cout << scores[0] << std::endl;  // 90（第一个元素）
std::cout << scores[4] << std::endl;  // 88（最后一个元素）
```

> **踩坑预警**：C 风格数组不进行任何边界检查。`scores[5]`、`scores[100]`、`scores[-1]` 这些越界访问编译时不报错，运行时也不抛异常——它们会默默地读写数组之外的内存。这种未定义行为可能恰好"看上去正常"，也可能立刻崩溃，还可能悄无声息地修改其他变量的值。调试这种问题的时候血压真的会拉满。

修改数组元素也是通过下标：

```cpp
scores[2] = 80;  // 把第三个元素从 78 改成 80
```

遍历数组有几种方式。最传统的是下标循环，C++11 引入的范围 `for` 更简洁：

```cpp
// 范围 for 遍历（只在声明作用域内有效）
for (int s : scores) {
    std::cout << s << " ";
}
// 输出: 90 85 80 92 88
```

范围 `for` 只能用于"知道自身大小"的数组——传给函数后就不好使了，后面会解释原因。

## 多维数组——矩阵的内存真相

C++ 支持多维数组，本质上是"数组的数组"。最常见的是二维数组，用来表示矩阵或表格：

```cpp
int matrix[3][4] = {
    {1,  2,  3,  4},
    {5,  6,  7,  8},
    {9, 10, 11, 12}
};
```

这段代码声明了一个 3 行 4 列的矩阵。`matrix[0]` 是第一行（本身是一个包含 4 个 `int` 的数组），`matrix[0][2]` 是第一行第三个元素，值为 3。

关键问题：这个矩阵在内存里长什么样？答案是**按行连续存储**（row-major），所有元素紧密排列在一块连续内存里：

```text
地址:   低地址 →→→→→→→→→→→→→→→→→→→→→→→ 高地址
内容: 1 2 3 4 5 6 7 8 9 10 11 12
       ↑--- 行0 ---↑--- 行1 ---↑--- 行2 ---↑
```

`matrix[1][0]` 在内存中紧挨着 `matrix[0][3]`。理解这一点对后续理解指针和数组的关系至关重要。

遍历二维数组用嵌套循环：

```cpp
for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 4; ++j) {
        std::cout << matrix[i][j] << "\t";
    }
    std::cout << std::endl;
}
```

输出：

```text
1  2  3  4
5  6  7  8
9 10 11 12
```

这里有一个性能细节：因为内存按行存储，外层遍历行、内层遍历列是最缓存友好的方式。如果颠倒内外层循环，CPU 每次访问都在内存中跳跃，缓存命中率大幅下降，大规模数据中差异可达数倍。

## 数组传参——一切噩梦的起点

现在来到 C 风格数组最大的坑：把数组传给函数时，它会发生**退化**（decay）。

```cpp
void print_array(int arr[])
{
    std::cout << "sizeof(arr) = " << sizeof(arr) << std::endl;
}

int main()
{
    int data[5] = {1, 2, 3, 4, 5};
    std::cout << "sizeof(data) = " << sizeof(data) << std::endl;
    print_array(data);
    return 0;
}
```

输出：

```text
sizeof(data) = 20
sizeof(arr) = 8
```

在 `main` 里，`sizeof(data)` 是 20（5 个 `int`，每个 4 字节）。但到了函数里，`sizeof(arr)` 变成了 8——这是 64 位系统上指针的大小，不是数组的大小。

这就是数组退化：数组作为参数传递时自动退化为指向首元素的指针，函数签名里的 `int arr[]` 和 `int* arr` 完全等价。

> **踩坑预警**：数组退化意味着函数内部完全丢失了数组的大小信息。你没法用 `sizeof` 算出元素个数，也没法用范围 `for` 循环遍历它。如果你在函数里写 `sizeof(arr) / sizeof(arr[0])`，得到的不是数组长度，而是"一个指针除以一个 int"的无意义结果。这就是为什么 C 风格的函数几乎总是要求你把数组长度作为额外的参数传进来。

所以正确的做法是显式传递大小：

```cpp
void print_array(const int arr[], int size)
{
    for (int i = 0; i < size; ++i) {
        std::cout << arr[i] << " ";
    }
    std::cout << std::endl;
}
```

用 `const` 修饰是因为函数只读取不修改，这是一个好习惯——编译器会在你不小心修改它时报错。

### 多维数组传参

多维数组传参更麻烦——你必须告诉编译器第二维（及更高维）的大小，否则编译器无法计算元素地址：

```cpp
// 编译器需要知道第二维是 4 才能正确计算 matrix[i][j] 的地址
void print_matrix(int matrix[][4], int rows)
{
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < 4; ++j) {
            std::cout << matrix[i][j] << "\t";
        }
        std::cout << std::endl;
    }
}
```

这直接导致函数只能接受第二维恰好是 4 的数组，3x3 矩阵就不适用了。这也是 C 风格数组在实际项目中非常难用的原因之一。

## C 数组 vs 现代替代品

说了这么多，我们已经感受到了 C 风格数组的各种痛点。它们不能直接赋值——`int b[3] = a;` 编译器直接拒绝；不能作为函数返回值——返回局部数组的指针更危险，因为栈帧回收后内存已无效；会退化成指针丢失大小信息；长度必须在编译期确定，不支持运行时动态大小。

> **踩坑预警**：C 风格数组还有一个容易被忽略的陷阱——你无法用 `auto` 推断数组类型。`auto a = {1,2,3};` 推断出的是 `std::initializer_list<int>`，不是数组。`auto b = arr;`（`arr` 是数组）推断出的是指针，不是数组的拷贝。这些隐式行为都和数组退化有关，稍不留神就会写出和预期完全不同的代码。

这些问题正是 C++11 引入 `std::array` 的原因——它在栈上分配内存（和 C 数组一样），但提供了赋值、比较、范围 `for`、`.size()` 等现代特性，而且不会退化为指针。但理解 C 风格数组依然重要，因为你会在遗留代码、C 语言库、嵌入式代码中不断遇到它们。

## 实战演练——arrays.cpp

把这一章的核心知识点整合到一个程序里：

```cpp
// arrays.cpp
// C 风格数组综合演练：初始化、遍历、函数传参、矩阵操作

#include <iostream>

/// @brief 打印一维数组
void print_array(const int arr[], int size)
{
    for (int i = 0; i < size; ++i) {
        std::cout << arr[i];
        if (i < size - 1) {
            std::cout << ", ";
        }
    }
    std::cout << std::endl;
}

/// @brief 计算数组元素之和
int array_sum(const int arr[], int size)
{
    int total = 0;
    for (int i = 0; i < size; ++i) {
        total += arr[i];
    }
    return total;
}

/// @brief 打印矩阵（第二维固定为 4）
void print_matrix(const int matrix[][4], int rows)
{
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < 4; ++j) {
            std::cout << matrix[i][j] << "\t";
        }
        std::cout << std::endl;
    }
}

/// @brief 将 3x4 矩阵转置为 4x3 矩阵
void transpose_3x4(const int src[][4], int dst[][3])
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            dst[j][i] = src[i][j];
        }
    }
}

int main()
{
    // --- 初始化方式展示 ---
    std::cout << "=== 初始化方式 ===" << std::endl;

    int full_init[5] = {10, 20, 30, 40, 50};
    std::cout << "完全初始化: ";
    print_array(full_init, 5);

    int partial_init[5] = {1, 2};  // 后面自动填 0
    std::cout << "部分初始化: ";
    print_array(partial_init, 5);

    int zero_init[5] = {};  // 全部填 0
    std::cout << "零初始化:   ";
    print_array(zero_init, 5);

    int deduced[] = {2, 3, 5, 7, 11, 13};
    constexpr int kDeducedCount = sizeof(deduced) / sizeof(deduced[0]);
    std::cout << "大小推断:   ";
    print_array(deduced, kDeducedCount);
    std::cout << std::endl;

    // --- 遍历与求和 ---
    std::cout << "=== 遍历与求和 ===" << std::endl;
    int scores[] = {90, 85, 78, 92, 88};
    constexpr int kScoreCount = sizeof(scores) / sizeof(scores[0]);

    std::cout << "成绩: ";
    print_array(scores, kScoreCount);

    int total = array_sum(scores, kScoreCount);
    double average = static_cast<double>(total) / kScoreCount;
    std::cout << "总分: " << total << std::endl;
    std::cout << "均分: " << average << std::endl;
    std::cout << std::endl;

    // --- 矩阵操作 ---
    std::cout << "=== 矩阵操作 ===" << std::endl;
    int matrix[3][4] = {
        {1,  2,  3,  4},
        {5,  6,  7,  8},
        {9, 10, 11, 12}
    };

    std::cout << "原始矩阵 (3x4):" << std::endl;
    print_matrix(matrix, 3);

    int transposed[4][3] = {};
    transpose_3x4(matrix, transposed);

    std::cout << std::endl << "转置矩阵 (4x3):" << std::endl;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 3; ++j) {
            std::cout << transposed[i][j] << "\t";
        }
        std::cout << std::endl;
    }

    return 0;
}
```

编译运行：`g++ -std=c++17 -Wall -Wextra -o arrays arrays.cpp && ./arrays`

预期输出：

```text
=== 初始化方式 ===
完全初始化: 10, 20, 30, 40, 50
部分初始化: 1, 2, 0, 0, 0
零初始化:   0, 0, 0, 0, 0
大小推断:   2, 3, 5, 7, 11, 13

=== 遍历与求和 ===
成绩: 90, 85, 78, 92, 88
总分: 433
均分: 86.6

=== 矩阵操作 ===
原始矩阵 (3x4):
1  2  3  4
5  6  7  8
9  10 11 12

转置矩阵 (4x3):
1  5  9
2  6  10
3  7  11
4  8  12
```

验证一下：90 + 85 + 78 + 92 + 88 = 433，均分 86.6，没问题。矩阵转置后第 0 行变成第 0 列，正确。

## 动手试试

光看不练等于没学，建议每题都动手写一遍。

### 练习一：数组求和与均值

写一个程序，声明一个包含 10 个整数的数组，写两个函数分别计算总和和平均值（平均值返回 `double`）。验证方法：手动加一遍，和程序输出对比。

### 练习二：矩阵转置

写一个函数，将 N x M 的二维数组转置为 M x N。先用固定大小（2x3 转置为 3x2）实现，再思考：如果行数列数也要是参数，C 风格数组能做到吗？

### 练习三：修复越界 bug

下面这段代码有越界访问的 bug，找到并修复它：

```cpp
int data[5] = {10, 20, 30, 40, 50};
for (int i = 0; i <= 5; ++i) {  // 提示：仔细看循环条件
    std::cout << data[i] << std::endl;
}
```

这种 off-by-one 错误在初学者代码里非常常见。

## 小结

这一章我们拆解了 C 风格数组。数组在内存中连续存储，下标从 0 开始，`sizeof(arr) / sizeof(arr[0])` 可以获取元素个数（但只在声明作用域内有效）。多维数组按行连续存储，按行优先遍历对缓存更友好。数组传参时退化为指针，丢失大小信息。不能赋值、不能返回、没有边界检查——这些痛点正是 `std::array` 存在的理由。

下一章我们就来看看 `std::array`——在保持 C 数组性能优势的同时，补齐了所有短板的现代替代品。
