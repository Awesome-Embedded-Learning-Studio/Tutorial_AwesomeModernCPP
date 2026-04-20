---
title: "const 初探"
description: "掌握 const 修饰变量和指针的各种用法，初步了解 constexpr 编译期常量"
chapter: 1
order: 3
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
  - "类型转换"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# const 初探

写代码的时候，有些东西就是不应该被改动的——配置参数一旦设定就不应该被意外覆盖，数组的容量声明之后就不应该再变化，圆周率这种物理常数就更不用说了。如果我们全靠"自觉"来保证这些值不被修改，那跟闭着眼睛走夜路没什么区别，迟早有一天会手滑改掉某个关键值，然后花半天时间去排查一个莫名其妙的 bug。

C++ 给我们提供了一把安全锁：`const`。它的核心思路很简单——如果一个东西不应该变，那就明确告诉编译器，让编译器替我们看着。任何试图修改 `const` 值的代码，都会在编译阶段直接被拦下来。比起跑到线上才发现数据被意外篡改，在编译期就把问题掐死，显然靠谱得多。(所以rust甚至干脆颠倒过来，你不说他是可变的，他就是不变的！所以变量甚至声明就是const的！)

## 给变量上一把锁——const 基础用法

我们从最简单的场景开始。假设我们有一个缓冲区的最大容量，这个值在整个程序运行期间都不应该被改变：

```cpp
const int kMaxBufferSize = 1024;
```

一旦加上了 `const`，这个变量就成了"只读"的——我们必须在声明的时候给它一个初始值，之后任何试图修改它的操作都会被编译器拒绝。来试试看：

```cpp
const int kMaxBufferSize = 1024;
kMaxBufferSize = 2048;  // 编译错误！
```

编译器会给出一个非常明确的报错：

```text
error: assignment of read-only variable 'kMaxBufferSize'
```

这就是 `const` 的核心价值——它把"我不应该改这个值"从一个靠自觉的约定，变成了一个由编译器强制执行的规则。你可能会问，这不就是用编译器当保镖吗？没错，就是这个意思，而且这个保镖从不打瞌睡。

### const 和 #define 到底有什么区别

如果你接触过 C 语言，可能会说"这玩意我用 `#define` 也能做到啊"。确实，`#define MAX_SIZE 1024` 在效果上看起来差不多，但两者之间有几个关键区别。

首先，`const` 变量有明确的类型。`const int kMaxBufferSize = 1024;` 中的 `int` 告诉编译器这是一个整数，如果后续不小心把它赋给一个 `double`，编译器可以进行类型检查甚至发出警告。而 `#define` 只是简单的文本替换，预处理器根本不在乎类型——它只会老老实实地把所有 `MAX_SIZE` 替换成 `1024`，至于 `1024` 是整数还是浮点数，它管不着。

其次，`const` 变量遵循正常的作用域规则。一个在函数内部声明的 `const` 变量只在这个函数里可见，而在全局声明的 `const` 变量默认具有内部链接性（也就是说其他 `.cpp` 文件看不到它）。`#define` 一旦展开，从定义位置到文件末尾全部生效，没有任何作用域限制——这在大型项目里很容易引发名字冲突。

最后，调试的时候 `const` 变量就是一个普通的变量，你在调试器里能看到它的名字和值。而 `#define` 在预处理阶段就被替换掉了，调试器看到的只是一个裸的数字 `1024`，你根本不知道这个 1024 是从哪里来的。

所以我们的结论是：在 C++ 里，优先用 `const` 或者后面会讲到的 `constexpr` 来定义常量，把 `#define` 留给那些真正需要条件编译的场景。

关于命名规范，本教程中的常量统一使用 `kPascalCase` 风格，比如 `kMaxBufferSize`、`kDefaultBaudRate`、`kPi`。这个 `k` 前缀是 C++ 社区里比较常见的常量命名方式，一眼就能看出这是个不该被修改的值。

## const 和指针——最容易搞混的地方

单独用 `const` 修饰一个普通变量很简单，但当 `const` 遇上指针，事情就开始变得有趣了。很多朋友在这一块被搞得晕头转向，包括笔者自己刚开始学的时候也在这里卡了好久。别急，我们一步一步来拆。

核心问题是：`const` 到底修饰的是指针本身，还是指针指向的数据？答案取决于 `const` 出现的位置。C++ 的指针声明有三种 `const` 组合方式，我们逐一来看。

### 指向常量的指针：`const int* p`

```cpp
int value = 42;
const int* p = &value;
```

这里 `const` 修饰的是 `int`，也就是说通过 `p` 去修改它指向的数据是不允许的。但指针 `p` 本身可以改变——它可以指向别的地址。你可以把它理解成"这个指针很守规矩，它承诺不会通过自己去改目标数据"。

```cpp
int x = 10;
int y = 20;
const int* p = &x;

*p = 100;   // 编译错误！不能通过 const int* 修改数据
p = &y;     // 没问题，指针本身可以指向别的地方
```

注意一个细节：虽然通过 `p` 不能修改 `x` 的值，但 `x` 本身并不是 `const` 的。如果直接用 `x = 100;` 修改它是完全合法的——`const int*` 只是说"我不通过这个指针改"，并不代表目标数据真的不可变。

### 常量指针：`int* const p`

```cpp
int value = 42;
int* const p = &value;
```

这回 `const` 修饰的是指针变量 `p` 本身。也就是说指针一旦初始化，就死死地指向那个地址，不能再指向别的地方。但是通过 `p` 去修改目标数据是完全允许的。

```cpp
int x = 10;
int y = 20;
int* const p = &x;

*p = 100;   // 没问题，可以修改数据
p = &y;     // 编译错误！指针本身是 const 的，不能改指向
```

你可以把它理解成一个"死心眼的指针"——它认准了一个地址就不动了，但那个地址里的内容它随便改。

### 两个都 const：`const int* const p`

```cpp
int value = 42;
const int* const p = &value;
```

这种写法把上面的两种约束叠加在一起：指针本身不能改指向，通过指针也不能改数据。这种写法在函数参数里其实挺常见的——当你传递一个指针给函数，既不想让函数内部改变指针的指向，也不想让它修改数据的时候，就会这么写。

### 从右往左读——一个实用的阅读技巧

很多朋友觉得这三种组合很难记住，这里教一个经典的阅读方法：**从右往左读声明**。我们拿 `const int* const p` 举例：

- 从变量名 `p` 开始，往左读
- `const` → p 是一个常量
- `*` → 常量指针
- `int` → 指向 int 类型
- `const` → 这个 int 是常量

连起来就是：`p` 是一个常量指针，指向常量 int。

再看 `const int* p`：`p` 是一个指针（`*`），指向常量 int（`const int`）——数据不可改，指针可改。

`int* const p`：`p` 是一个常量（`const`）指针（`*`），指向 int——指针不可改，数据可改。

多拿几个例子练练，很快就能形成直觉。

> **踩坑预警**：面试和考试特别喜欢考这三种声明之间的区别。如果你一时分不清，千万别靠"猜"——用从右往左读的方法，一步一步拆，比靠记忆可靠得多。另外，`const int* p` 和 `int const* p` 其实是完全等价的两种写法，`const` 放在 `int` 前面还是后面都可以。但 `int* const p` 就不一样了，`const` 跑到了 `*` 的右边，修饰的是指针。这个位置差异是关键。

踩坑不止这一个。很多初学者以为 `const int* p = &x;` 意味着 `x` 本身变成了常量——并不是。`x` 仍然是普通变量，你可以直接修改 `x`。`const int*` 的意思是"我不通过这个指针去修改"，是一种访问约束，不是对目标数据本身的约束。

## const 和引用

指针讲完了，我们来看引用。`const` 和引用搭配比指针简单得多，因为引用本身就不允许重新绑定——它从一出生就死死绑定到某个变量上。所以 `const` 和引用组合只有一种情况：

```cpp
int x = 42;
const int& ref = x;
```

`ref` 是 `x` 的一个别名，但通过 `ref` 不能修改 `x` 的值。和 `const int*` 类似，这只是说"我不通过 `ref` 改"，`x` 本身依然可以自由修改。

这种"常量引用"在实际开发中有一个极其重要的用途——函数参数。想象一下你有一个函数需要接收一个 `std::string` 参数：

```cpp
void print(std::string s)
{
    std::cout << s << std::endl;
}
```

每次调用 `print("hello")` 的时候，都会发生一次字符串的拷贝。如果字符串很长、或者这个函数被频繁调用，这个拷贝开销就不可忽视了。改成 `const` 引用就解决了：

```cpp
void print(const std::string& s)
{
    std::cout << s << std::endl;
}
```

`const std::string& s` 的意思是：接收一个引用（不拷贝），但承诺不修改它。这样既避免了拷贝开销，又向调用者保证了安全性。这个 `const T&` 的参数模式在 C++ 中出现频率极高，我们后面的章节会反复遇到它，这里先有个印象就好。

## constexpr——让编译器帮你算

到目前为止，我们说的 `const` 只是表示"这个值在运行期间不会变"。但有些常量的值在编译阶段就已经确定了——比如 `5 * 5` 肯定等于 `25`，完全不需要等到程序跑起来再算。C++11 引入了 `constexpr`，用来明确告诉编译器："这个值你能在编译的时候就算出来。"

```cpp
constexpr int kSquare = 5 * 5;           // 编译期就算好了，值为 25
constexpr int kBufferSize = 1024 * 64;   // 同样在编译期计算
```

`constexpr` 和 `const` 的关系可以用一句话概括：`constexpr` 一定是 `const` 的（编译期常量当然不能改），但 `const` 不一定是 `constexpr` 的（运行时确定的只读值也算 `const`）。比如：

```cpp
int x = 10;
const int cx = x;          // const 但不是 constexpr，因为 x 的值运行时才知道
constexpr int kVal = 42;   // constexpr，同时也是 const
```

`constexpr` 更强大的地方在于它可以用在函数上。一个 `constexpr` 函数的意思是：如果传入的参数都是编译期能确定的值，那这个函数的返回值也可以在编译期算出来：

```cpp
constexpr int square(int x)
{
    return x * x;
}

constexpr int kResult = square(5);  // 编译期就算好了，kResult = 25, 不相信让AI告诉你如何objdump或者dumpbin看汇编，这里不教了
```

在编译期算好的值有一个很大的好处：它们可以用来做那些必须用常量表达式的地方，比如数组的大小：

```cpp
constexpr int kArraySize = square(3);  // 9
int data[kArraySize];                   // 合法，因为 kArraySize 是编译期常量
```

如果 `kArraySize` 只是普通的 `const`，在某些编译器上这行可能不会通过（取决于 `const` 变量是否被当作常量表达式）。用 `constexpr` 就完全没有歧义。

这里我们只是对 `constexpr` 做一个初步的接触。`constexpr` 是现代 C++ 最重要的特性之一——到了 C++14 它允许函数里写更复杂的逻辑，到了 C++17 又进一步放宽了限制，C++20 更是引入了 `consteval`（必须编译期执行）和 `constinit`。后面我们会有专门的章节深入讲解编译期计算，现在只需要知道：如果你的常量值在编译期就能确定，优先用 `constexpr`。

> **踩坑预警**：`constexpr` 函数不保证一定在编译期执行。编译器只在"需要编译期常量"的场景下（比如数组大小、模板参数）才会强制在编译期计算。其他情况下，编译器可能选择在编译期算，也可能选择在运行时算——这取决于编译器的优化策略和函数的复杂度。如果你需要强制编译期执行，C++20 的 `consteval` 才是正确的选择。

## 综合实战——const_demo.cpp

纸上得来终觉浅。我们现在把上面讲的所有 `const` 用法串在一起，写一个完整的示例程序。这个程序不会有太复杂的逻辑，但会覆盖每一种 `const` 组合，并且验证编译器的行为。

```cpp
// const_demo.cpp —— 演示 const 变量、指针、引用和 constexpr 的各种用法

#include <iostream>

/// @brief constexpr 函数：计算平方
/// @param x 被平方的值
/// @return x 的平方
constexpr int square(int x)
{
    return x * x;
}

int main()
{
    // --- const 变量 ---
    const int kMaxSize = 100;
    // kMaxSize = 200;  // 取消注释会编译错误
    std::cout << "kMaxSize = " << kMaxSize << std::endl;

    // --- constexpr ---
    constexpr int kArraySize = square(5);  // 编译期计算，结果为 25
    std::cout << "kArraySize = " << kArraySize << std::endl;

    // --- 指向常量的指针 ---
    int a = 10;
    int b = 20;
    const int* p_to_const = &a;
    // *p_to_const = 100;  // 取消注释会编译错误
    p_to_const = &b;       // 没问题，指针可以改指向
    std::cout << "*p_to_const = " << *p_to_const << std::endl;

    // --- 常量指针 ---
    int* const const_p = &a;
    *const_p = 100;        // 没问题，可以改数据
    // const_p = &b;       // 取消注释会编译错误
    std::cout << "*const_p = " << *const_p << std::endl;

    // --- 两个都 const ---
    const int* const double_const = &a;
    // *double_const = 1;  // 编译错误
    // double_const = &b;  // 编译错误
    std::cout << "*double_const = " << *double_const << std::endl;

    // --- const 引用 ---
    int x = 42;
    const int& ref = x;
    // ref = 100;           // 编译错误
    x = 100;               // 直接改 x 是可以的
    std::cout << "ref = " << ref << std::endl;  // 输出 100

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o const_demo const_demo.cpp
./const_demo
```

预期输出：

```text
kMaxSize = 100
kArraySize = 25
*p_to_const = 20
*const_p = 100
*double_const = 100
ref = 100
```

你可以把注释掉的那些"编译错误"行逐个取消注释，看看编译器会给出什么样的报错信息。实际动手感受一下编译器是怎么拦截这些操作的，比光看文字印象深刻得多。

## 动手试试

理论看完了，接下来轮到你自己上手了。下面三个练习帮你检验对 `const` 的理解程度，建议每个都完整地写出来、编译运行。

### 练习一：声明 const 指针并预测行为

写出以下声明，然后对每个指针尝试（1）修改指针指向的数据、（2）修改指针本身的指向。在编译之前先预测哪些操作会被编译器拒绝，然后再验证你的预测。

- `const int* p1`
- `int* const p2`
- `const int* const p3`

### 练习二：把 #define 改造成 constexpr

下面是一段使用 `#define` 的 C 风格代码。把所有的宏常量替换成 `constexpr` 变量，并写一个 `constexpr` 函数 `circle_area(double radius)` 来计算圆的面积。

```cpp
#define PI 3.14159265
#define MAX_RADIUS 100.0
#define MIN_RADIUS 0.1
```

### 练习三：写一个使用 const 引用参数的函数

写一个函数 `print_sum`，接收两个 `const int&` 参数，输出它们的和。然后在 `main` 函数里调用它。思考一下：对于 `int` 这种小类型，用 `const int&` 和直接用 `int` 作为参数，性能上有区别吗？什么类型的参数最适合用 `const T&` 传递？

## 小结

这一章我们围绕 `const` 这个关键字，把 C++ 中最常用的几种"只读"机制梳理了一遍。`const` 变量在声明时必须初始化，之后不可修改，比 `#define` 更安全、更有类型信息、更容易调试。`const` 和指针的组合是最容易出错的地方——`const int*` 是"指向常量的指针"（数据不可改，指针可改），`int* const` 是"常量指针"（指针不可改，数据可改），从右往左读是区分它们的有效方法。`const` 引用在函数参数中极为常见，`const T&` 模式既能避免拷贝又能保证安全。`constexpr` 是更严格的常量——它要求值在编译期就能算出来，能让程序运行得更快，也能用在数组大小等需要常量表达式的场景。

下一章我们将进入值类别（value category）的世界——左值和右值到底是什么，为什么移动语义能让程序跑得更快。这些概念听起来有点抽象，但理解了 `const` 之后再去学它们，会发现很多思路是相通的。
