---
title: "函数基础"
description: "掌握 C++ 函数的定义、声明、参数传递和返回值，理解作用域与生命周期"
chapter: 3
order: 1
difficulty: beginner
reading_time_minutes: 15
platform: host
prerequisites:
  - "range-for 循环"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# 函数基础

孩子们，我见过的，有人写代码真的会写一个一万行的程序从头到尾只有 `main()` 一个函数，所有代码像面条一样堆在一起。显然，这个人不太懂函数（小白除外）

这读起来是什么体验？变量到处都是，逻辑纠缠不清，想改一个功能得通读全文生怕牵一发动全身。说实话，这种代码别说给别人看了，过一个星期连自己都看不懂，调侃只有上帝看不懂了。（当然也许再过一个星期上帝都看不懂）

函数就是解决这个问题的核心工具。它让我们把一段完成特定任务的代码封装成一个有名字的单元，需要的时候直接调用名字就行，不用关心内部的实现细节。这一章我们从最基本的概念出发，把函数的定义方式、参数传递、返回值、作用域这些基础知识彻底搞清楚。

## 第一步——函数的声明与定义

写一个函数之前，我们需要搞清楚两个概念：**声明**（declaration）和**定义**（definition）。声明是告诉编译器"有这样一个函数存在"，只给出函数名、返回类型和参数列表，不包含函数体；定义则是给出完整的实现。

```cpp
// 声明（也叫函数原型/prototype）
int add(int a, int b);

// 定义（包含函数体）
int add(int a, int b)
{
    return a + b;
}
```

声明末尾的分号代替了函数体，编译器看到声明就知道 `add` 是一个接收两个 `int` 参数、返回 `int` 的函数。至于它内部怎么实现的，编译器暂时不关心——只要在链接的时候能找到真正的定义就行。

那为什么要区分这两者？因为 C++ 编译器是"从上往下"逐行处理的。如果 `main()` 里调用了 `add()`，但 `add` 的定义写在 `main` 的后面，编译器在处理 `main` 的时候还不知道 `add` 是什么，就会直接报错。解决办法就是在文件开头放一个声明，让编译器提前知道这个函数的存在：

```cpp
#include <iostream>

// 先声明，告诉编译器这些函数存在
int add(int a, int b);
int multiply(int a, int b);

int main()
{
    std::cout << add(3, 4) << std::endl;       // 编译器知道 add 的签名
    std::cout << multiply(3, 4) << std::endl;  // 编译器知道 multiply 的签名
    return 0;
}

// 定义放在后面，完全没问题
int add(int a, int b)
{
    return a + b;
}

int multiply(int a, int b)
{
    return a * b;
}
```

那一大堆这些声明聚合在一起呢？这不就是头文件嘛！这种"先声明后定义"的模式在实际项目中非常重要——我们后续学到头文件的时候就会看到，声明通常放在 `.h` 文件里供多个源文件共享，而定义放在 `.cpp` 文件中。现在只要记住一个原则就行：**编译器在使用一个函数之前，必须看到它的声明（或者定义）**。

> ⚠️ **踩坑预警**
> 忘记写声明、函数定义又放在调用点之后，是新手最常遇到的编译错误之一。错误信息一般是 `error: use of undeclared identifier 'xxx'`。看到这个提示，第一反应应该是检查函数定义的位置——要么把定义挪到调用点前面，要么在文件开头补上声明。

## 第二步——返回类型与 return 语句

每个 C++ 函数都有一个返回类型，它写在函数名前面，告诉编译器这个函数执行完毕后会产出什么类型的值。`return` 语句用来把值送回调用者，同时结束函数的执行。

```cpp
int max(int a, int b)
{
    if (a > b) {
        return a;   // 返回 a，函数立即结束
    }
    return b;       // 返回 b
}
```

函数可以有多个 `return` 语句，但每次调用只会执行其中一个——`return` 一旦执行，后面的代码全部跳过。对于上面这个 `max` 函数，两条路径都保证了一定会 `return`，所以没有问题。

如果函数不需要返回任何值，就把返回类型写成 `void`。`void` 函数可以不写 `return`，函数体执行完自动返回；也可以写一个裸的 `return;` 来提前退出：

```cpp
void print_greeting(const std::string& name)
{
    if (name.empty()) {
        return;  // 提前退出，不打印任何内容
    }
    std::cout << "Hello, " << name << "!" << std::endl;
}
```

C++14 引入了一个很实用的特性：**返回类型推导**。在函数返回类型的位置写 `auto`，编译器会根据 `return` 语句自动推导返回类型：

```cpp
auto add(int a, int b)
{
    return a + b;  // 编译器推导出返回类型为 int
}
```

这对那些返回类型写起来很长或者模板代码中的函数特别方便。但有一个限制：所有 `return` 语句必须返回相同的类型。如果一条路径返回 `int`，另一条返回 `double`，编译器会报错。

> ⚠️ **踩坑预警**
> 非 `void` 函数里忘记写 `return` 是一个经典 bug。编译器可能会给出警告，但不一定报错——如果控制流走到了函数末尾却没有遇到 `return`，行为是**未定义的**（undefined behavior）。函数可能返回一个垃圾值，也可能程序直接崩溃，全凭运气。所以，一定要养成习惯：每个非 `void` 函数的所有执行路径都必须有 `return`。

## 第三步——参数与实参

函数通过**参数**（parameter）接收外部传入的数据。在函数签名里声明的变量叫形式参数（形参），调用时传入的具体值叫实际参数（实参）：

```cpp
//          形参
//            ↓    ↓
int add(int a, int b)
{
    return a + b;
}

int main()
{
    //       实参
    //        ↓    ↓
    int result = add(3, 4);  // a 接收 3, b 接收 4
    return 0;
}
```

函数可以有任意数量的参数，也可以没有参数。没有参数时括号里留空就行（C++ 中空括号和 `void` 等价：`int foo()` 与 `int foo(void)` 含义相同）。

多参数函数中，实参和形参是**按位置**一一对应的——第一个实参传给第一个形参，第二个传给第二个，以此类推。C++ 不支持像 Python 那样的命名参数调用，所以参数的顺序一定要对齐：

```cpp
void print_info(const std::string& name, int age, double height)
{
    std::cout << name << ", " << age << " 岁, "
              << height << " cm" << std::endl;
}

int main()
{
    // 按位置传递，顺序不能搞错
    print_info("Alice", 20, 165.5);
    return 0;
}
```

实参的类型需要和形参匹配，或者能隐式转换。比如形参是 `double`，传入 `int` 是合法的（会发生隐式转换），但反过来可能会丢失精度。默认情况下参数是**按值传递**的——函数内部拿到的是实参的副本，修改副本不影响原始数据。关于引用传递和指针传递，我们会在下一章详细讨论。

## 第四步——局部作用域与生命周期

在函数体内声明的变量叫做**局部变量**，它们的作用域（scope）仅限于该函数内部。换句话说，从 `{` 开始到 `}` 结束的这个范围内，变量是可见的；出了这个范围，变量就不存在了：

```cpp
int compute(int x)
{
    int result = x * 2;  // result 是局部变量
    return result;
}   // result 在这里被销毁

int main()
{
    int r = compute(5);
    // std::cout << result;  // 编译错误！result 不在作用域内
    return 0;
}
```

局部变量存储在**栈**（stack）上。函数被调用时，系统在栈上为它的局部变量分配空间；函数返回时，这些空间被回收，变量随即被销毁。这个过程是自动的，我们不需要手动管理。

不同函数可以使用同名变量，它们互不干扰，因为各自有独立的作用域：

```cpp
void func_a()
{
    int value = 10;  // func_a 的 value
    std::cout << "func_a: " << value << std::endl;
}

void func_b()
{
    int value = 20;  // func_b 的 value，跟 func_a 的毫无关系
    std::cout << "func_b: " << value << std::endl;
}
```

甚至同一函数内的不同代码块也可以有同名变量，内层块会**遮蔽**（shadow）外层块的变量——不过在实际开发中，我们不建议这样做，可读性太差。

> ⚠️ **踩坑预警**
> 返回局部变量的**引用**或**指针**是一个严重的错误，而且编译器不一定能帮你拦住。局部变量在函数返回后就被销毁了，引用或指针指向的内存已经无效——这就是经典的"悬空引用"（dangling reference）问题：
>
> ```cpp
> int& dangerous()
> {
>     int local = 42;
>     return local;  // 严重错误：返回局部变量的引用
> }   // local 在这里被销毁，引用指向的内存已无效
> ```
>
> 程序可能在你调试的时候跑得好好的，换成 Release 编译或者数据量变大就突然崩了。这种"时好时坏"的 bug 比稳定崩溃的更难排查。规则很简单：**永远不要返回局部变量的引用或指针**。按值返回是安全的——它会拷贝一份给调用者。

## 第五步——函数重载初窥

C++ 允许我们定义多个同名函数，只要它们的参数列表不同（参数个数不同，或者参数类型不同）。这叫做**函数重载**（function overloading）：

```cpp
int add(int a, int b)
{
    return a + b;
}

double add(double a, double b)
{
    return a + b;
}
```

编译器会根据调用时传入的实参类型，自动选择最匹配的版本——`add(3, 4)` 调用 `int` 版本，`add(3.5, 2.1)` 调用 `double` 版本。这对代码的可读性和一致性很有帮助，调用者不需要记住 `add_int`、`add_double` 这样一堆不同的名字。

函数重载的完整规则有不少细节，比如重载解析的优先级、歧义处理等，我们会在后面的章节深入讨论。现在只要知道有这么回事就好。

## 实战演练——functions.cpp

我们把前面学到的知识点整合到一个完整程序里，包含函数声明、定义、返回值处理、局部作用域等概念的演示：

```cpp
// functions.cpp
// Platform: host
// Standard: C++17

#include <iostream>
#include <string>

// 函数声明（原型）
int add(int a, int b);
int max_of(int a, int b);
int factorial(int n);
bool is_even(int n);
void print_result(const std::string& label, int value);

// main 函数——程序入口
int main()
{
    // 加法
    int sum = add(15, 27);
    print_result("15 + 27", sum);

    // 取较大值
    int bigger = max_of(42, 17);
    print_result("max(42, 17)", bigger);

    // 阶乘
    int fact = factorial(6);
    print_result("6!", fact);

    // 判断奇偶
    int test_values[] = {0, 1, 2, 7, 10};
    for (int val : test_values) {
        std::cout << val << " 是"
                  << (is_even(val) ? "偶数" : "奇数")
                  << std::endl;
    }

    return 0;
}

// ---- 函数定义 ----

int add(int a, int b)
{
    return a + b;
}

int max_of(int a, int b)
{
    if (a > b) {
        return a;
    }
    return b;
}

/// @brief 计算 n 的阶乘（n!）
/// @param n 非负整数
/// @return n 的阶乘
int factorial(int n)
{
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

bool is_even(int n)
{
    return n % 2 == 0;
}

void print_result(const std::string& label, int value)
{
    std::cout << label << " = " << value << std::endl;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o functions functions.cpp
./functions
```

运行结果：

```text
15 + 27 = 42
max(42, 17) = 42
6! = 720
0 是偶数
1 是奇数
2 是偶数
7 是奇数
10 是偶数
```

这个程序里，`factorial` 是一个**递归函数**——它在自己的函数体内调用了自己。递归的思路是把 `n!` 拆成 `n * (n-1)!`，直到 `n <= 1` 时直接返回 1 作为终止条件。递归是一种强大的编程技巧，但也有它的代价——每次递归调用都会在栈上分配新的局部变量空间，你想想，如果我们疯狂自己调用自己，也就是“递归太深”的话，咱们就会导致栈溢出！所以在实际工程上，除非是循环真的很不好写而且可以很笃定绝对不会嵌套太多层，我们可能才会考虑递归。否则是一律禁用。至少笔者初期工作的时候敢这样搞那肯定是要被骂的。在后续章节中我们还会更深入地讨论递归和迭代的选择。

值得注意的一点是 `print_result` 函数的参数类型是 `const std::string&` 而不是 `std::string`。这里的 `&` 表示引用传递，避免了字符串的拷贝开销；`const` 表示函数内部不会修改这个字符串。虽然引用传递的细节要到下一章才正式讲解，但这个写法在实际代码中极为常见，先混个眼熟就好。

## 动手试试

### 练习一：最大公约数

写一个函数 `int gcd(int a, int b)`，使用辗转相除法（欧几里得算法）计算两个正整数的最大公约数。算法很简单：如果 `b` 为 0 就返回 `a`，否则递归调用 `gcd(b, a % b)`。

```text
gcd(48, 18)  → 6
gcd(100, 75) → 25
gcd(7, 3)    → 1
```

### 练习二：素数判断

写一个函数 `bool is_prime(int n)`，判断一个正整数 `n` 是否为素数。注意处理边界情况：小于 2 的数不是素数，2 是素数。提示：只需要检查 2 到 `sqrt(n)` 范围内有没有能整除 `n` 的数。

```text
is_prime(2)  → true
is_prime(17) → true
is_prime(18) → false
is_prime(1)  → false
```

### 练习三：用 struct 返回多个值

C++ 函数只能返回一个值，但我们可以把多个值打包到 `struct` 里再返回。定义一个 `struct DivResult` 包含商和余数，然后写一个函数 `divmod` 同时返回两者：

```cpp
struct DivResult {
    int quotient;
    int remainder;
};

DivResult divmod(int dividend, int divisor);
```

```text
divmod(17, 5) → 商: 3, 余: 2
divmod(100, 7) → 商: 14, 余: 2
```

## 小结

这一章我们从零开始认识了 C++ 函数的基础机制。函数的声明告诉编译器"有这个函数"，定义给出具体实现——编译器在使用函数之前必须看到声明或定义之一。返回类型决定了函数产出的值类型，非 `void` 函数的每条执行路径都必须有 `return` 语句。参数按位置一一对应传递，默认是值拷贝。局部变量的作用域限定在函数体内，函数返回时它们被自动销毁——这也是为什么绝对不能返回局部变量的引用或指针。

函数重载让我们用同一个名字处理不同类型的参数，编译器会自动选择最合适的版本。此外，我们第一次接触了递归——函数调用自身的编程技巧，在阶乘计算中展示了它的基本用法。

这些是函数的骨架。接下来我们要深入参数传递的细节——值传递、引用传递、指针传递各自的工作机制和适用场景，那才是真正决定程序性能和正确性的关键。
