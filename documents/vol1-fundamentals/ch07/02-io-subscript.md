---
title: "流与下标运算符"
description: "掌握 << >> 重载和 operator[] 的实现，让自定义类型支持流式 I/O 和索引访问"
chapter: 7
order: 2
difficulty: intermediate
reading_time_minutes: 12
platform: host
prerequisites:
  - "算术与比较运算符"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
---
# 流与下标运算符

> 完整的仓库地址在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) 中，您也可以光顾一下，喜欢的话给一个 Star 激励一下作者

到目前为止，我们重载了算术和比较运算符，让 `Fraction` 和 `Vector3D` 这样的自定义类型能像 `int` 一样参与运算和比较。但如果你试着写 `std::cout << fraction;`，编译器会毫不留情地报错——它不知道怎么把你的类型塞进输出流。同样，自定义容器的 `container[0]` 也需要手动重载 `operator[]` 才能生效。

这两组运算符——流运算符 `<<`/`>>` 和下标运算符 `[]`——是让自定义类型真正"融入语言生态"的关键。一旦搞定它们，你的类型就能直接用 `cout` 打印、用 `cin` 读取、用方括号索引，和内置类型的体验完全一致。

## 重载 << 让对象能被打印

先回忆一下我们平时怎么打印变量的：`std::cout << 42 << " hello";`。`<<` 左边是 `std::ostream` 对象，右边是要输出的内容。所以 `std::cout << fraction` 的左操作数是 `ostream`，不是 `Fraction`——这意味着 `operator<<` **不能是成员函数**，因为成员函数的隐式第一参数是 `this`，而这里左操作数是流。

解决方案是实现为非成员函数（通常声明为友元），签名为：

```cpp
friend std::ostream& operator<<(std::ostream& os, const Fraction& f);
```

返回 `os` 的引用是为了支持链式调用——`cout << a << b` 等价于 `operator<<(operator<<(cout, a), b)`，第一次调用返回 `cout` 的引用，作为第二次调用的左操作数。

我们拿 `Fraction` 类来演示，只看 `operator<<` 部分（完整的类定义后面实战环节再给出）：

```cpp
friend std::ostream& operator<<(std::ostream& os, const Fraction& f)
{
    if (f.denominator == 1) {
        os << f.numerator;       // 整数形式：5/1 只输出 5
    }
    else {
        os << f.numerator << "/" << f.denominator;
    }
    return os;
}
```

使用起来和打印内置类型完全一样：`std::cout << Fraction(3, 4)` 输出 `3/4`，`std::cout << Fraction(5, 1)` 输出 `5`，链式调用 `cout << a << " and " << b` 也毫无问题。

这里有一个值得思考的设计选择：`operator<<` 需要访问 `Fraction` 的私有成员。把它声明为 `friend` 是最直接的做法；另一个方案是提供一个公有 `print` 成员函数，然后 `operator<<` 调用它。`friend` 更简洁，`print` 方法则在需要支持不同格式化输出时更灵活。

## 重载 >> 让对象能从流中读取

有输出就得有输入。`operator>>` 的签名和 `operator<<` 对称，但有两个关键区别：第二参数不是 `const` 引用（因为要往里写入数据），并且流是 `std::istream` 而非 `ostream`：

```cpp
friend std::istream& operator>>(std::istream& is, Fraction& f);
```

实现的时候需要考虑输入格式。我们约定输入格式为 `numerator/denominator`，中间用斜杠分隔：

```cpp
friend std::istream& operator>>(std::istream& is, Fraction& f)
{
    int num, denom;
    char slash;

    is >> num >> slash >> denom;

    // 检查流状态和分母合法性
    if (is && slash == '/' && denom != 0) {
        f.numerator = num;
        f.denominator = denom;
        f.reduce();
    }
    else {
        // 输入失败时设置流为失败状态
        is.setstate(std::ios::failbit);
    }

    return is;
}
```

> **踩坑预警**：`operator>>` 里一定要检查流状态。很多示例代码直接 `is >> num >> slash >> denom;` 就完事了，压根不判断读取是否成功。如果用户输入的不是数字（比如敲了个 `abc`），`is >> num` 就会失败，但后续代码依然用不确定的值去构造对象——这完全是未定义行为。正确做法是用 `if (is)` 检查流状态，再验证分隔符和分母合法性。此外，输入失败时**不要修改对象**——让它保持在输入前的状态，而不是赋予一个半初始化的垃圾值。
>
> **踩坑预警**：另一个常见错误是在输入失败时没有设置 `failbit`。如果你只检查了流状态但不设置 `failbit`，调用者就无法通过 `if (cin >> fraction)` 来判断输入是否成功。上面代码中 `is.setstate(std::ios::failbit)` 就是处理这种情况的。

使用方式和 `cin >>` 读取 `int` 一模一样：`if (std::cin >> f)` 在输入 `3/4` 后会让 `f` 变成 `Fraction(3, 4)`，输入 `abc` 则进入 `else` 分支报错。

## 下标运算符 operator[]

下标运算符是自定义容器类的标配——有了它，你的容器就能用 `obj[i]` 访问元素，和原生数组的体验完全一致。`operator[]` 必须实现为成员函数，而且**通常需要提供两个版本**：非 `const` 版本返回可修改的引用，`const` 版本返回只读引用。这个设计我们在 C++98 运算符重载那章已经见过，这里我们把它落实到实际代码中。

先用一个简洁的 `IntArray` 来演示基本结构：

```cpp
class IntArray {
private:
    int* data;
    std::size_t count;

public:
    explicit IntArray(std::size_t n)
        : data(new int[n]()), count(n)
    {
    }

    ~IntArray() { delete[] data; }

    // 禁止拷贝（简化示例，后面章节会讲移动语义）
    IntArray(const IntArray&) = delete;
    IntArray& operator=(const IntArray&) = delete;

    // 非 const 版本：允许读写
    int& operator[](std::size_t index)
    {
        return data[index];
    }

    // const 版本：只读
    const int& operator[](std::size_t index) const
    {
        return data[index];
    }

    std::size_t size() const { return count; }
};
```

两个版本的共存至关重要。非 `const` 对象调用 `arr[0] = 42` 走非 `const` 版本返回 `int&`，可以读写；`const` 引用调用 `ref[0]` 走 `const` 版本返回 `const int&`，只读——尝试 `ref[0] = 100` 直接编译报错。

> **踩坑预警**：如果你忘了提供 `const` 版本的 `operator[]`，那么任何通过 `const` 引用访问容器元素的操作都会编译失败。这在函数参数传递时特别常见——很多函数接受 `const IntArray&` 参数，内部用 `arr[i]` 读取元素，没有 `const` 版本直接报错。提供两个版本是标准的、也是推荐的做法。

### 边界检查：operator[] vs at()

`operator[]` 传统的做法是**不做边界检查**——这和原生数组的行为一致，追求最高性能，越界访问是未定义行为。如果你需要边界检查，标准库容器提供了 `at()` 成员函数，越界时抛出 `std::out_of_range` 异常。在自己的容器中也可以照做：

```cpp
int& at(std::size_t index)
{
    if (index >= count) {
        throw std::out_of_range("IntArray::at: index out of range");
    }
    return data[index];
}

const int& at(std::size_t index) const
{
    if (index >= count) {
        throw std::out_of_range("IntArray::at: index out of range");
    }
    return data[index];
}
```

这样就有了两种选择：`[]` 追求性能不检查，`at()` 追求安全抛异常。调试阶段用 `at()`、发布版本用 `[]` 是常见的策略。

## 实战：io_overload.cpp

把前面所有的知识整合成一个完整的示例程序：

```cpp
// io_overload.cpp
// 流运算符和下标运算符综合演练

#include <iostream>
#include <stdexcept>
#include <cmath>

class Fraction {
private:
    int numerator;
    int denominator;

    void reduce()
    {
        int a = std::abs(numerator);
        int b = std::abs(denominator);
        while (b != 0) {
            int temp = b;
            b = a % b;
            a = temp;
        }
        int gcd = (a != 0) ? a : 1;
        numerator /= gcd;
        denominator /= gcd;
        if (denominator < 0) {
            numerator = -numerator;
            denominator = -denominator;
        }
    }

public:
    Fraction(int num = 0, int denom = 1)
        : numerator(num), denominator(denom)
    {
        if (denominator == 0) {
            throw std::invalid_argument("分母不能为零");
        }
        reduce();
    }

    double to_double() const
    {
        return static_cast<double>(numerator) / denominator;
    }

    // 加法
    Fraction operator+(const Fraction& other) const
    {
        return Fraction(
            numerator * other.denominator + other.numerator * denominator,
            denominator * other.denominator
        );
    }

    // 输出流
    friend std::ostream& operator<<(std::ostream& os, const Fraction& f)
    {
        if (f.denominator == 1) {
            os << f.numerator;
        }
        else {
            os << f.numerator << "/" << f.denominator;
        }
        return os;
    }

    // 输入流
    friend std::istream& operator>>(std::istream& is, Fraction& f)
    {
        int num = 0;
        int denom = 1;
        char slash = '\0';

        is >> num >> slash >> denom;

        if (is && slash == '/' && denom != 0) {
            f.numerator = num;
            f.denominator = denom;
            f.reduce();
        }
        else {
            is.setstate(std::ios::failbit);
        }

        return is;
    }
};

class IntArray {
private:
    int* data;
    std::size_t count;

public:
    explicit IntArray(std::size_t n)
        : data(new int[n]()), count(n)
    {
    }

    ~IntArray() { delete[] data; }

    IntArray(const IntArray&) = delete;
    IntArray& operator=(const IntArray&) = delete;

    int& operator[](std::size_t index)
    {
        return data[index];
    }

    const int& operator[](std::size_t index) const
    {
        return data[index];
    }

    const int& at(std::size_t index) const
    {
        if (index >= count) {
            throw std::out_of_range("IntArray::at: index out of range");
        }
        return data[index];
    }

    std::size_t size() const { return count; }

    /// @brief 打印所有元素
    void print(std::ostream& os = std::cout) const
    {
        os << "[";
        for (std::size_t i = 0; i < count; ++i) {
            os << data[i];
            if (i + 1 < count) {
                os << ", ";
            }
        }
        os << "]";
    }
};

int main()
{
    // --- Fraction 输出演示 ---
    Fraction a(3, 4);
    Fraction b(2, 6);   // 自动约分为 1/3
    Fraction c(6, 1);   // 整数形式

    std::cout << "a = " << a << std::endl;    // 3/4
    std::cout << "b = " << b << std::endl;    // 1/3
    std::cout << "c = " << c << std::endl;    // 6
    std::cout << "a + b = " << (a + b) << std::endl;  // 13/12
    std::cout << "a (double) = " << a.to_double() << std::endl;  // 0.75
    std::cout << std::endl;

    // --- IntArray 下标访问演示 ---
    IntArray arr(5);
    for (std::size_t i = 0; i < arr.size(); ++i) {
        arr[i] = static_cast<int>(i * 10);  // 通过 [] 写入
    }

    std::cout << "arr = ";
    arr.print();
    std::cout << std::endl;

    const IntArray& const_arr = arr;
    std::cout << "const_arr[2] = " << const_arr[2] << std::endl;  // 20

    // 边界检查
    try {
        std::cout << "arr.at(10) = " << arr.at(10) << std::endl;
    }
    catch (const std::out_of_range& e) {
        std::cout << "捕获异常: " << e.what() << std::endl;
    }

    return 0;
}
```

编译运行：`g++ -std=c++17 -Wall -Wextra -o io_overload io_overload.cpp && ./io_overload`

预期输出：

```text
a = 3/4
b = 1/3
c = 6
a + b = 13/12
a (double) = 0.75

arr = [0, 10, 20, 30, 40]
const_arr[2] = 20
捕获异常: IntArray::at: index out of range
```

验证一下：`3/4 + 1/3 = 9/12 + 4/12 = 13/12`，正确。`arr` 被赋值为 `{0, 10, 20, 30, 40}`，`const_arr[2]` 是 20，`at(10)` 越界被异常捕获，都没问题。

## 动手试试

光看不练等于没学，建议每题都动手写一遍。

### 练习一：给之前的 Fraction 添加流运算符

如果你按照上一章的练习实现了自己的 `Fraction` 类，现在给它加上 `operator<<` 和 `operator>>`。要求 `operator<<` 在分母为 1 时只输出分子，`operator>>` 支持 `分子/分母` 格式的输入。输入失败时不要修改对象，并正确设置流的 `failbit`。写一段测试代码验证 `cin >> fraction` 和 `cout << fraction` 都能正常工作。

### 练习二：实现 Matrix 类的 operator[]

设计一个简单的 `Matrix` 类，内部用一维数组存储 N x M 的元素。重载 `operator[]` 使其返回某一行的首元素引用——这需要你定义一个辅助的 `Row` 代理类。先实现基础版本，只要求 `matrix[i][j]` 的读操作能正确工作，再考虑写操作。

提示：`matrix[i]` 返回一个 `Row` 对象，`Row::operator[]` 再返回具体的元素引用。这是 C++ 中经典的"代理模式"用法。

## 小结

这一章我们掌握了两组让自定义类型"融入语言生态"的运算符。流运算符 `<<` 和 `>>` 必须实现为非成员函数（因为左操作数是流对象，不是你的类），通常声明为友元以便访问私有数据；返回流的引用是为了支持 `cout << a << b << c` 这样的链式调用。`operator>>` 要特别注意检查流状态和输入合法性，失败时设置 `failbit` 且不修改对象。下标运算符 `operator[]` 是容器类的标配，必须同时提供 `const` 和非 `const` 两个版本——非 `const` 版本返回可修改的引用用于写入，`const` 版本返回只读引用用于读取。如果需要边界检查，额外提供 `at()` 方法，越界时抛出 `std::out_of_range` 异常。

下一章我们来看函数调用运算符 `operator()` 和类型转换运算符——前者让你的对象变成"可调用的"，后者控制你的类型如何与其他类型相互转换。这两个运算符用好了解放生产力，用不好就是调试噩梦的起点。
