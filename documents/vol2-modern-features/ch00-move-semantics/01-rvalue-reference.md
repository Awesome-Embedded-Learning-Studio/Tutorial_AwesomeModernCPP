---
title: "右值引用：从拷贝到移动"
description: "理解 C++ 值类别体系，掌握右值引用的绑定规则与核心语义"
chapter: 0
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 移动语义
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 15
prerequisites:
  - "卷一：C++ 基础入门"
related:
  - "移动构造与移动赋值"
  - "完美转发"
---

# 右值引用：从拷贝到移动

笔者在写 C++ 的头几年里，一直觉得"右值引用"这四个字散发着一种不可名状的学术味——`T&&` 是什么？左值右值到底怎么分？`std::move` 是不是真的在"移动"什么东西？每次看到别人的代码里出现 `std::move`，总是似懂非懂地抄过来，祈祷编译通过就好。后来深入一挖才发现，整个移动语义体系的基石其实就那么几条规则，理解了之后看什么代码都豁然开朗。这一篇我们就把这件事从头讲清楚：从值类别的分类体系出发，搞懂右值引用到底绑定了什么、`std::move` 到底做了什么、以及为什么它们能让你的程序跑得更快。

## 从一个让人血压升高的问题说起

假设我们有一个简单的动态字符串包装类：

```cpp
class StringWrapper {
    char* data_;
    std::size_t size_;

public:
    StringWrapper(const char* str)
    {
        size_ = std::strlen(str);
        data_ = new char[size_ + 1];
        std::memcpy(data_, str, size_ + 1);
    }

    // 拷贝构造：深拷贝
    StringWrapper(const StringWrapper& other)
        : size_(other.size_)
    {
        data_ = new char[size_ + 1];
        std::memcpy(data_, other.data_, size_ + 1);
    }

    ~StringWrapper()
    {
        delete[] data_;
    }
};
```

然后我们写一段看起来很无辜的代码：

```cpp
StringWrapper build_greeting(const std::string& name)
{
    StringWrapper result(("Hello, " + name + "!").c_str());
    return result;
}

int main()
{
    StringWrapper greeting = build_greeting("World");
    return 0;
}
```

在没有移动语义的世界里，`build_greeting` 返回 `result` 时会触发拷贝构造——分配一块新内存，把 `result` 里的字符串逐字节复制过去。然后 `result` 自己析构，释放掉原来那块内存。也就是说，我们花了一次内存分配加一次逐字节拷贝，只为了把一个马上就要销毁的对象里的数据"搬到"另一个位置上。如果字符串很长，比如一个几 KB 的 JSON 文本，这种拷贝就显得格外浪费——源对象反正马上就要死了，数据留在那块内存里也是白搭，为什么不直接把内存的控制权接管过来？

这就是移动语义要解决的核心问题。而要理解移动语义，我们必须先理解 C++ 是如何对表达式进行分类的——也就是所谓的**值类别**（value category）。

## 值类别的全景图

在 C++11 之前，事情比较简单：表达式要么是左值（lvalue），要么是右值（rvalue）。C++11 引入了移动语义之后，分类体系变成了三足鼎立：每个表达式恰好属于 **lvalue**、**xvalue**、**prvalue** 三者之一。这三个类别又可以两两组合成更宽泛的类别：**glvalue**（generalized lvalue）= lvalue + xvalue，**rvalue** = xvalue + prvalue。

如果你觉得这个分类体系有点绕，别急，笔者一开始也绕了半天。我们可以从两个属性来理解它：**有身份**（has identity，指的是表达式有名字、能取地址）和 **可移动**（can be moved from，指的是表达式是临时的、可以被安全地"偷走"资源）。

有身份且不可移动的是 **lvalue**——比如普通变量 `int x = 10;` 里的 `x`，它有名字、有地址、生命周期还没有结束，你当然不能随便把它的资源偷走。有身份且可移动的是 **xvalue**（expiring value）——比如 `std::move(x)` 的结果，它告诉你"这个对象有身份，但它马上就要死了，你可以安全地偷走它的资源"。没有身份但可移动的是 **prvalue**（pure rvalue）——比如字面量 `42` 或者函数返回的临时对象，它本来就没有名字，你不需要担心偷了之后谁会再访问它。

我们来看一组具体的例子来把这三类区分清楚。

```cpp
int x = 10;            // x 是 lvalue
int&& r = std::move(x); // std::move(x) 是 xvalue
int y = x + 1;         // x + 1 是 prvalue
int z = 42;            // 42 是 prvalue
```

这里面 `x` 是最典型的 lvalue——有名字，有地址，`&x` 是合法表达式。`std::move(x)` 产生的是一个 xvalue，它和 `x` 指向同一块内存，但语义上标记为"即将过期"。`x + 1` 和 `42` 都是 prvalue——临时的、没有名字的值。

> ⚠️ **踩坑预警**：有一个经典的误区是"左值可以出现在赋值号左边，右值只能在右边"。这个说法在 C 语言时代基本成立，但在 C++ 里它既不充分也不必要。`const int cx = 10;` 中 `cx` 是 lvalue，但 `cx = 20;` 编译不过——const 限制了修改，但不改变值类别。反过来，`std::string("hello")` 是 prvalue，但在 C++11 之后某些情况下也能出现在赋值号左边（比如调用成员函数）。

## 右值引用的绑定规则

理解了值类别，我们来看看右值引用——`T&&`——到底能绑定到什么上面。规则其实很简单：**右值引用只能绑定到右值（prvalue 或 xvalue）上，不能绑定到左值上**。

```cpp
int x = 10;

int&& r1 = 42;           // OK：42 是 prvalue
int&& r2 = x + 1;        // OK：x + 1 是 prvalue
int&& r3 = std::move(x); // OK：std::move(x) 是 xvalue

// int&& r4 = x;         // 编译错误：x 是 lvalue，不能绑定到右值引用
```

如果你取消注释最后一行，GCC 会给你一个相当直接的错误信息：

```text
error: cannot bind rvalue reference of type 'int&&' to lvalue of type 'int'
```

这个绑定规则背后的直觉是：右值引用的设计目的是让你能够"接管"临时对象的资源。如果一个对象是 lvalue（有名字、有地址、还在被人用），你怎么能安全地偷走它的东西呢？编译器在这里拦住你，完全是为了安全。

现在我们来看看右值引用和 const 左值引用在绑定行为上的对比，这对理解后续的移动构造函数至关重要。

const 左值引用 `const T&` 是 C++ 里的"万能接收器"——它可以绑定到任何东西上：左值、右值、const、非 const，来者不拒。而右值引用 `T&&` 是"挑剔接收器"——它只接受右值。这个差异看起来简单，但它引出了一个非常重要的实战区别：当你用 `const T&` 接收一个右值时，你承诺了"我不修改它"，所以你没法偷走它的资源；当你用 `T&&` 接收一个右值时，你有了修改它的权限，所以你可以安全地把资源转移走。

```cpp
void process_const_ref(const std::string& s)
{
    // 可以读取 s，但不能修改它
    // 所以无法"偷走" s 的内部缓冲区
    std::cout << s.size() << "\n";
}

void process_rvalue_ref(std::string&& s)
{
    // s 是非 const 的右值引用，可以修改它
    // 所以可以安全地转移 s 的内部资源
    std::string stolen = std::move(s);
    // 此时 s 处于"有效但未指定"的状态
}
```

你可能会问：为什么不让右值引用也能绑定左值？原因在于，如果 `T&&` 可以绑定到任何东西上，那我们就失去了区分"这个对象可以安全偷取"和"这个对象还在使用中"的能力——而这个区分恰恰是移动语义存在的根本理由。

## std::move 的本质——一个精心包装的类型转换

`std::move` 这个名字大概是 C++ 历史上最具误导性的命名之一。它听起来像是"移动"了什么东西，但实际上它**什么都没移动**。`std::move` 只做一件事：**把它的参数转换成右值引用**，也就是 `static_cast<T&&>`。仅此而已，不多不少。

我们可以自己实现一个等价的 `move`：

```cpp
template<typename T>
constexpr typename std::remove_reference<T>::type&&
my_move(T&& t) noexcept
{
    return static_cast<typename std::remove_reference<T>::type&&>(t);
}
```

这段代码做的事情非常直接：不管传入的 `T` 是什么类型，先用 `remove_reference` 把可能存在的引用去掉，然后 `static_cast` 成右值引用。整个过程中没有任何数据被移动、被拷贝、或者被修改——它纯粹是一个类型转换。

那它到底有什么用？关键在于**移动构造函数和移动赋值运算符的签名**。当你写 `std::string a = std::move(b);` 的时候，`std::move(b)` 把 `b` 转换成 `std::string&&`，这个右值引用去匹配 `std::string` 的移动构造函数 `std::string(std::string&& other)`。移动构造函数才是真正执行"资源转移"操作的家伙——它偷走 `other` 的内部缓冲区指针，把 `other` 的指针置空。`std::move` 只是在旁边递了一把钥匙。

```cpp
std::string a = "Hello";
std::string b = std::move(a);  // std::move 只是转换类型
                                  // 移动构造函数做了实际的资源转移
// 此刻 a 处于"有效但未指定"的状态
// 在大多数实现中 a 变成空字符串，但你不应该依赖这个行为
```

这里有一个非常容易踩的坑：**对基本类型使用 `std::move` 不会带来任何性能收益**。`std::move(42)` 只是把 `int` 转换成 `int&&`，但 `int` 的"移动"和"拷贝"是同一回事——都是复制四个字节。移动语义的威力只体现在**管理了资源的类**上，比如持有动态内存、文件句柄、网络连接的类。

## 临时对象的生命周期——右值引用延长了什么

在 C++ 中，临时对象（prvalue）的生命周期通常在包含它的完整表达式结束时终止。但右值引用和 const 左值引用有一个特殊能力：绑定到临时对象时，会延长该临时对象的生命周期，让它活到引用的作用域结束。

```cpp
const int& cr = 42;       // const 引用延长了 42 的生命周期
std::cout << cr << "\n";   // OK：42 还活着

int&& rr = 100;            // 右值引用也延长了 100 的生命周期
std::cout << rr << "\n";   // OK：100 还活着
```

这两者在延长生命周期上的行为是一样的，区别在于 `rr` 是非 const 的——你可以修改它。这看起来有点怪，一个字面量 `100` 怎么能被修改？实际上编译器在幕后把这个临时值放到了一块存储空间里，`rr` 指向的就是这块空间。

```cpp
int&& rr = 100;
rr = 200;                  // 合法！rr 指向的存储空间被修改了
std::cout << rr << "\n";   // 输出 200
```

这个特性在实战中用得不多，但理解它能帮你消除对"右值引用是不是马上就悬空了"的恐惧。当你写 `std::string&& ref = std::move(name);` 的时候，`ref` 指向的对象不会在下一行就消失——它一直活到 `ref` 的作用域结束。

## 通用示例——字符串拼接中的拷贝与移动

让我们把前面学的东西放在一起，看一个真实的例子。假设我们在构建日志消息：

```cpp
#include <iostream>
#include <string>
#include <vector>

std::string build_log_message(
    const std::string& level,
    const std::string& module,
    const std::string& detail)
{
    std::string msg = "[" + level + "] " + module + ": " + detail;
    return msg;
}

int main()
{
    std::string log = build_log_message("ERROR", "Network", "Connection timeout");
    std::cout << log << "\n";
    return 0;
}
```

这里面的 `"[" + level + "] " + module + ": " + detail` 产生了大量的临时 `std::string` 对象——每做一次 `+` 都会创建一个新的临时字符串。在 C++03 的世界里，每一次 `+` 都会导致一次内存分配和一次数据拷贝。C++11 之后，编译器可以在多个环节利用移动语义来优化这些临时对象的传递。

更直接的收益来自函数返回。`build_log_message` 返回 `msg`，编译器在这里有两种优化手段：NRVO（命名返回值优化）可以直接消除这次拷贝，退一步说，即使 NRVO 没生效，C++11 也会自动把 `msg` 当作右值来处理（隐式移动），调用 `std::string` 的移动构造函数——只转移内部指针，不复制字符数据。

再来看一个容器元素转移的例子：

```cpp
std::vector<std::string> names;

std::string name = "Alice";
names.push_back(std::move(name));  // 移动：name 的内部数据转移到 vector 中
// name 现在处于有效但未指定的状态，不要再使用它

names.push_back("Bob");   // 直接构造：临时 std::string 在 vector 的空间中就地构造
```

第一个 `push_back` 使用了移动语义：`std::move(name)` 把 `name` 转成右值引用，vector 调用 `std::string` 的移动构造函数来构造新元素——代价是转移一个指针和两个 `size_t`，而不是复制整个字符串内容。第二个 `push_back("Bob")` 利用了 `std::string` 的 `const char*` 构造函数，临时 `std::string` 直接在 vector 的存储空间里构造——这甚至比移动还快，因为它连"转移指针"那一步都省了。

## 动手实验——rvalue_demo.cpp

我们来写一个完整的程序，把右值引用的绑定规则、`std::move` 的行为、以及临时对象的生命周期全部跑一遍。

```cpp
// rvalue_demo.cpp -- 右值引用与值类别演示
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>

class Tracker
{
    std::string name_;
    static int kDefaultId;

public:
    explicit Tracker(std::string name)
        : name_(std::move(name))
    {
        std::cout << "  [" << name_ << "] 构造\n";
    }

    Tracker(const Tracker& other)
        : name_(other.name_ + "_copy")
    {
        std::cout << "  [" << name_ << "] 拷贝构造\n";
    }

    Tracker(Tracker&& other) noexcept
        : name_(std::move(other.name_))
    {
        other.name_ = "(moved-from)";
        std::cout << "  [" << name_ << "] 移动构造\n";
    }

    ~Tracker()
    {
        std::cout << "  [" << name_ << "] 析构\n";
    }

    Tracker& operator=(const Tracker& other)
    {
        name_ = other.name_ + "_copy";
        std::cout << "  [" << name_ << "] 拷贝赋值\n";
        return *this;
    }

    Tracker& operator=(Tracker&& other) noexcept
    {
        name_ = std::move(other.name_);
        other.name_ = "(moved-from)";
        std::cout << "  [" << name_ << "] 移动赋值\n";
        return *this;
    }

    const std::string& name() const { return name_; }
};

int Tracker::kDefaultId = 0;

/// @brief 返回临时对象（prvalue）
Tracker make_tracker(std::string name)
{
    return Tracker(std::move(name));
}

int main()
{
    std::cout << "=== 1. 基本构造 ===\n";
    Tracker a("A");
    std::cout << '\n';

    std::cout << "=== 2. 拷贝构造 ===\n";
    Tracker b = a;
    std::cout << "  a.name = " << a.name() << "\n";
    std::cout << "  b.name = " << b.name() << "\n\n";

    std::cout << "=== 3. 移动构造（显式 std::move）===\n";
    Tracker c = std::move(a);
    std::cout << "  a.name = " << a.name() << "\n";
    std::cout << "  c.name = " << c.name() << "\n\n";

    std::cout << "=== 4. 返回临时对象 ===\n";
    Tracker d = make_tracker("D");
    std::cout << "  d.name = " << d.name() << "\n\n";

    std::cout << "=== 5. 移动赋值 ===\n";
    d = std::move(b);
    std::cout << "  b.name = " << b.name() << "\n";
    std::cout << "  d.name = " << d.name() << "\n\n";

    std::cout << "=== 6. 程序结束，析构顺序 ===\n";
    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o rvalue_demo rvalue_demo.cpp
./rvalue_demo
```

预期输出类似：

```text
=== 1. 基本构造 ===
  [A] 构造

=== 2. 拷贝构造 ===
  [A_copy] 拷贝构造
  a.name = A
  b.name = A_copy

=== 3. 移动构造（显式 std::move）===
  [A] 移动构造
  a.name = (moved-from)
  c.name = A

=== 4. 返回临时对象 ===
  [D] 构造
  d.name = D

=== 5. 移动赋值 ===
  [A_copy] 移动赋值
  b.name = (moved-from)
  d.name = A_copy

=== 6. 程序结束，析构顺序 ===
  [A_copy] 析构
  [(moved-from)] 析构
  [A] 析构
  [(moved-from)] 析构
```

我们来逐步分析这个输出。第 2 步中，`Tracker b = a;` 触发了拷贝构造——`a` 是左值，只能匹配拷贝构造函数，`b` 的名字变成了 `"A_copy"`。第 3 步中，`std::move(a)` 把 `a` 转成右值引用，匹配移动构造函数——`c` 的名字变成了 `"A"`（从 `a` 那里偷来的），而 `a` 的名字变成了 `"(moved-from)"`。

第 4 步是最有意思的。`make_tracker("D")` 在函数内部构造了一个 `Tracker("D")`，然后返回。注意输出里只有一次构造——没有拷贝，也没有移动。这是因为 C++17 的**保证消除**（guaranteed copy elision）：返回 prvalue 时，编译器直接在调用者的空间里构造对象，连移动都省了。这就是为什么我们在下一篇文章里要专门讲 RVO 和 NRVO。

第 5 步的移动赋值也值得注意。`d = std::move(b);` 把 `b` 的资源转移给 `d`——`d` 原来的名字 `"D"` 被覆盖成了 `"A_copy"`，`b` 变成了 `"(moved-from)"`。这个过程中 `d` 原来的资源（那块存着 `"D"` 的内存）被正确释放了，因为移动赋值运算符在覆盖之前要确保旧资源被清理。

## 小结

这一篇我们把右值引用的地基打好了。C++ 的值类别体系分为 lvalue、xvalue、prvalue 三类，它们按"有身份"和"可移动"两个维度交叉组合。右值引用 `T&&` 只能绑定到右值（prvalue 或 xvalue）上，这保证了我们不会意外偷走一个还在使用的左值的资源。`std::move` 本质上是一个 `static_cast<T&&>`，它不做任何移动操作——真正移动资源的是移动构造函数和移动赋值运算符。临时对象绑定到右值引用时，生命周期会被延长到引用的作用域结束。

这些概念看起来抽象，但它们构成了整个移动语义大厦的根基。下一篇我们就要在这个地基上盖楼——实现移动构造函数和移动赋值运算符，真正地完成零拷贝的资源转移。
