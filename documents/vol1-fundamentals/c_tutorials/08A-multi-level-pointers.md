---
title: "多级指针与声明读法"
description: "深入理解多级指针的内存模型和实际使用场景，区分指针数组与数组指针，掌握 cdecl 声明读法和多级 const 指针的组合"
chapter: 1
order: 11
tags:
  - host
  - cpp-modern
  - beginner
  - 入门
  - 基础
difficulty: beginner
platform: host
reading_time_minutes: 15
cpp_standard: [11]
prerequisites:
  - "指针与数组、const 和空指针"
---

# 多级指针与声明读法

上一篇我们把指针和数组、`const`、NULL 的关系理清楚了。现在来啃指针里更绕的部分——多级指针（指向指针的指针）、指针数组和数组指针那对"混淆双胞胎"，以及看到 `const int* const *` 这种声明时不至于大脑宕机的方法。

说实话，这些东西初学确实容易搞混。但笔者的经验是：不要死记硬背，掌握一套读声明的方法论之后，再复杂的声明都能拆开理解。更重要的是，C++ 里的 `unique_ptr<T[]>`、`std::span`、移动语义的指针转移，全都建立在这些底层机制之上。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 理解多级指针的内存模型和实际使用场景
> - [ ] 区分指针数组与数组指针
> - [ ] 用 cdecl 读法拆解任何 C 声明
> - [ ] 正确读写多级 const 指针声明

## 环境说明

我们接下来的所有实验都在这个环境下进行：

- 平台：Linux x86\_64（WSL2 也可以）
- 编译器：GCC 13+ 或 Clang 17+
- 编译选项：`-Wall -Wextra -std=c17`

## 第一步——搞清楚多级指针到底在指什么

### 内存模型：一环套一环

如果一个指针存储的地址指向的仍然是指针，那就是多级指针。`int*` 指向 `int`，`int**` 指向 `int*`，`int***` 指向 `int**`，以此类推。在内存里它们像一条链：

```text
int*** ppp  ──→  int** pp  ──→  int* p  ──→  int value = 42
  0x1000          0x2000         0x3000       0x4000
```text

每一级存储的都是下一级的地址。`*ppp` 得到 `pp`（`int**`），`**ppp` 得到 `p`（`int*`），`***ppp` 才是最终的 `42`。来验证一下：

```c
#include <stdio.h>

int main(void)
{
    int value = 42;
    int* p = &value;
    int** pp = &p;
    int*** ppp = &pp;

    printf("value 的地址 = %p\n", (void*)&value);
    printf("p   的值     = %p\n", (void*)p);
    printf("pp  解一次   = %p\n", (void*)*pp);
    printf("ppp 解三次   = %d\n", ***ppp);
    return 0;
}
```text

```bash
gcc -Wall -Wextra -std=c17 multi_ptr.c -o multi_ptr && ./multi_ptr
```text

运行结果：

```text
value 的地址 = 0x7ffd1234abcd
p   的值     = 0x7ffd1234abcd
pp  解一次   = 0x7ffd1234abcd
ppp 解三次   = 42
```text

很好，每一级解引用都在往链的下游走，最终拿到了 `42`。

### 什么时候用多级指针

实话说，超过两级的情况在正常项目里很少见。最常见的场景是：**想在函数内部修改一个指针变量本身**（不是它指向的数据），就需要传这个指针的地址进去：

```c
void allocate_buffer(int** out_ptr, int size)
{
    *out_ptr = (int*)malloc(size * sizeof(int));
    // 修改的是 out_ptr 指向的那个指针变量
}

int main(void)
{
    int* buffer = NULL;
    allocate_buffer(&buffer, 100);
    // 现在 buffer 指向了 malloc 分配的内存
    free(buffer);
    return 0;
}
```text

C 只有值传递，要修改 `buffer` 这个变量本身，必须传 `&buffer`——也就是 `int**`。

> ⚠️ **踩坑预警**
> 多级指针不是拿来炫技的。三级以上的指针在绝大多数项目中都不应该出现——如果你发现自己写了 `int****`，大概率是设计有问题。能用结构体封装就别裸用多级指针。

### argv——最常见的二级指针

`main` 函数的参数 `argv` 就是 `char**`：

```c
int main(int argc, char *argv[]) { /* ... */ }
int main(int argc, char **argv)    { /* ... */ }  // 完全等价
```text

`char *argv[]` 在参数列表中会退化为 `char**`，两种写法完全一样。`argv` 指向一个 `char*` 数组，数组里每个元素指向一个命令行参数字符串，最后以 `NULL` 哨兵收尾：

```text
argv
  │
  ▼
  ┌─────┐     ┌─────────────────┐
  │ ptr ├────→│ "./myprogram\0" │  argv[0]
  ├─────┤     └─────────────────┘
  │ ptr ├────→│ "hello\0"       │  argv[1]
  ├─────┤     └─────────────────┘
  │ ptr ├────→│ "world\0"       │  argv[2]
  ├─────┤     └─────────────────┘
  │ NULL │     argv[3] = NULL
  └─────┘
```text

## 第二步——分清指针数组和数组指针

`int* a[10]` 和 `int (*a)[10]` 看起来只差一对括号，含义却完全不同。这是 C 语言声明语法里最经典的"混淆双胞胎"。

### 指针数组：`int* a[10]`

`int* a[10]` 声明的是一个**数组**，里面放了 10 个 `int*` 元素：

```c
int x = 10, y = 20, z = 30;
int* arr[3] = {&x, &y, &z};

printf("%d %d %d\n", *arr[0], *arr[1], *arr[2]);
// 10 20 30
```text

内存布局——数组连续存储了三个指针值，每个指针各自指向不同的 `int`：

```text
arr[0]  arr[1]  arr[2]
  │        │       │
  ▼        ▼       ▼
 &x       &y      &z
```text

### 数组指针：`int (*a)[10]`

`int (*a)[10]` 声明的是一个**指针**，它指向一整行含有 10 个 `int` 的数组。最常见的用途是配合二维数组：

```c
int matrix[3][10] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
    {10, 11, 12, 13, 14, 15, 16, 17, 18, 19},
    {20, 21, 22, 23, 24, 25, 26, 27, 28, 29}
};

int (*row_ptr)[10] = matrix;  // 指向第一行
printf("%d\n", (*row_ptr)[2]);         // 2
printf("%d\n", (*(row_ptr + 1))[2]);   // 12，跳到第二行
```text

`row_ptr + 1` 跳过一整行（10 个 `int` = 40 字节），指向下一行。

> ⚠️ **踩坑预警**
> `*(row_ptr + 1)[2]` 不是你要的答案——`[]` 的优先级高于 `*`，所以这会先算 `(row_ptr + 1)[2]` 再解引用，结果完全不对。正确写法必须加括号：`(*(row_ptr + 1))[2]`。优先级问题是 C 语言里最容易出 bug 的地方之一。

## 第三步——掌握 cdecl 读法

有一套系统的方法可以读懂任何 C 声明，叫做"右左法则"（也叫螺旋法则）。核心规则：**从标识符开始，先往右读，再往左读，遇到括号就跳到下一层**。

以 `int* a[10]` 为例：

1. 找到标识符 `a`
2. 往右：`[10]`——"a 是一个有 10 个元素的数组"
3. 往左：`int*`——"元素类型为 int 指针"
4. 合起来：**a 是一个有 10 个 int 指针元素的数组（指针数组）**

以 `int (*a)[10]` 为例：

1. 标识符 `a`
2. 往右被括号挡住，先往左：`*`——"a 是一个指针"
3. 跳出括号，往右：`[10]`——"指向一个有 10 个元素的数组"
4. 再往左：`int`——"元素类型为 int"
5. 合起来：**a 是一个指向含 10 个 int 元素的数组的指针（数组指针）**

再来看函数指针：`int (*func)(double)`

1. 标识符 `func`
2. 括号挡住，往左：`*`——"func 是一个指针"
3. 跳出括号，往右：`(double)`——"指向接受 double 参数的函数"
4. 往左：`int`——"返回 int"
5. 合起来：**func 是一个函数指针，指向接受 double 返回 int 的函数**

这套方法练几次就熟了，以后看到任何奇怪的声明都不会慌。你也可以用 [cdecl.org](https://cdecl.org/) 这个在线工具来验证你的解读。

> ⚠️ **踩坑预警**
> `int* a, b` 这行声明里，`a` 是 `int*`，但 `b` 只是 `int`——不是两个指针。`*` 跟着声明符走，不跟着类型走。如果真的要声明两个指针，必须写 `int *a, *b`。这个坑不知道绊倒过多少人。

## 第四步——const 和多级指针的组合

`const` 和单级指针的组合在上一篇已经讲过。现在来看多级的情况——核心原则不变：**`const` 修饰的是它左边紧邻的类型（如果在最左边，则修饰右边的类型）**。

### 复习：单级 const 指针

```c
const int* p1;              // 指向 const int 的指针，不能通过 p1 改值，但 p1 可改方向
int* const p2 = &v;         // const 指针，p2 不能改方向，但可通过它改值
const int* const p3 = &v;   // 都锁死了
```text

### 多级 const 指针

当出现 `int**` 时，`const` 可以加在不同位置：

```c
int value = 42;
int* ptr = &value;

// 底层 const：指向的指针是只读的
int* const* pp1 = &ptr;
// pp1 可以改，*pp1 不能改，**pp1 可以改

// 顶层 const：pp2 本身是只读的
int** const pp2 = &ptr;
// pp2 不能改，*pp2 可以改，**pp2 可以改

// 双重 const
const int* const* pp3 = &ptr;
// pp3 可以改，*pp3 不能改，**pp3 不能改
```text

读法还是用右左法则逐层拆解。以 `const int* const* p` 为例：`p` 是指针 → 指向一个 `const` 的指针 → 那个指针指向 `const int`。

这种东西在实战中确实不常见，但理解它的读法很重要——C++ 标准库的函数签名、模板错误信息里经常会出现类似的复杂类型。

## C++ 衔接

C 的多级指针机制在 C++ 中都有对应的现代替代，理解底层原理有助于更好地使用这些高层工具。

`std::unique_ptr<T[]>` 自动管理动态数组，不需要手动 `malloc`/`free`。C 语言里用 `int**` 手动管理二维数组的那种痛苦（分配、逐行释放、容易忘），在 C++ 里可以一行搞定：

```cpp
auto matrix = std::make_unique<int[]>(rows * cols);
// 用 matrix[i * cols + j] 访问，离开作用域自动释放
```text

移动语义本质上就是指针的转移——不是拷贝数据，而是把资源的所有权"偷"过来再把源对象置空，这和 C 里手动交换指针然后置空如出一辙，只是 C++ 把这个模式标准化了。

`std::span<const int>` 把 C 函数里"指针+长度"的经典组合打包成一个类型安全的对象，不需要手动管长度，还能从数组、vector、array 自动构造。

`std::reference_wrapper<int>` 提供了可重新绑定的引用语义，在容器里存放"引用"时可以替代多级指针。

这些内容我们会在后续 C++ 教程中深入讨论。现在只需要记住核心思路：**C++ 的哲学是用类型系统来自动管理资源，而不是靠程序员的自觉性**。

## 小结

多级指针的核心逻辑其实很简单：每一级存的是下一级的地址，解引用就是在链上往下游走。真正容易混淆的是指针数组和数组指针——记住"先看括号再读方向"就行。cdecl 读法是这一篇最重要的工具技能，练几次就能拆解任何声明。多级 const 用右左法则逐层分析，不要一口气读。

## 练习

### 练习：动态二维数组的分配与释放

用多级指针实现一个动态二维数组的分配、填充和释放。请自行实现以下三个函数：

```c
/// @brief 分配 rows x cols 的动态二维数组
/// @param rows 行数
/// @param cols 列数
/// @return 指向二维数组的二级指针，失败返回 NULL
int** allocate_matrix(int rows, int cols);

/// @brief 释放动态二维数组
/// @param matrix 二级指针
/// @param rows 行数（用于逐行释放）
void free_matrix(int** matrix, int rows);

/// @brief 将二维数组的所有元素填充为指定值
/// @param matrix 二级指针
/// @param rows 行数
/// @param cols 列数
/// @param value 填充值
void fill_matrix(int** matrix, int rows, int cols, int value);
```text

提示：分配时先分配一个指针数组（`int**` 指向的那一维），然后对每一行分别 `malloc`。释放时顺序反过来——先释放每一行，再释放指针数组本身。

## 参考资源

- [C 声明语法 - cppreference](https://en.cppreference.com/w/c/language/declarations)
- [cdecl: C 声明翻译工具](https://cdecl.org/)
