---
title: "指针与数组、const 和空指针"
description: "深入理解数组名退化为指针的机制、const 与指针的四种组合、NULL 指针和野指针的防范，为学习 C++ 引用和智能指针打下基础"
chapter: 1
order: 10
tags:
  - host
  - cpp-modern
  - beginner
  - 入门
difficulty: beginner
platform: host
reading_time_minutes: 15
cpp_standard: [11]
prerequisites:
  - "指针入门：地址的世界"
---

# 指针与数组、const 和空指针

上一篇里我们掌握了指针的基本操作——声明、初始化、取地址、解引用、指针运算。现在我们来啃指针里几个比较绕但非常重要的应用：数组和指针之间到底是什么关系，`const` 和指针组合在一起有多少种意思，以及 NULL 指针和野指针为什么这么危险。

先别急，我们一个一个来。这些内容看着多，但核心逻辑其实很清楚。

> **学习目标**
> 完成本章后，你将能够：
>
> - [ ] 理解数组名退化为指针的机制和两个例外情况
> - [ ] 正确读写 `const` 与指针的四种组合声明
> - [ ] 区分 NULL 指针和野指针，掌握防御方法

## 环境说明

我们接下来的所有实验都在这个环境下进行：

- 平台：Linux x86\_64（WSL2 也可以）
- 编译器：GCC 13+ 或 Clang 17+
- 编译选项：`-Wall -Wextra -std=c17`

## 第一步——数组名到底是什么

### "退化"——一个核心规则

在 C 语言里有一个非常重要的规则：**在大多数语境下，数组名会自动退化为指向首元素的指针**。这个规则听起来很学术，但其实很好理解——数组名 `numbers` 本身代表一整块连续的内存，但当你把它赋给一个指针、或者传给函数的时候，编译器只把这块内存的起始地址传过去，数组的长度信息就"丢"了。

```c
int numbers[5] = {1, 2, 3, 4, 5};
int* ptr = numbers;      // 合法：numbers 退化为 &numbers[0]
```text

`numbers` 本身的类型是 `int[5]`（一个包含 5 个 int 的数组），但在赋值给指针的时候，它自动转换成了 `int*`（指向首元素的指针）。这意味着 `numbers[i]` 和 `*(numbers + i)` 是完全等价的——下标运算符 `[]` 本质上就是指针算术的语法糖。

正因为如此，我们可以用指针来遍历数组：

```c
int numbers[5] = {10, 20, 30, 40, 50};

for (int* p = numbers; p < numbers + 5; p++) {
    printf("%d ", *p);
}
```text

来验证一下，编译运行：

```bash
gcc -Wall -Wextra -std=c17 array_ptr.c -o array_ptr && ./array_ptr
```text

运行结果：

```text
10 20 30 40 50
```text

### 但是——数组不是指针

事情的关键在这里：数组名只是"经常退化为指针"，**数组本身不是指针**。有两个场景下数组名不会退化：

第一，`sizeof` 运算符。`sizeof(numbers)` 返回的是整个数组的字节大小（5 × 4 = 20 字节），而不是一个指针的大小（4 或 8 字节）。这是我们在上一篇里用来计算数组元素个数的手法：`sizeof(numbers) / sizeof(numbers[0])`。

第二，`&` 运算符。`&numbers` 的类型是"指向整个数组的指针"（`int(*)[5]`），不是"指向指针的指针"（`int**`）。它和 `numbers` 的数值相同（都是数组首字节的地址），但类型不同，指针运算的步长也不同。

来验证一下这些区别：

```c
#include <stdio.h>

int main(void)
{
    int numbers[5] = {10, 20, 30, 40, 50};

    printf("sizeof(numbers)    = %zu（整个数组）\n", sizeof(numbers));
    printf("sizeof(&numbers)   = %zu（指针大小）\n", sizeof(&numbers));
    printf("numbers 的值       = %p\n", (void*)numbers);
    printf("&numbers 的值      = %p\n", (void*)&numbers);
    printf("numbers + 1        = %p（跳过一个 int）\n", (void*)(numbers + 1));
    printf("&numbers + 1       = %p（跳过整个数组）\n", (void*)(&numbers + 1));

    return 0;
}
```text

运行结果：

```text
sizeof(numbers)    = 20（整个数组）
sizeof(&numbers)   = 8（指针大小）
numbers 的值       = 0x7ffd1234abcd
&numbers 的值      = 0x7ffd1234abcd
numbers + 1        = 0x7ffd1234abd1（跳过一个 int，+4）
&numbers + 1       = 0x7ffd1234abe1（跳过整个数组，+20）
```text

很好，`numbers` 和 `&numbers` 的数值相同，但 `numbers + 1` 只跳过了 4 字节（一个 `int`），而 `&numbers + 1` 跳过了 20 字节（整个数组）。这就是"类型不同，步长不同"。

> ⚠️ **踩坑预警**
> 数组传给函数后一定会退化为指针——在函数内部 `sizeof(arr)` 返回的是指针大小，不是数组大小。所以如果你需要在函数里知道数组长度，必须另外传一个长度参数进去。

## 第二步——const 与指针的四种组合

`const` 和指针的组合是面试里的经典问题，也是实际编码中频繁用到的东西。总共有四种组合方式，我们从最直观的开始逐个拆解。

### 1. 指向 const 数据的非 const 指针

```c
const int* p1 = &value;
// *p1 = 100;   // 错误：不能通过 p1 修改指向的数据
p1 = &other;    // 合法：指针本身可以指向别的地方
```text

`const int*` 的意思是"p1 指向的 int 是只读的"——你不能通过 `p1` 去改那个值，但 `p1` 本身可以指向其他变量。注意这里 `value` 本身不一定非得是 `const` 的，只是你承诺不通过 `p1` 这个途径去修改它。这个用法在函数参数里极为常见——`void process(const int* data)` 就是在告诉调用者"放心，我保证不碰你的数据"。

### 2. 指向非 const 数据的 const 指针

```c
int* const p2 = &value;
*p2 = 100;      // 合法：可以修改指向的数据
// p2 = &other;  // 错误：指针本身不可变
```text

指针本身是 `const` 的——一旦初始化就永远指向同一个地址，但你可以通过它修改那块内存里的数据。这种用法在嵌入式开发里很常见，比如固定地址的硬件寄存器映射：

```c
volatile unsigned int* const kGpioBase = (volatile unsigned int*)0x40020000;
```text

指针的值（地址）固定不变，但可以通过它读写寄存器。

### 3. 指向 const 数据的 const 指针

```c
const int* const p3 = &value;
// *p3 = 100;    // 错误
// p3 = &other;  // 错误
```text

两边都锁死了——指针不能改方向，数据也不能通过指针改。这通常用于只读的硬件寄存器或常量查找表的访问。

### 4. 普通的 `int*`

这就是最普通的 `int* p`，两边都能改，没什么特别的约束。

### 怎么读这些声明

一个实用的读法技巧：看 `const` 出现在 `*` 的左边还是右边。

- `const` 在 `*` **左边**：修饰的是**指向的数据**（数据不可改）
- `const` 在 `*` **右边**：修饰的是**指针本身**（方向不可改）
- 两边都有：都不可改

> ⚠️ **踩坑预警**
> 从右往左读声明也是个好方法：`const int* p` → "p is a pointer to int const"（指向 const int 的指针）；`int* const p` → "p is a const pointer to int"（指向 int 的 const 指针）。

## 第三步——NULL 指针和野指针

### NULL——"我什么都没指"

`NULL` 是一个宏，值为 `(void*)0`，表示"不指向任何有效的内存地址"。解引用 NULL 指针是未定义行为——在大多数系统上会触发段错误（SIGSEGV），程序直接崩溃。

段错误听起来很糟糕，但它其实是一种"好的崩溃"——问题立刻暴露，你拿个调试器一看就知道是空指针解引用。相比之下，下面要说的野指针才是真正可怕的东西。

### 野指针——代码中的定时炸弹

野指针（wild pointer）是指向了无效内存的指针。它通常有三种来源：

第一种是**未初始化的指针**——声明了但没赋值，里面是栈上的随机值，这个地址可能指向任何地方。第二种是**悬空指针**（dangling pointer）——指针曾经指向有效内存，但那块内存已经被释放了（`free` 之后继续使用指针）。第三种是**越界访问**——指针运算跑出了合法范围。

```c
// 未初始化——最经典的野指针
int* wild;
*wild = 42;   // 未定义行为：往随机地址写入 42

// 悬空指针
int* dangling = (int*)malloc(sizeof(int));
free(dangling);
*dangling = 42;  // 未定义行为：内存已经释放了

// 好习惯：释放后置 NULL
dangling = NULL;
```text

野指针的可怕之处在于它不一定立刻崩溃——它可能碰巧指向一块可写的内存，你的程序"看起来"正常运行，但某个不相关的变量已经被你悄悄改掉了。这种 bug 的症状和原因之间可能隔了十万八千里，查起来让人血压拉满。

> ⚠️ **踩坑预警**
> 野指针制造的是"薛定谔的 bug"——在你的程序里，它可能看起来一切正常，直到某天换了个编译器或者开了优化，突然就崩了。而且崩溃的位置往往离真正的 bug 很远，查起来极其痛苦。

### 三条防御规则

最好的防御措施其实很简单，记住这三条就行：

1. **指针声明时立即初始化**——哪怕初始化为 `NULL` 也行
2. **`free` 之后立刻置 `NULL`**——防止后续误用
3. **使用指针前先检查是否为 `NULL`**——加一层保护

```c
int* safe_ptr = NULL;

// ... 某处分配了内存 ...

if (safe_ptr != NULL) {
    *safe_ptr = 42;   // 安全：确认非空才使用
}
```text

这三条规则能帮你避开绝大多数指针相关的灾难。笔者在这里真诚建议：把这三条刻进肌肉记忆里，以后写代码会少掉很多头发。

## C++ 衔接

C 语言的原始指针功能强大但责任全在程序员。C++ 在这个基础上做了几件非常关键的事情。

首先是**引用**（reference）。`int& r = value` 本质上是编译器自动解引用的 const 指针——必须在声明时初始化，一旦绑定就不能改变，使用时不需要 `*`，语法上就像直接操作原始变量。引用不可能为 NULL（好吧，严格来说你可以构造悬空引用，但那是故意作死），也不可能指向未初始化的内存。C++ 函数参数优先传引用而不是传指针。

然后是**智能指针**。`std::unique_ptr` 和 `std::shared_ptr` 用 RAII 机制自动管理内存生命周期——指针超出作用域时自动释放内存，从根本上消除了手动 `malloc`/`free` 导致的内存泄漏和悬空指针问题。

```cpp
// C++ 智能指针——先睹为快
#include <memory>

std::unique_ptr<int> p = std::make_unique<int>(42);
// *p == 42，使用方式和原始指针一样
// 离开作用域时自动 delete，不需要手动释放
```text

这些内容我们会在后续的 C++ 教程中深入讨论。现在只需要知道一个核心思路：**C++ 的哲学是用类型系统和对象生命周期来自动化管理，而不是靠程序员的自觉性**。

## 小结

我们来梳理一下这篇的核心要点。数组名在大多数语境下会退化为指向首元素的指针，但 `sizeof` 和 `&` 是两个例外——在这些场景下数组名保持"数组"的身份。`const` 和指针有四种组合，记住"const 在 `*` 左边修饰数据，在右边修饰指针本身"就行。NULL 指针虽然会段错误，但那是"好的崩溃"；野指针才是真正的定时炸弹，记住三条防御规则（声明即初始化、free 后置 NULL、使用前检查）就能避开绝大多数灾难。

到这里我们已经把指针的基础打牢了。接下来我们要学习函数——怎么组织代码让它更好复用、更好维护。

## 练习

### 练习 1：指针版线性搜索

实现一个线性搜索函数，返回目标值在数组中首次出现的指针。如果未找到，返回 `NULL`。

```c
/// @brief 在 int 数组中线性搜索目标值
/// @param data 数组首元素地址
/// @param count 元素个数
/// @param target 要搜索的值
/// @return 指向目标元素的指针，未找到则返回 NULL
const int* linear_search(const int* data, size_t count, int target);
```text

### 练习 2：指针版数组反转

实现一个原地反转数组的函数，只使用指针算术（两个指针从两端向中间靠拢），不使用数组下标：

```c
/// @brief 原地反转 int 数组
/// @param data 数组首元素地址
/// @param count 元素个数
void reverse_array(int* data, size_t count);
```text

### 练习 3：const 练习

判断以下每个声明中，哪些操作是合法的，哪些会编译错误：

```c
int value = 42, other = 100;

const int* p1 = &value;
int* const p2 = &value;
const int* const p3 = &value;

// 对每个指针 p1/p2/p3，判断以下操作是否合法：
// *px = 50;      // 通过指针修改数据
// px = &other;   // 修改指针指向
```text

## 参考资源

- [cppreference: 指针声明](https://en.cppreference.com/w/c/language/pointer)
- [cppreference: NULL](https://en.cppreference.com/w/c/types/NULL)
- [cppreference: 数组到指针的退化](https://en.cppreference.com/w/c/language/conversion)
