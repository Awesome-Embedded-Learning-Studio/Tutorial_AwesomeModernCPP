---
title: "参数传递方式"
description: "理解值传递、引用传递和 const 引用传递的区别，学会为不同场景选择正确的传参方式"
chapter: 3
order: 2
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
  - "函数基础"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# 参数传递方式

数据怎么"走进"函数、处理完的结果怎么"走出来"，直接决定了程序的正确性和性能。你可能觉得"传个参数而已，有什么好讲的"，但恰恰是这些看似不起眼的细节，在实际项目里制造了大量的 bug 和性能问题——拷贝了一个不该拷贝的大对象导致性能暴跌，或者不小心通过引用改掉了调用者的数据导致难以追踪的逻辑错误。

这一章我们把 C++ 的三种核心参数传递方式彻底搞清楚：值传递、引用传递、const 引用传递。不复杂，但必须踩实。

## 值传递——函数拿到的是副本

值传递是最直观的传参方式：调用函数时，实参被复制一份，函数体操作的是这个副本，原变量完全不受影响。

```cpp
#include <iostream>

void add_ten(int x)
{
    x += 10;
    std::cout << "函数内 x = " << x << std::endl;
}

int main()
{
    int value = 5;
    add_ten(value);
    std::cout << "函数外 value = " << value << std::endl;
    return 0;
}
```

运行结果：

```text
函数内 x = 15
函数外 value = 5
```

`value` 还是 5，一点没变——`add_ten` 的参数 `x` 是 `value` 的一份拷贝，改的是副本，原始变量毫发无损。这种隔离性在很多时候是我们想要的：函数内部的修改不会泄漏到外部。

但值传递的代价也很明显——每次调用都要拷贝。对于 `int`、`double` 这种只有几个字节的基本类型，拷贝开销可以忽略不计。可如果参数是一个包含数万个元素的结构体呢？

```cpp
struct SensorData {
    int readings[10000];
    double timestamps[10000];
    char description[256];
};

void process(SensorData data)  // 整个结构体被拷贝一份
{
    // 处理数据...
}
```

每次调用 `process`，编译器都要把 `SensorData` 的大约 80 KB 数据完整复制一遍。在循环里频繁调用？那就是灾难级的无意义拷贝。

> **踩坑预警**：值传递不会引发逻辑错误，但会引发性能灾难。当函数接收大对象作为值参数并在热点循环中频繁调用时，几乎可以确定存在性能问题。

## 引用传递——直接操作原始数据

引用传递的核心思路是：不拷贝，直接让函数访问调用者的原始变量。参数类型后加 `&` 即声明引用参数。

```cpp
void add_ten(int& x)
{
    x += 10;
}

int main()
{
    int value = 5;
    add_ten(value);
    // value 现在是 15
    return 0;
}
```

这回 `value` 变成了 15。`x` 是 `value` 的引用——不是副本，而是 `value` 本身的另一个名字。

引用传递最经典的应用场景是 `swap` 函数。在 C 语言里必须传指针，C++ 有了引用，写起来干净多了：

```cpp
/// @brief 交换两个整数的值
/// @param a 第一个整数
/// @param b 第二个整数
void swap_values(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

int main()
{
    int x = 3;
    int y = 7;
    swap_values(x, y);
    std::cout << "x = " << x << ", y = " << y << std::endl;
    return 0;
}
```

运行结果：

```text
x = 7, y = 3
```

交换成功。注意调用语法非常自然——不需要像 C 那样取地址传指针。

但引用传递也带来了新的约束和陷阱。

> **踩坑预警**：非 const 引用参数只能绑定到左值——也就是有名字、有地址的变量。字面量、临时值（右值）都不能传给非 const 引用。比如 `add_ten(5)` 会编译报错，因为 `5` 是个字面量，没有内存地址可供引用绑定。同理，`swap_values(x, 3)` 也编译不过——你不能把一个数字字面量交换到别的地方去。如果你看到类似 `cannot bind non-const lvalue reference to an rvalue` 的编译错误，多半就是这个问题。

## const 引用传递——两全其美

值传递安全但有拷贝开销，引用传递高效但能修改原始数据。有没有一种方式既不拷贝又不允许修改？有——const 引用：

```cpp
void print(const std::string& s)
{
    std::cout << s << std::endl;
    // s += "!";  // 编译错误，const 引用不允许修改
}
```

`const std::string& s` 做了两件事：`&` 表示引用，不发生拷贝；`const` 表示只读，函数内部不能通过 `s` 修改原始字符串。调用者看到 `const` 就知道"这个函数不会动我的数据"，意图非常清晰。

const 引用还有一个重要特性：它能绑定到右值。非 const 引用不能绑定到字面量，但 const 引用可以：

```cpp
void print(const std::string& s);

print(std::string("hello"));  // OK：const 引用绑定到临时对象
print("world");               // OK：const 引用绑定到隐式构造的临时 string
```

这让 `const T&` 成为极其灵活的参数类型——既能接收左值也能接收右值，同时避免了拷贝、保证了只读。

我们回头看前面那个拷贝大结构体的例子，用 const 引用改写一下：

```cpp
void process(const SensorData& data)  // 零拷贝，只读访问
{
    // 处理数据...
}
```

拷贝开销消失了，同时 `data` 在函数内部是只读的，不会意外修改调用者的数据。这就是前面说的"两全其美"。

## 怎么选——传参方式的决策指南

三种传参方式各有适用场景，我们把决策规则整理清楚。对于基本类型（`int`、`double`、`float`、指针等，通常不超过 8 字节），直接用值传递。这些类型拷贝成本极低，值传递既安全又简单，而且对编译器优化更友好。如果你看到有人写 `void foo(const int& x)`，那大概率是过度优化——传一个 `int` 的引用并不比传 `int` 本身快，在某些平台上反而更慢（引用本质上是指针实现的，需要一次额外的间接寻址）。

对于比较大或者比较复杂的类型（`std::string`、`std::vector`、自定义结构体等），如果函数只是读取数据而不修改，用 `const T&`。如果函数需要修改调用者的数据（比如 `swap`、填充输出结构体），用非 const 引用 `T&`。

总结成一张表：

```text
参数类型         不修改            需要修改
-----------     ----------       ----------
基本类型         T（值传递）       T（值传递后返回）
非平凡类型       const T&          T&
```

这条规则在绝大多数情况下都适用。等你学到移动语义和完美转发之后，会知道还有更精细的传参策略（比如按值传递 + move），但在当前阶段，上面这张表就足够指导日常编码了。

## 返回值——怎么把结果交还给调用者

函数的返回值同样涉及传递方式的选择。大多数情况下，直接返回 by value 就对了：

```cpp
std::string greet(const std::string& name)
{
    return "Hello, " + name + "!";
}
```

你可能会担心：返回一个 `std::string` 不会发生拷贝吗？实际上，现代 C++ 编译器会做两种关键优化——RVO（Return Value Optimization）和 NRVO（Named Return Value Optimization）。简单来说，编译器会直接在调用者预留的内存空间里构造返回值，省去中间的拷贝或移动操作。从 C++17 开始，RVO 在某些情况下甚至是强制保证的。所以 `return "Hello, " + name + "!";` 并不会产生额外的字符串拷贝，性能方面完全不用担心。

但如果你试图返回局部变量的引用，事情就危险了：

```cpp
const int& get_value()
{
    int x = 42;
    return x;  // 返回局部变量的引用——悬垂引用！
}
```

> **踩坑预警**：这段代码能编译通过，但运行时是未定义行为（Undefined Behavior）。`x` 是函数内的局部变量，函数返回后 `x` 的内存就被回收了——你返回的引用指向了一块已经不存在的内存。通过这个引用去读取数据，可能读到垃圾值，可能读到"碰巧还在"的旧值，也可能直接段错误。编译器不会报错（语法上完全合法），所以这个 bug 非常隐蔽。原则很简单：永远不要返回局部变量的引用或指针。返回 by value 就好了，编译器会帮你优化。

## 输出参数 vs 返回值

当函数需要产生多个结果时，老式 C 代码常用引用参数来"输出"，但调用端 `divide(a, b, q, r)` 不看签名根本分不清输入输出。现代 C++ 更推荐用结构体直接返回：

```cpp
struct DivResult {
    int quotient;
    int remainder;
};

DivResult divide(int a, int b)
{
    return {a / b, a % b};
}
```

调用端 `auto result = divide(a, b);` 一目了然，`result.quotient` 比 `result.first` 可读性强太多了。输出参数在往大缓冲区填充数据的场景下仍有意义，但大多数时候优先选择返回值。

## 实战演练——passing.cpp

现在我们把这一章的知识点串起来，写一个完整的示例程序。这个程序会演示 `swap` 操作、不同传参方式的性能对比，以及 const 引用在字符串处理中的用法。

```cpp
// passing.cpp —— 演示值传递、引用传递和 const 引用传递

#include <iostream>
#include <string>
#include <chrono>

/// @brief 交换两个整数的值
void swap_values(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

struct BigData {
    int payload[4096];  // 16 KB
};

/// @brief 值传递版本：每次调用拷贝整个 BigData
long sum_by_value(BigData data)
{
    long total = 0;
    for (int i = 0; i < 4096; ++i) {
        total += data.payload[i];
    }
    return total;
}

/// @brief const 引用版本：零拷贝
long sum_by_const_ref(const BigData& data)
{
    long total = 0;
    for (int i = 0; i < 4096; ++i) {
        total += data.payload[i];
    }
    return total;
}

/// @brief 拼接问候语，const 引用避免字符串拷贝
std::string build_greeting(const std::string& name)
{
    return "Hello, " + name + "! Welcome to Modern C++.";
}

int main()
{
    // swap 演示
    int a = 10;
    int b = 20;
    std::cout << "交换前: a = " << a << ", b = " << b << std::endl;
    swap_values(a, b);
    std::cout << "交换后: a = " << a << ", b = " << b << std::endl;

    // 性能对比
    BigData data{};
    for (int i = 0; i < 4096; ++i) {
        data.payload[i] = i;
    }

    constexpr int kIterations = 100000;

    auto start = std::chrono::high_resolution_clock::now();
    long result_value = 0;
    for (int i = 0; i < kIterations; ++i) {
        result_value = sum_by_value(data);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto ms_value = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end - start)
                        .count();

    start = std::chrono::high_resolution_clock::now();
    long result_ref = 0;
    for (int i = 0; i < kIterations; ++i) {
        result_ref = sum_by_const_ref(data);
    }
    end = std::chrono::high_resolution_clock::now();
    auto ms_ref = std::chrono::duration_cast<std::chrono::milliseconds>(
                      end - start)
                      .count();

    std::cout << "\n--- 性能对比 (" << kIterations << " 次调用) ---"
              << std::endl;
    std::cout << "值传递: " << result_value
              << ", 耗时: " << ms_value << " ms" << std::endl;
    std::cout << "const引用: " << result_ref
              << ", 耗时: " << ms_ref << " ms" << std::endl;

    // 字符串处理
    std::string name = "Charlie";
    std::cout << build_greeting(name) << std::endl;
    std::cout << build_greeting(std::string("World")) << std::endl;

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -O2 -Wall -Wextra -o passing passing.cpp
./passing
```

预期输出：

```text
交换前: a = 10, b = 20
交换后: a = 20, b = 10

--- 性能对比 (100000 次调用) ---
值传递结果: 8386560, 耗时: 680 ms
const 引用结果: 8386560, 耗时: 190 ms

--- const 引用处理字符串 ---
Hello, Charlie! Welcome to Modern C++.
Hello, World! Welcome to Modern C++.
```

性能数据会因机器不同而有差异，但趋势一致：值传递每次拷贝 16 KB，const 引用版本避开了拷贝，快了数倍。注意我们用了 `-O2`，即使如此编译器也必须遵守语言语义——你让它拷贝它就得拷贝。

`build_greeting` 的两次调用也值得注意：第一次传左值 `name`，第二次传临时对象 `std::string("World")`——两者都能通过 `const std::string&` 接收，这正是 const 引用的灵活性所在。

## 动手试试

### 练习一：实现 swap

写一个 `swap_values` 函数交换两个 `double` 的值，再写一个重载版本交换两个 `std::string`。用 `main` 函数验证结果。

### 练习二：高效处理大型结构体

定义一个包含至少 1000 个 `double` 元素数组的结构体 `Measurement`。写两个函数：一个用值传递计算平均值，一个用 const 引用传递计算平均值。分别计时比较性能。

### 练习三：修复悬垂引用

下面这段代码有什么问题？找到 bug 并修复它。

```cpp
const std::string& get_prefix()
{
    std::string prefix = "user_";
    return prefix;
}

int main()
{
    std::string name = get_prefix() + "admin";
    std::cout << name << std::endl;
    return 0;
}
```

提示：想想函数返回后局部变量 `prefix` 会发生什么。

## 小结

这一章我们搞清楚了 C++ 的三种核心参数传递方式。值传递会拷贝实参，函数操作的是副本，对基本类型来说简单安全，但对大对象则有不可忽视的性能开销。引用传递让函数直接访问调用者的原始变量，零拷贝且能修改数据，适合 `swap` 这类需要改变实参的场景，但非 const 引用不能绑定到右值。const 引用传递兼具零拷贝和只读安全性，`const T&` 能绑定到左值和右值，是处理非平凡类型只读参数的标准做法。

返回值方面，直接按值返回就好，现代编译器的 RVO/NRVO 优化会消除不必要的拷贝。绝对不要返回局部变量的引用——那是悬垂引用的经典来源。当函数需要输出多个结果时，优先用结构体返回值而不是输出参数。

下一章我们学习函数重载和默认参数——让同一个函数名根据参数类型或数量表现出不同的行为，这是 C++ 多态性的基础之一。
