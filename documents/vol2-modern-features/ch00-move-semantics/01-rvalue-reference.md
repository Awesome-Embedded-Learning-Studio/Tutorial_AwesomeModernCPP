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

欢迎来到现代C++！尽管现代C++这个词一般说的是C++11以及之后的C++。但是特性变更足以拿出来好好说说了。

> 一些朋友会有争议，笔者自己交流的时候就被喷过C++11还算现代？嗯。。。好像也没问题，从笔者落笔文章的2026年开始，这些特性就已经存在10多年了——确实在时间上不算现代。但是它相对于C++98这些老古董C++而言，已经有了相当大的特性变更。这就是这一卷被单独拿出来的原因！


我最开始接触C++的时候，看那本《Effective Modern C++》，就总觉得并不是很理解“右值引用”这个概念。总觉得吧，"右值引用"这四个字散发着一种不可名状的学术味——`T&&` 是什么？左值右值到底怎么分？`std::move` 是不是真的在"移动"什么东西？每次看到别人的代码里出现 `std::move`，总是似懂非懂地抄过来，祈祷编译通过就好。现在笔者跑来写东西，就要把这些内容搞明白，至少，不犯很低级的错误！

> 依旧碎碎念：笔者挺害怕C++语言律师的。所以每一次提笔写点东西都怕被这批大佬嘲讽。不过严谨从来是好的——写C++不严谨，小心睡觉的时候被内存爆炸摇起来狠狠被你的ld艹一顿。但是作为教学，没必要一上来就死扣细节。小心一叶障目。

## 从一个让人血压升高的问题说起

现在有一个场景，字符串处理，大家都知道对吧。不少人会觉得——欸，std::string有的时候太重了，希望来一个字符串只读视图。const char*不错，但是NULL Terminate太搞了（指定一个\0作为节点约束有时候很不靠谱），那好，我们自己做一个StringWrapper！

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

在没有移动语义且编译器未做 NRVO（命名返回值优化）的情况下，`build_greeting` 返回 `result` 时会触发拷贝构造——分配一块新内存，把 `result` 里的字符串逐字节复制过去。然后 `result` 自己析构，释放掉原来那块内存。当然，现实中 C++03 时代的 GCC 和 MSVC 已经普遍把 NRVO 作为编译器扩展实现了，所以这段分析讨论的是"NRVO 不生效"时的最坏情况。也就是说，我们花了一次内存分配加一次逐字节拷贝，只为了把一个马上就要销毁的对象里的数据"搬到"另一个位置上。如果字符串很长，比如一个几 KB 的 JSON 文本，这种拷贝就显得格外浪费——源对象反正马上就要死了，数据留在那块内存里也是白搭，为什么不直接把内存的控制权接管过来？

这就是移动语义要解决的核心问题。而要理解移动语义，我们必须先理解 C++ 是如何对表达式进行分类的——也就是所谓的**值类别**（value category）。

## 值类别的全景图

在 C++11 之前，事情比较简单：

> 表达式要么是左值（lvalue），要么是右值（rvalue）。

就是这么简单。但是C++11来了，资源的概念所属权可以被移动之后。分类就变得复杂起来。

- 每个表达式恰好属于 **lvalue**、**xvalue**、**prvalue** 三者之一。
- 这三个类别又可以两两组合成更宽泛的类别：**glvalue**（generalized lvalue）= lvalue + xvalue，**rvalue** = xvalue + prvalue。

如果你觉得这个分类体系有点绕，别急，笔者一开始也绕了半天。我们可以从两个属性来理解它：**有身份**（has identity，指的是表达式有名字、能取地址）和 **可移动**（can be moved from，指的是表达式是临时的、可以被安全地"偷走"资源）。

有身份且不可移动的是 **lvalue**。

举个例子，普通变量 `int x = 10;` 里的 `x`，它有名字、有地址、生命周期还没有结束，你当然不能随便把它的资源偷走。有身份且可移动的是 **xvalue**（expiring value）——比如 `std::move(x)` 的结果，它告诉你"这个对象有身份，但它马上就要死了，你可以安全地偷走它的资源"。没有身份但可移动的是 **prvalue**（pure rvalue）——比如字面量 `42` 或者函数返回的临时对象，它本来就没有名字，你不需要担心偷了之后谁会再访问它。

我们来看一组具体的例子来把这三类区分清楚。

```cpp
int x = 10;            // x 是 lvalue
int&& r = std::move(x); // std::move(x) 是 xvalue
int y = x + 1;         // x + 1 是 prvalue
int z = 42;            // 42 是 prvalue
```

这里面 `x` 是最典型的 lvalue——有名字，有地址，`&x` 是合法表达式（你当然可以在栈上拿到这个变量的地址！）。`std::move(x)` 产生的是一个 xvalue，它和 `x` 指向同一块内存，但语义上标记为"即将过期"。`x + 1` 和 `42` 都是 prvalue——临时的、没有名字的值。

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

你可能会问：为什么不让右值引用也能绑定左值？好问题，我们知道移动语义就是表达所属权的转移。一个左值就是有自己独立地址，掌管自己资源的变量，天然的就跟“不掌管准备移走”的语义冲突。所以你根本就不会想让 `T&&` 可以绑定到任何东西上！如果这样，我们就无法说出"这个对象可以安全偷取"和"这个对象还在使用中"的能力——而这个区分恰恰是移动语义存在的根本理由。

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

这里有一个非常容易踩的坑：**对基本类型使用 `std::move` 在逻辑上不会带来任何性能收益**（出于对编译器优化的恐惧，我根本没法下定论）。`std::move(42)` 只是把 `int` 转换成 `int&&`，但 `int` 的"移动"和"拷贝"是同一回事——都是复制四个字节。移动语义的威力只体现在**管理了资源的类**上，比如持有动态内存、文件句柄、网络连接的类。

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

这里面的 `"[" + level + "] " + module + ": " + detail` 产生了大量的临时 `std::string` 对象——每做一次 `+` 都会创建一个新的临时字符串。在 C++03 的世界里，每一次 `+` 都会导致一次内存分配和一次数据拷贝。C++11 之后情况有所改善——如果 `operator+` 接受值参数并返回命名局部变量，编译器会在返回时自动触发**隐式移动**（implicit move），后续拼接环节传递的是移动后的临时对象，只转移内部指针而不复制字符数据。当然，C++17 的保证消除（guaranteed copy elision）更进一步，当 `operator+` 返回 prvalue 时，连移动构造都可以省掉。

更直接的收益来自函数返回。`build_log_message` 返回 `msg`，编译器在这里有两种优化手段：NRVO（命名返回值优化）可以直接消除这次拷贝，退一步说，即使 NRVO 没生效，C++11 也会自动把 `msg` 当作右值来处理（隐式移动），调用 `std::string` 的移动构造函数——只转移内部指针，不复制字符数据。

再来看一个容器元素转移的例子：

```cpp
std::vector<std::string> names;

std::string name = "Alice";
names.push_back(std::move(name));  // 移动：name 的内部数据转移到 vector 中
// name 现在处于有效但未指定的状态，不要再使用它

names.push_back("Bob");   // 先从 const char* 构造临时对象，再移动进 vector
```

第一个 `push_back` 使用了移动语义：`std::move(name)` 把 `name` 转成右值引用，vector 调用 `std::string` 的移动构造函数来构造新元素——代价是转移一个指针和两个 `size_t`，而不是复制整个字符串内容。第二个 `push_back("Bob")` 看起来好像"直接构造"，但实际发生的事情是：`"Bob"` 先通过 `std::string` 的 `const char*` 构造函数创建一个临时对象，然后这个临时对象作为右值传入 `push_back(T&&)` 重载，被移动构造进 vector 的存储空间里。也就是说，它比 `push_back(std::move(name))` 多了一步临时对象的构造，但依然只做了一次移动而没有发生深拷贝。如果你真的想跳过临时对象的构造，实现真正的就地构造，应该用 `emplace_back("Bob")`——它会直接在 vector 的存储空间里调用 `std::string` 的构造函数。

我们可以用带追踪的类来验证这一点：

```cpp
// push_back_inplace.cpp -- push_back vs emplace_back 行为对比
// GCC 15, -O0 -std=c++17
```

```bash
g++ -std=c++17 -O0 -o /tmp/push_back_inplace push_back_inplace.cpp && /tmp/push_back_inplace
```

```text
=== push_back(TrackedString("Bob")) ===
  [ctor from const char*] "Bob"
  [move ctor] "Bob"
  [dtor] ""
=== done ===

=== emplace_back("Alice") ===
  [ctor from const char*] "Alice"
=== done ===
```

输出很清楚：`push_back(TrackedString("Bob"))` 先构造了一个临时对象，然后移动进去，临时对象再析构——两步构造。而 `emplace_back("Alice")` 只有一行输出，它直接在 vector 的存储空间里就地构造，省掉了移动那一步。回到文章中 `std::string` 的场景，`push_back("Bob")` 的过程是一样的：`"Bob"` 先隐式转换成临时 `std::string`，再被移动进 vector。如果你追求极致的零开销，`emplace_back` 才是正确选择。

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
  [A] 析构
  [(moved-from)] 析构
  [(moved-from)] 析构
```

我们来逐步分析这个输出。第 2 步中，`Tracker b = a;` 触发了拷贝构造——`a` 是左值，只能匹配拷贝构造函数，`b` 的名字变成了 `"A_copy"`。第 3 步中，`std::move(a)` 把 `a` 转成右值引用，匹配移动构造函数——`c` 的名字变成了 `"A"`（从 `a` 那里偷来的），而 `a` 的名字变成了 `"(moved-from)"`。

第 4 步是最有意思的。`make_tracker("D")` 在函数内部构造了一个 `Tracker("D")`，然后返回。注意输出里只有一次构造——没有拷贝，也没有移动。这是因为 C++17 的**保证消除**（guaranteed copy elision）：返回 prvalue 时，编译器直接在调用者的空间里构造对象，连移动都省了。这就是为什么我们在下一篇文章里要专门讲 RVO 和 NRVO。

第 5 步的移动赋值也值得注意。`d = std::move(b);` 把 `b` 的资源转移给 `d`——`d` 原来的名字 `"D"` 被覆盖成了 `"A_copy"`，`b` 变成了 `"(moved-from)"`。这个过程中 `d` 原来的资源（那块存着 `"D"` 的内存）被正确释放了，因为移动赋值运算符在覆盖之前要确保旧资源被清理。

## 小结

这一篇我们把右值引用的地基打好了。C++ 的值类别体系分为 lvalue、xvalue、prvalue 三类，它们按"有身份"和"可移动"两个维度交叉组合。右值引用 `T&&` 只能绑定到右值（prvalue 或 xvalue）上，这保证了我们不会意外偷走一个还在使用的左值的资源。`std::move` 本质上是一个 `static_cast<T&&>`，它不做任何移动操作——真正移动资源的是移动构造函数和移动赋值运算符。临时对象绑定到右值引用时，生命周期会被延长到引用的作用域结束。

这些概念看起来抽象，但它们构成了整个移动语义大厦的根基。下一篇我们就要在这个地基上盖楼——实现移动构造函数和移动赋值运算符，真正地完成零拷贝的资源转移。
