---
title: "类模板"
description: "掌握类模板定义、成员函数和模板参数，实现泛型栈"
chapter: 9
order: 2
difficulty: intermediate
reading_time_minutes: 12
platform: host
prerequisites:
  - "函数模板"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
---

# 类模板

上一章我们学会了用 `template <typename T>` 让函数变成泛型的——一个 `max_value` 就能处理各种类型。但函数模板只能泛化"一段逻辑"。如果我们想要一个泛化的"数据结构"呢？比如一个栈——它的 push、pop、top 操作对各种类型来说逻辑完全一样，但栈内部需要存储一组同类型的元素，这个"类型"在编写类的时候就决定了。C++ 标准库之所以能提供 `std::vector<int>`、`std::vector<std::string>` 这样灵活的容器，靠的就是类模板（class template）。这也是我们本章的主角！它让我们把类型参数化到整个类的层面——成员变量、成员函数、甚至嵌套类型都可以使用模板参数。这一章我们就来搞清楚类模板的语法、成员函数的定义方式、模板参数的种类，最后手把手实现一个完整的泛型栈。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 使用 `template <typename T>` 语法定义类模板
> - [ ] 在类内和类外定义模板类的成员函数
> - [ ] 区分类型参数与非类型参数，掌握默认模板参数的用法
> - [ ] 了解 C++17 的 CTAD（类模板参数推导）基本概念
> - [ ] 实现一个完整的 `Stack<T>` 泛型栈

## 第一步——理解类模板的基本语法

类模板的定义从 `template <typename T>` 开始，后面紧跟类的定义。所有出现 `T` 的地方在实例化时都会被替换为实际类型——包括成员变量、成员函数参数、返回类型，甚至友元声明。

```cpp
template <typename T>
class Stack
{
public:
    void push(const T& value);
    void pop();
    T& top();
    const T& top() const;
    bool empty() const;
    std::size_t size() const;

private:
    std::vector<T> data_;
};
```

`data_` 是 `std::vector<T>` 类型——模板里面套模板，这在 C++ 中非常常见。实例化时，`Stack<int>` 的 `data_` 就是 `std::vector<int>`，`Stack<std::string>` 的 `data_` 就是 `std::vector<std::string>`。

使用类模板时，必须提供具体的模板参数（C++17 的 CTAD 场景稍后讨论）：

```cpp
Stack<int> int_stack;           // T = int
Stack<double> double_stack;     // T = double
Stack<std::string> str_stack;   // T = std::string
```

敲桌子孩子们。这里和函数模板的一个重要区别：函数模板的参数类型通常可以通过调用参数推导出来，但类模板不行——实例化对象时编译器没法从构造函数推导出 `T`（C++17 之前），你必须老老实实写明 `Stack<int>`。

## 第二步——搞定成员函数的类内与类外定义

类模板的成员函数可以在类体内直接定义，也可以在类体外定义。类体内定义和普通类一样，没什么特别的。但类体外定义就需要注意了——每一个在类体外定义的成员函数，都必须带上完整的模板头部。

简单的成员函数直接写在类体内即可，这也是最常见的做法：

```cpp
template <typename T>
class Stack
{
public:
    bool empty() const { return data_.empty(); }
    std::size_t size() const { return data_.size(); }

private:
    std::vector<T> data_;
};
```

类外定义时，需要用 `Stack<T>::` 来限定成员函数所属的类，而且函数前面必须加上模板头部 `template <typename T>`。每一个在类外定义的成员函数都需要这样做，一个都不能漏：

```cpp
template <typename T>
void Stack<T>::push(const T& value)
{
    data_.push_back(value);
}

template <typename T>
void Stack<T>::pop()
{
    if (data_.empty()) {
        throw std::out_of_range("Stack<>::pop(): empty stack");
    }
    data_.pop_back();
}

template <typename T>
T& Stack<T>::top()
{
    if (data_.empty()) {
        throw std::out_of_range("Stack<>::top(): empty stack");
    }
    return data_.back();
}

template <typename T>
const T& Stack<T>::top() const
{
    if (data_.empty()) {
        throw std::out_of_range("Stack<>::top(): empty stack");
    }
    return data_.back();
}
```

`Stack<T>::` 中的 `<T>` 不能省——因为 `Stack` 本身是一个模板，只有 `Stack<T>` 才是具体的类。如果有多个模板参数，比如 `template <typename T, typename Alloc>`，类外定义就要写 `Stack<T, Alloc>::`，模板头部也要完整带上。

## 第三步——摸清模板参数的三副面孔

C++ 的模板体系支持三种参数：类型参数、非类型参数和模板模板参数。这一节我们看前两种。

### 类型参数——你一直在用的形式

`typename T`（或 `class T`）就是类型参数，可以有多个：

```cpp
template <typename Key, typename Value>
class Dictionary
{
    // ...
};
```

`std::map<Key, Value>` 就是这种模式。

### 非类型参数——编译期的常量

非类型模板参数（non-type template parameter）是一个编译期常量值，而不是类型。最常见的用法是指定容器容量：

```cpp
template <typename T, std::size_t kCapacity>
class RingBuffer
{
public:
    void push(const T& value)
    {
        buffer_[write_index_] = value;
        write_index_ = (write_index_ + 1) % kCapacity;
    }

    // ...

private:
    std::array<T, kCapacity> buffer_;
    std::size_t write_index_ = 0;
};
```

`kCapacity` 直接参与数组声明，实例化时必须提供编译期已知的值：

```cpp
RingBuffer<int, 16> buffer;        // 容量为 16 的 int 环形缓冲区
RingBuffer<double, 256> big_buf;   // 容量为 256 的 double 环形缓冲区
```

非类型参数只能是整型、枚举、指针、引用，或 C++20 起的浮点数和类类型。大多数情况下我们用整型就足够了。

### 默认模板参数——从右向左

模板参数也支持默认值，从右向左连续提供：

```cpp
template <typename T, typename Container = std::vector<T>>
class Stack
{
public:
    void push(const T& value) { data_.push_back(value); }
    // ...

private:
    Container data_;
};

Stack<int> s1;                                  // Container 默认为 std::vector<int>
Stack<int, std::deque<int>> s2;                 // Container 显式指定为 std::deque<int>
```

标准库的 `std::stack` 就是这种设计——第二参数默认为 `std::vector<T>`，可以换成 `std::deque<T>` 或 `std::list<T>`。

## 快速了解 CTAD——让编译器推导模板参数（C++17）

C++17 引入了 CTAD（Class Template Argument Deduction），让编译器根据构造函数参数自动推导模板参数类型。最常见的例子：`std::vector v = {1, 2, 3}` 会被推导为 `std::vector<int>`，`std::pair p(1, 2.5)` 会被推导为 `std::pair<int, double>`。对于我们自己写的类模板，如果构造函数参数能唯一确定模板参数类型，CTAD 也能工作。不过 CTAD 的推导规则比较复杂，有时结果和预期不一样。初学阶段了解这个特性就行，不确定时老老实实写明模板参数。

## 上号——实现一个完整的泛型栈

现在我们把前面的内容综合起来，实现一个完整的泛型栈。底层用 `std::vector<T>` 存储，提供 push、pop、top、empty、size 五个操作。所有代码写在一个头文件中——模板代码必须放在头文件里，原因稍后解释。

```cpp
// stack.hpp
// 编译: g++ -Wall -Wextra -std=c++17 stack_demo.cpp -o stack_demo
#pragma once

#include <stdexcept>
#include <vector>

/// @brief 泛型栈，底层使用 std::vector 存储
/// @tparam T 元素类型
template <typename T>
class Stack
{
public:
    /// @brief 将元素压入栈顶
    void push(const T& value) { data_.push_back(value); }

    /// @brief 弹出栈顶元素
    /// @throws std::out_of_range 栈为空时抛出异常
    void pop()
    {
        if (data_.empty()) {
            throw std::out_of_range("Stack::pop(): stack is empty");
        }
        data_.pop_back();
    }

    /// @brief 访问栈顶元素（可修改）
    /// @throws std::out_of_range 栈为空时抛出异常
    T& top()
    {
        if (data_.empty()) {
            throw std::out_of_range("Stack::top(): stack is empty");
        }
        return data_.back();
    }

    /// @brief 访问栈顶元素（只读）
    /// @throws std::out_of_range 栈为空时抛出异常
    const T& top() const
    {
        if (data_.empty()) {
            throw std::out_of_range("Stack::top(): stack is empty");
        }
        return data_.back();
    }

    /// @brief 判断栈是否为空
    bool empty() const { return data_.empty(); }

    /// @brief 返回栈中元素数量
    std::size_t size() const { return data_.size(); }

private:
    std::vector<T> data_;
};
```

所有操作都委托给内部的 `std::vector<T>` 来完成。`pop` 和 `top` 在栈为空时抛出 `std::out_of_range` 异常，这和标准库 `std::stack` 的行为不同——标准库在空栈上是未定义行为（UB）。我们选择抛异常是为了让错误更容易被发现。

接下来写测试程序，用三种不同类型来实例化 `Stack`：

```cpp
// stack_demo.cpp
#include <iostream>
#include <string>
#include "stack.hpp"

int main()
{
    // --- Stack<int> ---
    std::cout << "=== Stack<int> ===\n";
    Stack<int> int_stack;
    int_stack.push(10);
    int_stack.push(20);
    int_stack.push(30);
    std::cout << "size: " << int_stack.size() << "\n";
    std::cout << "top:  " << int_stack.top() << "\n";
    int_stack.pop();
    std::cout << "after pop, top: " << int_stack.top() << "\n";
    std::cout << "empty: " << std::boolalpha << int_stack.empty()
              << "\n";

    // --- Stack<double> ---
    std::cout << "\n=== Stack<double> ===\n";
    Stack<double> dbl_stack;
    dbl_stack.push(3.14);
    dbl_stack.push(2.718);
    std::cout << "size: " << dbl_stack.size() << "\n";
    std::cout << "top:  " << dbl_stack.top() << "\n";
    dbl_stack.pop();
    std::cout << "after pop, top: " << dbl_stack.top() << "\n";

    // --- Stack<std::string> ---
    std::cout << "\n=== Stack<std::string> ===\n";
    Stack<std::string> str_stack;
    str_stack.push("hello");
    str_stack.push("world");
    str_stack.push("template");
    std::cout << "size: " << str_stack.size() << "\n";
    std::cout << "top:  " << str_stack.top() << "\n";
    str_stack.pop();
    std::cout << "after pop, top: " << str_stack.top() << "\n";

    // --- 异常测试 ---
    std::cout << "\n=== Exception test ===\n";
    Stack<int> empty_stack;
    try {
        empty_stack.pop();
    } catch (const std::out_of_range& e) {
        std::cout << "caught: " << e.what() << "\n";
    }

    return 0;
}
```

### 验证运行

```bash
g++ -Wall -Wextra -std=c++17 stack_demo.cpp -o stack_demo && ./stack_demo
```

预期输出：

```text
=== Stack<int> ===
size: 3
top:  30
after pop, top: 20
empty: false

=== Stack<double> ===
size: 2
top:  2.718
after pop, top: 3.14

=== Stack<std::string> ===
size: 3
top:  template
after pop, top: world

=== Exception test ===
caught: Stack::pop(): stack is empty
```

核对关键结果：`Stack<int>` 压入三个元素后 top 是 `30`（最后压入的），pop 一次后 top 变成 `20`，正确。`Stack<double>` 和 `Stack<std::string>` 的行为也符合 LIFO（后进先出）预期。空栈调用 `pop` 时正确抛出 `std::out_of_range` 异常。

## 踩坑预警——模板的三个暗坑

写类模板时，有三个坑几乎是所有 C++ 程序员都踩过的。我们逐个拆解。

**暗坑一：模板声明和实现必须放在头文件中。** 你可能注意到了，我们把 `Stack` 的声明和实现全部放在了 `stack.hpp` 头文件中，没有分成 `.hpp` 和 `.cpp`。这不是偷懒——这是由 C++ 的编译模型决定的。每个 `.cpp` 文件独立编译，编译器在处理一个编译单元时只需要看到声明就能编译通过，具体实现留到链接阶段再解决。但模板不同——模板本身不是代码，它是"代码配方"。编译器必须看到模板的完整定义才能实例化出具体的代码。如果把声明放在 `.h`、实现放在 `.cpp`，其他编译单元在实例化 `Stack<int>` 时只能看到声明、找不到实现，链接时就会报 `undefined reference` 错误。最常见的做法就是把所有代码写在头文件里。如果你确实想分离声明和实现，可以使用显式实例化——在 `.cpp` 文件中写 `template class Stack<int>;`，强制编译器在这个编译单元内生成 `Stack<int>` 的所有成员函数——但这样一来，模板就只能支持你显式列出的那些类型了，失去了泛型的灵活性。

**暗坑二：模板错误信息又臭又长。** 因为模板实例化发生在编译期，如果模板代码内部有错误，编译器会把模板展开后的完整上下文都塞进错误信息里。一个简单的类型不匹配可能产生上百行的报错。C++20 的 Concepts 在很大程度上改善了这个问题——它让你在模板参数上添加约束，错误信息会直接告诉你"哪个约束不满足"而不是"在这一大坨实例化链中某个操作符不匹配"。不过 Concepts 我们后面才会讲到，现阶段遇到模板报错，先看最后一行，找到你自己的调用代码，然后往回推导类型。

**暗坑三：代码膨胀（code bloat）。** 如果你用 10 种不同的类型实例化了 `Stack`，编译器就会生成 10 份完整的代码——每份都包含 `push`、`pop`、`top`、`empty`、`size` 的完整实现。对于小型类模板这通常不是问题，但对于大型模板或者嵌入式平台来说，代码体积的增长可能是不可接受的。缓解策略包括：将不依赖模板参数的代码提取到非模板基类中、使用 `if constexpr` 在编译期分支以减少冗余实例化、以及在库级别通过显式实例化控制哪些版本被编译。

## 练习

### 练习 1：实现 Pair\<T, U\>

实现一个泛型的 `Pair` 类模板，存储两个不同类型的值。要求提供 `first()` 和 `second()` 访问器（const 和非 const 版本），以及一个 `swap(Pair& other)` 成员函数用来交换两个 `Pair` 对象的内容。用 `Pair<int, std::string>` 和 `Pair<double, char>` 分别测试。提示：类模板可以接受多个类型参数，写法是 `template <typename T, typename U>`。

### 练习 2：实现 RingBuffer\<T, N\>

实现一个环形缓冲区类模板，使用非类型模板参数 `std::size_t kCapacity` 指定容量。要求提供 `push(const T&)` 写入元素、`pop()` 读取并移除最早写入的元素、`full() const` 和 `empty() const` 判断状态、以及 `size() const` 返回当前元素数量。底层使用 `std::array<T, kCapacity>` 存储，用两个索引（读和写）追踪位置。环形缓冲区的核心思路是用取模运算 `% kCapacity` 让索引在数组末尾回绕到头部。

## 小结

这一章我们把泛型的能力从函数扩展到了类。类模板的核心语法和函数模板几乎一样——`template <typename T>` 打头，`T` 可以出现在成员变量、成员函数参数、返回类型等任何需要类型的地方。类外定义成员函数时必须带上完整的模板头部并用 `ClassName<T>::` 限定，这是新手最容易踩的坑。模板参数分为类型参数（`typename T`）和非类型参数（`std::size_t N`），两者可以混合使用，默认值从右向左连续提供。组织模板代码时，声明和实现必须放在头文件中（或使用显式实例化），同时需要注意代码膨胀——每种实例化类型都会生成一份完整的代码副本。

下一章我们进入模板特化——当通用方案对某些特定类型不够好时，如何为它们提供专门的实现。函数模板那一章我们已经简单接触过特化的概念，但类模板的特化更加灵活和强大，支持偏特化（partial specialization），这是构建高级泛型组件的核心工具。
