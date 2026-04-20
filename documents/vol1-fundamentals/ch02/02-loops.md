---
title: "循环语句"
description: "掌握 for、while、do-while 循环和 break/continue 控制，学会让程序重复执行任务"
chapter: 2
order: 2
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
  - "条件语句"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# 循环语句

计算机最擅长的事情就是不知疲倦地重复做同一件事。让我们手动打印 100 行 "Hello" 肯定要疯掉，但计算机只需要一条循环指令就能搞定。循环语句让我们告诉程序"把这个动作重复 N 遍"或者"一直做，直到某个条件满足为止"——这是几乎所有有意义程序的核心结构。

这一章我们把 C++ 的三种循环结构从里到外拆一遍，重点搞清楚每种循环适合什么场景、什么时候该用 break 和 continue、以及嵌套循环里那些容易踩的坑。

> **学习目标**
> 完成本章后，你将能够：
>
> - [ ] 掌握 while、do-while、for 三种循环的语法和适用场景
> - [ ] 正确使用 break 和 continue 控制循环流程
> - [ ] 理解嵌套循环的执行过程和时间复杂度
> - [ ] 独立编写图案打印和简单数值计算的程序

## 第一步——while 循环：不知道次数就一直做

`while` 循环是最直白的循环结构：先检查条件，条件为真就执行循环体，执行完再回来检查，直到条件为假才停下来。

```cpp
while (condition) {
    // 循环体
}
```

每次进入循环体之前先算一次 `condition`，结果是 `true` 就执行大括号里的代码，执行完毕后回到条件再判断。如果一开始条件就是 `false`，循环体一次都不会执行。

什么时候用 `while`？最典型的场景就是"我们事先不知道要循环多少次"。比如让用户不断输入数字累加，直到输入 0 为止：

```cpp
#include <iostream>

int main()
{
    int sum = 0;
    int value = 0;

    std::cout << "请输入数字（输入 0 结束）: ";
    std::cin >> value;

    while (value != 0) {
        sum += value;
        std::cout << "当前累加和: " << sum << std::endl;
        std::cout << "请继续输入（0 结束）: ";
        std::cin >> value;
    }

    std::cout << "最终结果: " << sum << std::endl;
    return 0;
}
```

运行效果：

```text
请输入数字（输入 0 结束）: 10
当前累加和: 10
请继续输入（0 结束）: 25
当前累加和: 35
请继续输入（0 结束）: 0
最终结果: 35
```

循环体里面必须有能改变条件的操作（这里是我们每次重新读入 `value`），否则就会变成死循环。

> ⚠️ **踩坑预警**：死循环是 `while` 最常见的坑。如果循环体里没有任何操作能让条件变成 `false`，程序就会一直跑下去永远不退出。比如忘了写 `std::cin >> value;` 这一行，`value` 永远不变，条件永远为真。写 `while` 循环的时候，养成习惯检查"循环体里有没有改变条件的代码"。

## 第二步——do-while 循环：先做一次再说

`do-while` 和 `while` 很像，只有一个关键区别：循环体至少执行一次。条件检查放在循环体之后：

```cpp
do {
    // 循环体
} while (condition);  // 注意这里有个分号！
```

因为"先做后判"的特性，`do-while` 特别适合菜单系统这种场景——菜单至少得显示一次，然后根据用户选择决定是否继续：

```cpp
int choice = 0;
do {
    std::cout << "\n=== 菜单 ===" << std::endl;
    std::cout << "1. 打印问候  0. 退出" << std::endl;
    std::cout << "请选择: ";
    std::cin >> choice;
    if (choice == 1) {
        std::cout << "你好！欢迎学习 C++！" << std::endl;
    }
} while (choice != 0);
```

> ⚠️ **踩坑预警**：`do-while` 末尾的分号千万别忘。漏掉的话，编译器会把下一行代码当成 while 的循环体来解析，报错信息可能非常诡异。这是 C++ 里少数几个必须在 `}` 后面加分号的地方之一，和 `if`、`while`、`for` 都不一样，很容易搞混。

## 第三步——for 循环：知道次数的首选

当循环次数已知的时候，`for` 循环是最清晰的选择。它把初始化、条件判断和递增操作都集中在一行里，一眼就能看出循环的范围：

```cpp
for (init; condition; increment) {
    // 循环体
}
```

执行顺序是：先执行一次 `init`，然后检查 `condition`，为真就执行循环体，执行完做 `increment`，再回去检查 `condition`，如此往复。

```cpp
for (int i = 1; i <= 10; ++i) {
    std::cout << i << " ";
}
// 输出: 1 2 3 4 5 6 7 8 9 10
```

这里 `i` 的作用域仅限于 `for` 循环内部——出了循环体就访问不到了，这是 C++11 开始支持的特性。

`for` 还支持同时操作多个变量，用一个经典的双指针翻转来演示：

```cpp
int data[] = {1, 2, 3, 4, 5};
int n = 5;

// 双指针从两端向中间走，交换元素
for (int i = 0, j = n - 1; i < j; ++i, --j) {
    int temp = data[i];
    data[i] = data[j];
    data[j] = temp;
}
// data 现在是 {5, 4, 3, 2, 1}
```

初始化部分声明了 `i` 和 `j` 两个变量，递增部分同时做 `++i` 和 `--j`，从两端向中间逼近，相遇时停止。

> ⚠️ **踩坑预警**：差一错误（off-by-one error）是 `for` 循环最经典的坑。本意是循环 10 次，写成 `for (int i = 1; i < 10; ++i)` 就只跑了 9 次。一个实用建议：养成固定习惯——要么总是从 0 开始用 `<`（`for (int i = 0; i < n; ++i)`），要么从 1 开始用 `<=`（`for (int i = 1; i <= n; ++i)`），不要混用，混用就是差一错误的温床。

## 第四步——break 和 continue：循环里的"紧急出口"

`break` 立即跳出当前循环，不再判断条件；`continue` 跳过当前轮次剩余代码，直接进入下一轮迭代。

```cpp
int data[] = {4, 7, 2, 9, 5, 1};
int target = 9;

for (int i = 0; i < 6; ++i) {
    if (data[i] == target) {
        std::cout << "找到 " << target << "，下标为 " << i << std::endl;
        break;  // 找到了，不用继续搜了
    }
}
// 输出: 找到 9，下标为 3
```

`continue` 的例子——打印 1 到 20 之间的奇数：

```cpp
for (int i = 1; i <= 20; ++i) {
    if (i % 2 == 0) {
        continue;  // 偶数跳过
    }
    std::cout << i << " ";
}
// 输出: 1 3 5 7 9 11 13 15 17 19
```

注意 `break` 只能跳出最内层的循环。嵌套两层时，内层的 `break` 只会跳出内层，外层该怎么转还怎么转。想一次性跳出多层循环，通常用一个标志变量配合外层条件判断，或者把逻辑封装成函数用 `return` 退出。

> ⚠️ **踩坑预警**：`break` 和 `continue` 用多了会让代码逻辑变得支离破碎，读代码的人得在脑子里跳来跳去地追踪执行流程。如果一个循环体里有超过两三个 `break` 或 `continue`，就该考虑是不是该把循环条件写得更清晰，或者把部分逻辑抽成单独的函数。简单直接的循环条件永远比到处 `break` 更容易维护。

## 第五步——嵌套循环：循环里的循环

循环体里面可以再放一个循环，它解决的是"对每一行做 X，每一行里的每一列做 Y"这类二维问题。来看经典的九九乘法表：

```cpp
#include <iostream>
#include <iomanip>  // std::setw

int main()
{
    for (int i = 1; i <= 9; ++i) {
        for (int j = 1; j <= i; ++j) {
            std::cout << j << "x" << i << "=" << std::setw(2) << i * j << " ";
        }
        std::cout << std::endl;
    }
    return 0;
}
```

运行结果：

```text
1x1= 1
1x2= 2 2x2= 4
1x3= 3 2x3= 6 3x3= 9
1x4= 4 2x4= 8 3x4=12 4x4=16
...
1x9= 9 2x9=18 3x9=27 4x9=36 5x9=45 6x9=54 7x9=63 8x9=72 9x9=81
```

外层循环控制行号 `i`，内层循环控制列号 `j`，`j` 从 1 遍历到 `i`，打印出来是三角形。`std::setw(2)` 让输出项占 2 个字符宽度，一位数和两位数能对齐。

嵌套循环的执行次数是各层循环次数的乘积。外层 N 次、内层 M 次，总共 N * M 次内层循环体。对于 N=1000 的双层嵌套，内层要执行一百万次——所以心里要有这个概念：数据量大的时候，嵌套层数越少越好。

## 完整实战——loops.cpp

我们把前面学的几种循环综合到一个程序里：九九乘法表、猜数字小游戏（while + break）、金字塔图案打印（嵌套 for）。

```cpp
// loops.cpp -- 综合循环练习
// 编译: g++ -Wall -Wextra -o loops loops.cpp

#include <iostream>
#include <iomanip>

/// @brief 打印九九乘法表
void print_multiplication_table()
{
    std::cout << "=== 九九乘法表 ===" << std::endl;
    for (int i = 1; i <= 9; ++i) {
        for (int j = 1; j <= i; ++j) {
            std::cout << j << "x" << i << "=" << std::setw(2) << i * j << " ";
        }
        std::cout << std::endl;
    }
}

/// @brief 猜数字游戏，演示 while + break 的配合
void guess_number_game()
{
    const int kSecret = 42;
    int guess = 0;
    int attempts = 0;

    std::cout << "\n=== 猜数字游戏 ===" << std::endl;
    std::cout << "我想了一个 1-100 之间的数字，你来猜！" << std::endl;

    while (true) {
        std::cout << "你的猜测: ";
        std::cin >> guess;
        ++attempts;

        if (guess == kSecret) {
            std::cout << "恭喜！你用了 " << attempts << " 次猜中了！" << std::endl;
            break;
        } else if (guess < kSecret) {
            std::cout << "太小了，再试试。" << std::endl;
        } else {
            std::cout << "太大了，再试试。" << std::endl;
        }
    }
}

/// @brief 打印由星号组成的金字塔
void print_pyramid()
{
    const int kHeight = 5;

    std::cout << "\n=== 金字塔图案 ===" << std::endl;
    for (int row = 1; row <= kHeight; ++row) {
        // 打印前导空格
        for (int space = 0; space < kHeight - row; ++space) {
            std::cout << " ";
        }
        // 打印星号（第 row 行有 2*row - 1 个星号）
        for (int star = 0; star < 2 * row - 1; ++star) {
            std::cout << "*";
        }
        std::cout << std::endl;
    }
}

int main()
{
    print_multiplication_table();
    guess_number_game();
    print_pyramid();

    return 0;
}
```

编译运行：

```bash
g++ -Wall -Wextra -o loops loops.cpp
./loops
```

```text
=== 九九乘法表 ===
1x1= 1
1x2= 2 2x2= 4
...（中间省略）
1x9= 9 2x9=18 ... 9x9=81

=== 猜数字游戏 ===
你的猜测: 50
太大了，再试试。
你的猜测: 25
太小了，再试试。
你的猜测: 42
恭喜！你用了 3 次猜中了！

=== 金字塔图案 ===
    *
   ***
  *****
 *******
*********
```

来拆一下金字塔的逻辑。第 `row` 行需要 `kHeight - row` 个前导空格让星号居中，然后打印 `2 * row - 1` 个星号。这个 `2n-1` 的规律在图案打印中非常常见。猜数字游戏里的 `while (true)` + `break` 也是一种经典模式——当退出条件不容易浓缩成一个布尔表达式时，在循环体内部判断然后 break 是一种清晰的做法。

## 动手试试

光看懂不够，得自己写一遍才算真的会。以下是四个练习，建议每个都动手完成。

### 练习一：打印空心正方形

输入一个正整数 N，打印一个 N x N 的空心正方形。比如 N=5 时：

```text
* * * * *
*       *
*       *
*       *
* * * * *
```

只有第一行、最后一行、第一列和最后一列打印星号，中间全是空格。提示：用嵌套 for 循环，内层判断当前是不是边界位置。

### 练习二：计算阶乘

用 `for` 循环计算 N 的阶乘（N!）。比如 5! = 120。注意阶乘增长极快，用 `int` 的话 13! 就会溢出，可以试试 `long long` 能撑到多大。

### 练习三：找素数

输入一个正整数 N，打印从 2 到 N 之间所有的素数。判断素数的方法：对于数 m，检查 2 到 m-1 之间有没有能整除 m 的数，如果没有就是素数。提示：外层循环遍历候选数，内层循环做整除检查，找到因子后用 `break` 提前退出内层。

### 练习四：打印菱形

输入一个奇数 N，打印一个 N 行的菱形图案。比如 N=5 时：

```text
  *
 ***
*****
 ***
  *
```

提示：上半部分和金字塔一样，下半部分是金字塔的镜像——行号从大到小。

## 小结

这一章我们把 C++ 的三种循环结构完整地过了一遍。`while` 适合"不知道次数，满足条件就继续"的场景，`do-while` 保证循环体至少执行一次（菜单系统最常用），`for` 在循环次数已知时最清晰因为它把初始化、条件和递增都集中在了一起。`break` 用于紧急跳出循环，`continue` 用于跳过当前轮次，但不要滥用——清晰的循环条件永远优于到处跳转的控制流。嵌套循环能解决二维问题，但要注意 O(N^2) 的执行次数增长。

下一章我们会遇到 C++11 引入的 range-for 循环——一种遍历容器和数组更现代、更安全的方式。有了这一章的基础，到时候你会发现 range-for 简直是清风拂面。

---

> **难度自评**：如果你对嵌套循环的执行顺序感到困惑，建议拿一支笔，在纸上手动模拟九九乘法表的执行过程——追踪外层变量 `i` 和内层变量 `j` 每一步的值，这样能建立非常直观的理解。
