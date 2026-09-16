---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: 掌握 const 修饰变量和指针的各种用法，初步了解 constexpr 编译期常量
difficulty: beginner
order: 3
platform: host
prerequisites:
- 类型转换
reading_time_minutes: 16
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: const 初探
---
# 哟哟哟！这不 const 吗？几天不见这么不可变了

写代码的时候，**有些东西就是不应该被改动的**——配置参数一旦设定就不应该被意外覆盖，数组的容量声明之后就不应该再变化，圆周率这种物理常数就更不用说了。**如果我们全靠"自觉"来保证这些值不被修改，那跟闭着眼睛走夜路没什么区别，迟早有一天会手滑改掉某个关键值，然后花半天时间去排查一个莫名其妙的 bug，换而言之，机制上的保证比你脑子记忆强得多！**。

C++ 给我们提供了一把安全锁：`const`。它的核心思路很简单——如果一个东西不应该变，那就明确告诉编译器，让编译器替我们看着。任何试图修改 `const` 值的代码，**都会在编译阶段直接被拦下来**。比起跑到线上才发现数据被意外篡改，在编译期就把问题掐死，显然靠谱得多。(所以rust甚至干脆颠倒过来，你不说他是可变的，他就是不变的！所以变量甚至声明就是const的！)

## 给变量上一把锁——const 基础用法

假设我们有一个缓冲区（一种简单的理解就是：找个地方放东西，等下用）的最大容量，这个值在整个程序运行期间都不应该被改变：

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

这就是 `const` 的核心价值——它把"我不应该改这个值"从一个靠自觉的约定，变成了一个由编译器强制执行的规则。**你可能会问，这不就是用编译器当保镖吗？没错，就是这个意思，而且这个保镖从不打瞌睡。**

### const 和 #define 到底有什么区别

如果你接触过 C 语言，可能会说"这玩意我用 `#define` 也能做到啊"。确实，`#define MAX_SIZE 1024` 在效果上看起来差不多，但两者之间有几个关键区别。

第一个事情，`const` 变量**有明确的类型（更加干净的语义！）**。`const int kMaxBufferSize = 1024;` 中的 `int` 告诉编译器这是一个整数，如果后续不小心把它赋给一个 `double`，编译器可以进行类型检查甚至发出警告。而 `#define` 只是简单的文本替换，预处理器根本不在乎类型——**它只会老老实实地把所有 `MAX_SIZE` 替换成 `1024`，至于 `1024` 是整数还是浮点数，它管不着。谁管？你！**

其次，`const` 变量遵循正常的作用域规则。一个在函数内部声明的 `const` 变量只在这个函数里可见，而在全局声明的 `const` 变量默认具有内部链接性（也就是说其他 `.cpp` 文件看不到它）。`#define` 一旦展开，从定义位置到文件末尾全部生效，没有任何作用域限制——**这在大型项目里很容易引发名字冲突。**

所以笔者在 C++ 里，优先用 `const` 或者后面会讲到的 `constexpr` 来定义常量，**把 `#define` 留给那些真正需要条件编译的场景。这才是C++甚至是现代C++中define的用武之地！**

> 欸，会有眼尖的朋友说你的const常量很有特色欸，为什么是 k 大头呢？答案是 `kPascalCase` 风格，比如 `kMaxBufferSize`、`kDefaultBaudRate`、`kPi`。这个 `k` 前缀是 C++ 社区里比较常见的常量命名方式，一眼就能看出这是个不该被修改的值。其实是我抄了Google Chrome的常量定义规范。

## const 遇上指针，虽然不是很常用但是值得一说

const，我们说，如果需要，还是加上。

单独用 `const` 修饰一个普通变量很简单，但当 `const` 遇上指针，事情就开始变得有趣了。很多朋友在这一块被搞得晕头转向，包括笔者自己刚开始学的时候也在这里卡了好久。别急，我们一步一步来拆。

核心问题是：`const` 到底修饰的是指针本身，还是指针指向的数据？答案**取决于 `const` 出现的位置**。C++ 的指针声明有三种 `const` 组合方式，我们逐一来看。

### 指向常量的指针：`const int* p`

```cpp
int value = 42;
const int* p = &value;
```

这里 `const` 修饰的是 `int`（我这样打括号：(const int)* p，能不能理解了？）也就是说通过 `p` 去修改它指向的数据是不允许的。但指针 `p` 本身可以改变——它可以指向别的地址。你可以把它理解成"这个指针很守规矩，它承诺不会通过自己去改目标数据"。

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

这里，我这样写：int* (const p)，p 本身是个指针是个const的。**所以往右一看他第一个修饰啥就行。**这回 `const` 修饰的是指针变量 `p` 本身。也就是说指针一旦初始化，就死死地指向那个地址，不能再指向别的地方。但是通过 `p` 去修改目标数据是完全允许的。

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

**这种写法把上面的两种约束叠加在一起：指针本身不能改指向，通过指针也不能改数据。这种写法在函数参数里其实挺常见的——当你传递一个指针给函数，既不想让函数内部改变指针的指向，也不想让它修改数据的时候，就会这么写。**

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

> 我们现在还没有讲现代C++中的移动机制。在C++98年代，我们几乎从来不写值传递的参数，直到C++11, std::move和右值的出现开始，我们终于有更好的语义了。

```cpp
void print(const std::string& s)
{
    std::cout << s << std::endl;
}
```

`const std::string& s` 的意思是：接收一个引用（不拷贝），但承诺不修改它。这样既避免了拷贝开销，又向调用者保证了安全性。这个 `const T&` 的参数模式在 C++ 中出现频率极高，我们后面的章节会反复遇到它，这里先有个印象就好。

## constexpr——让编译器帮你算

到目前为止，我们说的 `const` 只是表示"这个值在运行期间不会变"。但有些常量的值在编译阶段就已经确定了——比如 `5 * 5` 肯定等于 `25`，完全不需要等到程序跑起来再算。C++11 引入了 `constexpr`，用来明确告诉编译器："这个值你能在编译的时候就算出来。"。如果您熟悉汇编，意思很简单了就——给您算成了立即数，不用等运行时在处理。

```cpp
constexpr int kSquare = 5 * 5;           // 编译期就算好了，值为 25
constexpr int kBufferSize = 1024 * 64;   // 同样在编译期计算

// 在一些极低优化下，编译器真的会在运行的时候指挥CPU做两次寄存器加载和做一次寄存器的乘法。远慢于直接将
// 算好的数字塞进寄存器中，换而言之，程序在这个粒度上慢了好几倍甚至是几十倍
const int kSquare = 5 * 5;           // 编译期就算好了，值为 25
const int kBufferSize = 1024 * 64;   // 同样在编译期计算
```

“欸？Charliechen114514，那我问你：const不也是改不了吗？C++为什么要做如此多余的事情？”

并不多余。const 只是提醒编译器这玩意在编译的时候不能改，但是没告诉编译器你完全直接计算出来结果。所以在开低优化的时候，您甚至可以看到CPU居然在傻傻的计算5 x 5是25！而任何人都知道当你编写字面量5 x 5的时候，应该直接写25上去就好了。

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

这里我们只是对 `constexpr` 做一个初步的接触。`constexpr` 是现代 C++ 最重要的特性之一——到了 C++14 它允许函数里写更复杂的逻辑，到了 C++17 又进一步放宽了限制，C++20 更是引入了 `consteval`（必须编译期执行）和 `constinit`。我们将会在嵌入式C++中，反复使用这些极端重要的特性，**他们会在语法层面上辅助我们打下来运行效率和体积开销。**

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

## 在线运行

在线运行 const_demo.cpp，观察各种 const 用法的实际输出：

<OnlineCompilerDemo
  title="const 初探：变量、指针、引用与 constexpr"
  source-path="code/examples/vol1/04_const_demo.cpp"
  description="在线运行并观察 const 指针、const 引用和 constexpr 的实际行为。"
  allow-run
/>

## 动手试试

理论看完了，接下来轮到你自己上手了。下面三个练习帮你检验对 `const` 的理解程度，建议每个都完整地写出来、编译运行。

### 练习一：声明 const 指针并预测行为

写出以下声明，然后对每个指针尝试（1）修改指针指向的数据、（2）修改指针本身的指向。在编译之前先预测哪些操作会被编译器拒绝，然后再验证你的预测。

- `const int* p1`
- `int* const p2`
- `const int* const p3`

::: details 参考答案

**main.cpp**

```cpp
#include <iostream>
int main()
{
    int a1 = 0;
    int a2 = 0;
    int a3 = 0;

    const int* p1 = &a1;
    // *p1 = 5;   // 编译错误！不能通过 const int* 修改数据
    p1 = &a2;     // 没问题，指针本身可以指向别的地方
    std::cout << "*p1 = " << *p1 << std::endl;

    int* const p2 = &a2;
    *p2 = 5;      // 没问题，int* const 指向的数据可以修改
    // p2 = &a3;  // 编译错误！int* const 指针本身不能改变指向
    std::cout << "*p2 = " << *p2 << std::endl;

    p1 = &a3;     // 没问题，const int* 指针本身可以指向别的地方

    const int* const p3 = &a3;
    // *p3 = 5;   // 编译错误！const int* const 既不能修改数据，也不能改变指向
    // p3 = &a1;  // 编译错误！const int* const 指针本身不能改变指向
    std::cout << "*p3 = " << *p3 << std::endl;

    return 0;
}
```

编译运行:

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main && ./main
```

运行结果:

```text
*p1 = 0
*p2 = 5
*p3 = 0
```

:::

### 练习二：把 #define 改造成 constexpr

下面是一段使用 `#define` 的 C 风格代码。把所有的宏常量替换成 `constexpr` 变量，并写一个 `constexpr` 函数 `circle_area(double radius)` 来计算圆的面积。

```cpp
#define PI 3.14159265
#define MAX_RADIUS 100.0
#define MIN_RADIUS 0.1
```

::: details 参考答案

**main.cpp**

```cpp
#include <iostream>

constexpr double PI = 3.14159265;
constexpr double MAX_RADIUS = 100.0;
constexpr double MIN_RADIUS = 0.1;

constexpr double clamp_radius(double radius)
{
    return radius < MIN_RADIUS
        ? MIN_RADIUS
        : (radius > MAX_RADIUS ? MAX_RADIUS : radius);
}

constexpr double circle_area(double radius)
{
    const double r = clamp_radius(radius);
    return PI * r * r;
}

int main()
{
    double r = 0;
    std::cout << "请你输入所求圆的半径 : ";
    std::cin >> r;
    std::cout << "半径为" << r << "的面积是: " << circle_area(r) << std::endl;
    return 0;
}
```

题目只给了三个宏，没规定 `MAX_RADIUS` / `MIN_RADIUS` 怎么用，原样换成 `constexpr` 后它们会闲置。这里加了一个同为 `constexpr` 的 `clamp_radius`，把输入半径夹回 `[0.1, 100]` 区间，让两个常量真正参与计算——`constexpr` 函数也可以调用另一个 `constexpr` 函数，像 `constexpr double area = circle_area(2.0);` 这样的常量表达式初始化，整条调用链都会在编译期算完。

编译运行:

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main && ./main
```

运行结果:

```text
请你输入所求圆的半径 : 2
半径为2的面积是: 12.5664
```

:::

### 练习三：写一个使用 const 引用参数的函数

写一个函数 `print_sum`，接收两个 `const int&` 参数，输出它们的和。然后在 `main` 函数里调用它。思考一下：对于 `int` 这种小类型，用 `const int&` 和直接用 `int` 作为参数，性能上有区别吗？什么类型的参数最适合用 `const T&` 传递？

::: details 参考答案

**main.cpp**

```cpp
#include <iostream>

void print_sum(const int& a, const int& b)
{
    std::cout << a << " + " << b << " 的值是: " << a + b << std::endl;
}

int main()
{
    int a = 0;
    int b = 0;
    std::cout << "请输入a的值是 :";
    std::cin >> a;
    std::cout << "请输入b的值是 :";
    std::cin >> b;
    print_sum(a, b);
    return 0;
}
```

编译运行:

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main && ./main
```

运行结果:

```text
请输入a的值是 :1
请输入b的值是 :3
1 + 3 的值是: 4
```

对于 `int` 这种小类型，按值传递通常更合适：复制一个机器字大小的值成本很低，编译器也常能直接通过寄存器传递；使用 `const int&` 不一定更快，实际差异应以测量为准。`const T&` 更适合较大的、只读且不需要复制的对象，例如 `std::string` 或容器；小型标量类型一般直接按值传递即可。

:::
