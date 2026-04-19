---
chapter: 1
cpp_standard:
- 11
description: 理解 C 函数的声明定义调用机制、值传递本质、指针参数、返回值策略和递归原理，为 C++ 引用传递和函数重载打好基础
difficulty: beginner
order: 7
platform: host
prerequisites:
- 指针与数组、const 和空指针
reading_time_minutes: 12
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: 函数基础与参数传递
---
# 函数基础与参数传递

到现在为止我们写的代码都塞在 `main` 函数里。但现实世界的程序不会这样——一个项目动辄几万行代码，如果全挤在一个函数里，那基本没法维护。函数就是 C 语言模块化编程的基本单元：把一段逻辑封装起来，给它起个名字，需要的时候调用就行。

听起来很简单，但函数背后的机制——参数是怎么传进去的、返回值是怎么回来的、栈帧是怎么运作的——理解到位了，后面学 C++ 的引用传递、函数重载、模板的时候才不会感到困惑。

> **学习目标**
> 完成本章后，你将能够：
>
> - [ ] 正确声明、定义和调用 C 函数
> - [ ] 理解 C 只有值传递的本质
> - [ ] 掌握通过指针实现多返回值的技巧
> - [ ] 了解递归的原理和栈溢出风险

## 环境说明

我们接下来的所有实验都在这个环境下进行：

- 平台：Linux x86\_64（WSL2 也可以）
- 编译器：GCC 13+ 或 Clang 17+
- 编译选项：`-Wall -Wextra -std=c17`

## 第一步——函数的声明与定义

### 先声明，后使用

C 编译器是从上往下处理代码的。如果你在 `main` 里调用了一个函数，但那个函数定义在 `main` 后面，编译器在遇到调用点的时候还不知道这个函数的存在。所以我们需要**函数声明**（也叫函数原型）来提前告诉编译器函数的"签名"——参数类型和返回类型：

```c
#include <stdio.h>

// 函数声明（原型）——提前告诉编译器这个函数长什么样
int calculate_checksum(const unsigned char* data, unsigned int length);

int main(void) {
    unsigned char buffer[] = {0x01, 0x02, 0x03, 0x04};
    int checksum = calculate_checksum(buffer, 4);
    printf("Checksum: 0x%02X\n", checksum);
    return 0;
}

// 函数定义——函数真正的实现
int calculate_checksum(const unsigned char* data, unsigned int length) {
    int sum = 0;
    for (unsigned int i = 0; i < length; i++) {
        sum += data[i];
    }
    return sum & 0xFF;
}
```text

来验证一下，编译运行：

```bash
gcc -Wall -Wextra -std=c17 checksum.c -o checksum && ./checksum
```text

运行结果：

```text
Checksum: 0x0a
```text

在实际项目中，函数声明通常放在头文件（`.h`）里，函数定义放在源文件（`.c`）里。其他需要调用这个函数的文件只需 `#include` 对应的头文件就行——这就是模块化的基本模式，我们在编译基础那一篇已经见过了。

函数原型中的参数名可以省略（只留类型），但保留参数名是更好的实践——它充当了文档，让读代码的人一眼就知道每个参数的用途。

## 第二步——C 只有值传递

这是理解 C 函数最关键的一点：**C 语言只有值传递**。所有参数在传递时都会被拷贝一份，函数内部拿到的是原始数据的副本，对副本的修改不会影响原始数据。

### 副本不会被改——值传递的安全之处

```c
void try_modify(int x) {
    x = 100;  // 修改的是 x 的副本
}

int main(void) {
    int value = 42;
    try_modify(value);
    printf("%d\n", value);  // 仍然是 42
    return 0;
}
```text

`try_modify` 拿到的是 `value` 的一个副本（`x`），修改 `x` 不会影响外面的 `value`。这看起来像是"没起作用"，但换个角度想——这也意味着函数不会意外修改调用者的数据，这是一种安全保护。

### 传指针——绕过值传递的限制

如果我们确实需要让函数修改调用者的变量呢？答案是传地址（指针）。注意这里传递的仍然是值——只不过这个"值"是一个地址：

```c
void swap(int* a, int* b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

int main(void) {
    int x = 10, y = 20;
    swap(&x, &y);
    printf("x=%d, y=%d\n", x, y);
    return 0;
}
```text

`swap` 接收的是 `x` 和 `y` 的地址（指针的值拷贝），然后通过解引用 `*` 直接读写那块内存。指针本身是拷贝的，但它指向的内存是原始数据。

来验证一下：

```bash
gcc -Wall -Wextra -std=c17 swap_demo.c -o swap_demo && ./swap_demo
```text

运行结果：

```text
x=20, y=10
```text

> ⚠️ **踩坑预警**
> 传递大型结构体时如果用值传递，整块数据都会被拷贝——既浪费栈空间又消耗时间。应该传指针（通常是 `const` 指针），只拷贝一个地址（4 或 8 字节）就能让函数访问整个结构体。

## 第三步——返回值与多返回值

C 函数只能返回一个值。如果需要返回多个结果，常用的技巧有两种。

### 方法一：通过指针参数"返回"

```c
void divmod(int dividend, int divisor, int* quotient, int* remainder) {
    *quotient = dividend / divisor;
    *remainder = dividend % divisor;
}

int main(void) {
    int q, r;
    divmod(17, 5, &q, &r);
    printf("17 / 5 = %d 余 %d\n", q, r);
    return 0;
}
```text

这是一种很常见的 C 语言模式——需要"返回"的值通过指针参数传出去，函数的返回值本身通常用来表示成功或失败。

### 方法二：返回结构体

```c
typedef struct {
    int quotient;
    int remainder;
} DivResult;

DivResult div_with_remainder(int dividend, int divisor) {
    DivResult result;
    result.quotient = dividend / divisor;
    result.remainder = dividend % divisor;
    return result;
}
```text

现代编译器对返回结构体有很好的优化（返回值优化 RVO），通常不会产生额外的拷贝开销。

## 第四步——递归：函数调用自己

### 什么是递归

函数直接或间接地调用自身，就是递归。递归的本质是把问题分解成更小的同类子问题。打个比方：你要数一摞牌有多少张，你可以数最上面一张（1），然后递归地数剩下的（N-1 张），最后结果是 1 + (N-1) = N。

```c
int factorial(int n) {
    if (n <= 1) {
        return 1;  // 基准情况——停止递归的条件
    }
    return n * factorial(n - 1);  // 递归步骤
}
```text

递归调用链：`factorial(5)` → `5 * factorial(4)` → `5 * 4 * factorial(3)` → ... → `5 * 4 * 3 * 2 * 1 = 120`

每个递归调用都会在栈上分配一个新的栈帧（保存局部变量、参数和返回地址），所以递归深度受限于栈大小——这就是为什么递归有可能导致栈溢出。

来验证一下：

```c
#include <stdio.h>

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main(void) {
    for (int i = 0; i <= 10; i++) {
        printf("%d! = %d\n", i, factorial(i));
    }
    return 0;
}
```text

运行结果：

```text
0! = 1
1! = 1
2! = 2
3! = 6
4! = 24
5! = 120
6! = 720
7! = 5040
8! = 40320
9! = 362880
10! = 3628800
```text

> ⚠️ **踩坑预警**
> 递归的最大风险是**栈溢出**。每次递归调用都会占用栈空间，如果递归深度太大（比如 `factorial(100000)`），栈空间耗尽，程序直接崩溃。对于深度递归的场景，手动转为迭代循环更安全。

### 尾递归

如果一个递归函数的递归调用是整个函数的最后一步操作，那它就满足尾递归的形式。理论上，编译器可以把尾递归优化成循环，避免栈帧的累积：

```c
int factorial_tail(int n, int accumulator) {
    if (n <= 1) return accumulator;
    return factorial_tail(n - 1, n * accumulator);
}
// 使用：factorial_tail(5, 1) → 120
```text

但要注意，C 标准并不保证编译器一定做尾递归优化。在深度递归场景下，手动转为迭代更安全。

## 第五步——可变参数函数

有些函数的参数个数是不固定的——最典型的例子就是 `printf`。C 语言通过 `<stdarg.h>` 提供了可变参数函数的机制：

```c
#include <stdarg.h>
#include <stdio.h>

/// @brief 计算任意数量整数的平均值
/// @param count 整数的个数
/// @param ... 可变数量的 int 参数
/// @return 平均值
double average(int count, ...) {
    va_list args;
    va_start(args, count);  // 初始化，count 是最后一个固定参数

    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, int);  // 逐个取出 int 类型的参数
    }

    va_end(args);  // 清理
    return sum / count;
}

int main(void) {
    printf("Avg: %.2f\n", average(3, 10, 20, 30));
    printf("Avg: %.2f\n", average(5, 1, 2, 3, 4, 5));
    return 0;
}
```text

运行结果：

```text
Avg: 20.00
Avg: 3.00
```text

可变参数机制的用法是四步走：`va_list` 声明参数列表 → `va_start` 初始化 → `va_arg` 逐个取参数 → `va_end` 清理。

> ⚠️ **踩坑预警**
> 可变参数没有类型检查——如果你传了 `double` 但用 `va_arg(args, int)` 来取，编译器不会报错，但运行时拿到的值是错的。也没有个数检查——你必须自己通过某种方式告诉函数有多少个参数。这是 C 可变参数最危险的地方。

## C++ 衔接

C++ 在函数方面做了全面增强。最直接的改变是**引用传递**——`void swap(int& a, int& b)` 让参数传递既高效又直观，不需要手动取地址和解引用。

C++ 还支持**函数重载**——同名函数可以有不同参数列表，编译器根据调用时的参数类型自动选择。这解决了 C 中 `print_int`、`print_float`、`print_string` 这类命名膨胀问题。**可变参数模板**（variadic templates）是 C++11 引入的类型安全的可变参数机制，完美替代了 C 的 `va_list`。

`constexpr` 函数让函数可以在编译期执行——如果参数是编译期常量，函数的结果也是编译期常量。这比 C 的宏安全得多。

## 小结

函数是 C 语言模块化的基础。理解值传递的本质——所有参数都是拷贝——是掌握指针参数和多返回值技巧的前提。需要修改调用者的变量就传指针，大型结构体应传 `const` 指针。递归虽然优雅但要警惕栈溢出。可变参数提供了灵活性，但缺乏类型安全。

到这里我们已经掌握了函数的基本用法。接下来问题来了——变量的作用域和生命周期是怎么管理的？`static` 关键字到底有什么用？这些就是我们下一篇要讨论的内容。

## 练习

### 练习 1：可变参数日志函数

实现一个自定义的日志函数，支持日志级别和格式化字符串：

```c
typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } LogLevel;

/// @brief 带级别的日志输出
/// @param level 日志级别
/// @param format 格式化字符串
void log_message(LogLevel level, const char* format, ...);
```text

### 练习 2：递归与迭代——二分查找

分别用递归和迭代实现二分查找，比较两者的性能和可读性：

```c
int binary_search_recursive(const int* arr, size_t len, int target);
int binary_search_iterative(const int* arr, size_t len, int target);
```text

### 练习 3：多返回值实战

实现一个函数，同时计算数组的最大值和最小值：

```c
/// @brief 同时找出数组的最大值和最小值
/// @param data 数组
/// @param len 数组长度
/// @param min_out 最小值输出指针
/// @param max_out 最大值输出指针
void find_min_max(const int* data, size_t len, int* min_out, int* max_out);
```text

## 参考资源

- [cppreference: 函数声明](https://en.cppreference.com/w/c/language/function_declaration)
- [cppreference: stdarg.h](https://en.cppreference.com/w/c/variadic)
