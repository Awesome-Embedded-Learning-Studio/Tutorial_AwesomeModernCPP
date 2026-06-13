---
title: "移动操作、std::move 与拷贝消除"
description: "CppCon 2025 演讲笔记 —— 移动构造/赋值的完整实现、std::move 的真实含义、NRVO 与 C++17 强制拷贝消除、moved-from 状态"
conference: cppcon
conference_year: 2025
talk_title: "Back to Basics: Move Semantics"
speaker: "Ben Saks"
video_bilibili: "https://www.bilibili.com/video/BV1X54y1P7uM"
video_youtube: "https://www.youtube.com/watch?v=szU5b972F7E"
tags:
  - cpp-modern
  - host
  - beginner
difficulty: beginner
platform: host
cpp_standard: [11, 17, 20]
chapter: 4
order: 3
---

# 移动操作、std::move 与拷贝消除

:::tip
本文是 CppCon 2025 "Back to Basics: Move Semantics" 系列笔记的第三篇。前两篇分别讨论了拷贝开销与移动动机、左值右值与引用体系。本篇聚焦于实战层面的核心问题：怎么写移动构造和移动赋值、`std::move` 到底在做什么、以及 C++17 的拷贝消除如何改变游戏规则。
:::

说实话，我之前一直觉得自己"理解"移动语义——不就是偷指针嘛，有什么难的？直到有一天我在 code review 里看到同事写了一句 `return std::move(result);`，我顺嘴说了句"挺好，显式移动了"，然后被旁边的资深工程师一句话打脸：**"你确定这样写不会阻止 NRVO？"**

折腾了一晚上才搞明白——`return std::move(result)` 不仅不会帮你优化，反而会把编译器本可以零成本完成的返回值传递变成一次额外的移动构造。从那天起我才真正意识到，移动语义的魔鬼全在细节里。

这一篇我们就来把这些细节一个一个拆清楚。我们的实验环境是 Arch Linux WSL，GCC 16.1.1，编译选项 `-std=c++20`，如果你打算跟着跑代码，建议备好这个版本或者更新的编译器。

## 移动构造函数：偷指针的艺术

上一篇我们已经有了完整的 `MyString` 拷贝操作。现在给它加上移动构造函数。这个函数做的事情，用 Ben Saks 的话说，是一种**"破坏性拷贝"（destructive copy）**——我们把源对象的数据"偷"过来，然后让源对象进入一种无害的状态。

```cpp
class MyString
{
    std::size_t stored_length_;
    char* actual_str_;

public:
    // ... 之前的构造函数、析构函数、拷贝操作 ...

    // 移动构造函数
    MyString(MyString&& s) noexcept
        : stored_length_(s.stored_length_)
        , actual_str_(s.actual_str_)
    {
        s.actual_str_ = nullptr;
        s.stored_length_ = 0;
    }
};
```

我们来逐行拆解这段代码到底在做什么，因为每一行都有它存在的理由。

首先是参数类型 `MyString&& s`——这是一个右值引用。右值引用只能绑定到右值（临时对象、`std::move` 的结果等），这意味着只有当编译器确认"源对象即将消亡"时，才会调用这个构造函数。这就是移动语义安全性保障的第一层：编译器通过重载决议来帮你把关。

接下来是初始化列表。`stored_length_(s.stored_length_)` 把源对象的长度直接拿过来——`std::size_t` 是内置类型，所谓"拷贝"就是一次整数赋值，几乎零成本。`actual_str_(s.actual_str_)` 才是关键：我们把源对象的指针直接赋给新对象，新对象现在指向了源对象之前分配的那块堆内存。到目前为止，两个对象指向同一块内存——如果我们就这样结束，那就是 double delete，是未定义行为。

所以函数体里那两行才是灵魂。`s.actual_str_ = nullptr` 把源对象的指针置空，`s.stored_length_ = 0` 把长度归零。这样一来，源对象的析构函数执行 `delete[] actual_str_` 时，实际调用的是 `delete[] nullptr`——而标准明确规定<RefLink :id="1" preview="C++ Standard, [expr.delete] — deleting a null pointer has no effect" />，删除空指针是安全的无操作。

你可能注意到了，移动构造函数的参数 `s` 虽然是右值引用，但 `s` 的析构函数仍然会被调用。这是很多人忽略的一点：移动操作并不是"接管之后就不用管源对象了"。恰恰相反，源对象在移动完成之后还是一个完整、合法的对象——只不过它的内部状态被我们故意设成了"无害"的值。它仍然会被正常析构，只不过析构时什么也不会释放。

## 重载决议：编译器怎么选？

有了拷贝构造和移动构造两个版本之后，编译器面对初始化表达式时会怎么选？答案是根据实参的值类别来重载决议<RefLink :id="2" preview="C++ Standard, [over.match] — overload resolution selects the best viable function" />。

```cpp
MyString s1("hello");

// s1 是左值（有名字）→ 调用拷贝构造函数
MyString s2(s1);

// std::move(s1) 是右值 → 调用移动构造函数
MyString s3(std::move(s1));
```

第一行 `MyString s2(s1)` 中，`s1` 是一个左值——它有名字，你可以对它取地址。编译器看到实参是左值，去找能接受 `const MyString&` 的构造函数，命中拷贝构造。

第二行 `MyString s3(std::move(s1))` 中，`std::move(s1)` 的结果是右值引用，编译器去找能接受 `MyString&&` 的构造函数，命中移动构造。这就是为什么我们需要两种构造函数并存：拷贝构造处理"源对象还要继续用"的情况，移动构造处理"源对象反正要死了"的情况。

Ben Saks 在演讲里特别强调了一点：**右值引用本身并不执行移动**。它只是在类型系统层面给编译器提供了一个信号——"这个引用绑定到了一个右值上"。真正决定是拷贝还是移动的，是重载决议。如果我们的 `MyString` 没有移动构造函数，那 `std::move(s1)` 也只会触发拷贝构造——编译器会退而求其次，使用 `const MyString&` 版本，因为 `MyString&&` 可以被 `const MyString&` 接收。不会报错，但也不会移动。这一点后面还会再提到。

## 移动赋值运算符：老对象要先清理

移动构造处理的是"创建新对象"的场景，而移动赋值处理的是"覆盖已有对象"的场景。两者的核心逻辑很像，但移动赋值多了一步——要先清理目标对象的旧资源。

```cpp
MyString& operator=(MyString&& s) noexcept
{
    if (this != &s) {
        delete[] actual_str_;         // 第一步：清理自己的旧资源
        stored_length_ = s.stored_length_;
        actual_str_ = s.actual_str_;  // 第二步：偷源对象的资源
        s.actual_str_ = nullptr;      // 第三步：置空源对象
        s.stored_length_ = 0;
    }
    return *this;
}
```

这个顺序很重要。我们先 `delete[] actual_str_` 释放自己之前的堆内存，然后再接管源对象的指针。如果我们反过来——先赋值再 delete——那就把源对象给我们的指针给删了，这是一个典型的 use-after-free。

自赋值检查 `if (this != &s)` 在移动赋值中同样重要。虽然 `s` 是右值引用，理论上不应该有人写 `x = std::move(x)` 这种代码，但语言层面并不禁止，而且有时候模板实例化后可能会产生这种效果。没有自赋值检查的话，`delete[] actual_str_` 会把我们自己的内存释放掉，然后 `actual_str_ = s.actual_str_` 把一个悬空指针赋回给自己——直接炸。

注意返回类型是 `MyString&`——左值引用，不是右值引用。这是因为赋值运算符的目标（`=` 左边的对象）始终是左值。无论你用不用 `std::move`，赋值的接收端一定是"一个有名字、有地址的对象"。

另外，这个实现在异常安全方面是安全的——`MyString` 的数据成员只有内置类型（`std::size_t` 和 `char*`），对这些类型的操作不会抛异常。这也是为什么我给它标了 `noexcept`。如果你的类有更复杂的数据成员（比如另一个 `std::string`），那就得仔细考虑异常安全了。

## std::move：C++ 中被误解最深的函数

`std::move` 这个名字起得实在太坑了。我第一次看到它的时候，理所当然地以为它"执行移动操作"——毕竟它叫 "move" 嘛。但事实是，**`std::move` 本身不移动任何东西**。

它的真实身份是一个 `static_cast` 到右值引用的类型转换。标准库的实现大致等价于：

```cpp
template<typename T>
constexpr typename std::remove_reference<T>::type&& move(T&& t) noexcept
{
    return static_cast<typename std::remove_reference<T>::type&&>(t);
}
```

去掉 `remove_reference` 的模板体操不看，核心就是 `static_cast<T&&>(t)`。它把传入的参数转换成右值引用然后返回。仅此而已。它不生成任何移动代码，不调用任何移动构造函数，不修改任何对象的状态。

Ben Saks 在演讲里说了一句大实话：**如果我们能重新来过，大概会把它叫成 `make_movable` 或 `as_rvalue`**。这个名字至少不会让人误以为它在执行移动。

### 为什么需要 std::move：swap 中的命名陷阱

那既然 `std::move` 不移动，我们为什么还需要它？来看 `swap` 函数。这是最能说明问题的场景。

```cpp
template<typename T>
void swap(T& x, T& y)
{
    T temp(x);              // (1)
    x = y;                  // (2)
    y = temp;               // (3)
}
```

这个 C++03 风格的 `swap` 执行三次拷贝。我们当然想把它改成移动版本——毕竟我们前两篇文章一直在说移动比拷贝快得多。但是问题来了：函数体内的 `x`、`y`、`temp` 全都是左值。它们都有名字，你可以对它们取地址，它们的生命周期跨越多条语句。编译器不可能自动把它们当成右值来处理——万一你在第三行之后还要用 `temp` 呢？

C++ 有一个一般性的规则：**如果一个东西有名字，它就是左值**。只有没有名字的东西（比如临时对象、字面量、函数返回的按值结果）才能是右值。这个规则非常合理——编译器必须保守，它不能假设 `temp` 在下一行不被使用。

所以我们需要显式地告诉编译器："我知道 `temp` 之后不会再被使用了，请把它当作右值来处理。"这正是 `std::move` 的用途：

```cpp
template<typename T>
void move_swap(T& x, T& y)
{
    T temp(std::move(x));    // 移动构造 temp
    x = std::move(y);        // 移动赋值 x
    y = std::move(temp);     // 移动赋值 y
}
```

每一个 `std::move` 都是在向编译器传递一个信息：**"这个地方，我确认可以安全地从该对象移动资源。"** 编译器拿到这个信息后，才会在重载决议中选择移动版本。

### std::move 不保证移动

还有一个容易忽略的陷阱：`std::move` 并不保证一定会发生移动。如果一个类型只有拷贝操作而没有移动操作，那 `std::move` 的结果会退化为拷贝。

```cpp
struct CopyOnly
{
    CopyOnly() = default;
    CopyOnly(const CopyOnly&) { std::cout << "copy\n"; }
    // 没有移动构造函数！
};

CopyOnly a;
CopyOnly b(std::move(a));  // 输出 "copy" —— 退化为拷贝构造
```

这里 `std::move(a)` 把 `a` 转成了右值引用，但 `CopyOnly` 没有接受右值引用的构造函数。编译器退而求其次，使用 `const CopyOnly&` 版本的拷贝构造函数（因为 `CopyOnly&&` 可以绑定到 `const CopyOnly&`）。不会报错，只是你期望的"移动"变成了"拷贝"——而且是悄无声息的。

## 右值引用参数的命名悖论

这是移动语义中最让人困惑的一点，也是 Ben Saks 花了不少时间强调的内容。

当我们写一个接收右值引用参数的函数时，参数在函数内部**被当作左值**来处理：

```cpp
void process(MyString&& s)
{
    // s 有名字 → s 是左值
    MyString copy(s);             // 调用拷贝构造！不是移动构造！
    MyString moved(std::move(s)); // 这才调用移动构造
}
```

从函数外部的视角来看，传入的实参是一个右值（比如 `process(std::move(x))` 或者 `process(MyString("temp"))`）。但一旦进入函数体，`s` 就是一个有名字的变量了——它跨越多条语句而存在，编译器不可能假设它只使用一次。所以"有名字就是左值"的规则依然生效。

这导致一个实用的后果：**在函数内部，如果你要从右值引用参数移动资源，你必须显式使用 `std::move`**。而且一旦你移动了，这个参数在后续代码中的值就不可预测了——这是下一个小节要讨论的 moved-from 状态。

## 隐式可移动的返回表达式

好消息是，"有名字就是左值"这条规则有一个重要的例外——`return` 语句。

```cpp
MyString make_greeting()
{
    MyString temp("hello world");
    // ... 对 temp 做一些操作 ...
    return temp;  // 不需要 std::move！
}
```

在这段代码中，`temp` 虽然有名字（按理说是左值），但 `return temp;` 是函数中对 `temp` 的最后一次使用。编译器知道 `temp` 的生命周期在函数返回后即刻结束，所以标准允许它把 `temp` 当作隐式可移动的对象（implicitly movable entity）<RefLink :id="3" preview="C++ Standard, [class.copy.elision] — NRVO and implicit move" />来处理。

这意味着你**不需要**写 `return std::move(temp);`。直接 `return temp;` 就够了——编译器会自动选择移动构造（或者更好的选择，直接消除这次构造，下面马上讲到）。

## NRVO：比移动更牛的优化

说"隐式可移动"其实还没讲到头。编译器实际上可以做得比移动更好——它可以让返回值**零成本**地到达调用方，连移动都不需要。这就是所谓的**命名返回值优化（Named Return Value Optimization, NRVO）**。

```cpp
MyString make_greeting()
{
    MyString temp("hello world");
    return temp;
}

MyString s = make_greeting();
```

在没有 NRVO 的世界里，执行流程是这样的：先在 `make_greeting` 的栈帧上构造 `temp`，然后在 `s` 的位置构造一个临时对象（通过移动或拷贝），然后 `temp` 析构，然后临时对象再移动或拷贝到 `s`，然后临时对象析构。听着就很浪费。

NRVO 的思路非常巧妙：编译器在生成代码时，直接把 `temp` 构造在 `s` 的位置上。不是先构造再拷贝，而是从一开始就放在正确的位置。`temp` 就是 `s`，它们共享同一块内存。函数返回时，不需要任何拷贝或移动——对象本来就在该在的地方。

从 C++17 开始，这种优化在某些场景下变成了**强制性**的<RefLink :id="4" preview="C++ Standard, [class.copy.elision] — mandatory elision in certain contexts" />——编译器必须消除拷贝，而不是"可以消除但也可以不消除"。这不是一个可选的优化，而是语言的定义行为。历史原因让它还叫"优化"，但实际上它已经是一种保证了。

关于 NRVO 和 RVO 的完整技术细节，我们之前在 vol2 有专门的文章讲解：[RVO 与 NRVO：编译器的返回值优化](../../../vol2-modern-features/ch00-move-semantics/03-rvo-nrvo.md)。

## 千万别对返回值用 std::move

这大概是我见过的移动语义相关最常见的错误。前面说了 `return temp;` 是隐式可移动的，编译器要么做 NRVO（零成本），要么自动退回到移动构造（一次指针赋值的成本）。那有人会想：既然 `std::move` 是"请求移动"，那 `return std::move(temp);` 岂不是更明确、更安全？

**完全相反。**

```cpp
// 正确写法：允许 NRVO
MyString make_good()
{
    MyString temp("good");
    return temp;
}

// 错误写法：阻止 NRVO！
MyString make_bad()
{
    MyString temp("bad");
    return std::move(temp);  // 反而更慢！
}
```

原因在于 NRVO 的触发条件<RefLink :id="5" preview="C++ Standard, [class.copy.elision] — the return expression must be the name of a local variable" />：`return` 表达式必须是一个局部对象的名字。当你写 `return std::move(temp);` 时，返回表达式不再是 `temp` 这个名字了——它是 `std::move(temp)`，一个函数调用表达式。编译器无法对这个表达式执行 NRVO，只能退而求其次选择移动构造。

换句话说，`return std::move(temp);` 强制编译器走移动构造路径，而 `return temp;` 让编译器有机会走 NRVO 路径（零成本）。这就是为什么 Ben Saks 在演讲里反复强调：**不要对返回值使用 `std::move`**。

我们可以用 `-fno-elide-constructors` 这个编译器标志来对比两者的差异。这个标志会关闭 GCC 的拷贝消除优化，让我们看到"如果没有 NRVO"的世界是什么样的。

先看关闭消除之后 `return temp;` 的行为——它会退回到移动构造，因为 `temp` 是隐式可移动的。而 `return std::move(temp);` 同样是移动构造——两者在关闭消除后没有区别。但一旦开启消除（也就是默认行为），`return temp;` 就变成了零操作，而 `return std::move(temp);` 仍然是移动构造。差距就在这里。

我用 GCC 16.1.1 实测了一下，给 `MyString` 的各种构造函数加上打印日志后，对比结果是这样的：

```bash
# 默认开启 NRVO
$ g++ -std=c++20 -O2 test.cpp && ./a.out
=== return temp; (NRVO) ===
  构造: "hello"          # 只有这一次构造，没有移动，没有拷贝

=== return std::move(temp); ===
  构造: "hello"
  移动构造: "hello"       # 多了一次移动构造！
  析构: "(null)"
```

你看，`return std::move(temp);` 明确多了一次移动构造。对于 `MyString` 这种只有指针和整数的类，移动构造的代价很低（一次指针赋值），但对于更复杂的类（比如含多个动态容器的对象），这个额外移动的代价就不能忽略了。

```bash
# 关闭 NRVO 后对比
$ g++ -std=c++20 -O2 -fno-elide-constructors test.cpp && ./a.out
=== return temp; ===
  构造: "hello"
  移动构造: "hello"       # 没有 NRVO，退回到移动构造
  析构: "(null)"

=== return std::move(temp); ===
  构造: "hello"
  移动构造: "hello"       # 同样是移动构造
  析构: "(null)"
```

关闭 NRVO 后两者确实行为一致——都是一次移动构造。但这恰恰说明了 `return std::move(temp);` 在默认情况下白白浪费了 NRVO 的机会。

:::warning C++20/C++23 进一步扩大了「隐式可移动」的范围
本节讲的「别对返回值用 `std::move`」这条规则，在**所有标准版本（C++11 到 C++26）都成立**，是绝对安全的建议。但「隐式可移动」这套机制本身在后续标准里是被持续加强的，值得知道一下：C++11 引入了最初的隐式移动（return 一个局部对象时编译器可按移动处理）；C++20（提案 P1825「More implicit moves」）把「隐式可移动实体」的范围扩大了——比如绑定到右值引用的局部变量、以及 `throw` 一个局部对象，也纳入了隐式移动；C++23（提案 P2266）又做了进一步精细化，让返回值在某些场景下被当作 xvalue 处理，覆盖更多构造路径。

但无论这些扩展怎么变，**「return 局部对象时不要写 `std::move`」这条铁律从未改变**——P1825/P2266 扩大的是「编译器能自动移动」的范围，而 `std::move` 反而会破坏 NRVO 的触发条件。结论照旧：写 `return temp;`，把 NRVO 还是隐式移动的选择权交给编译器。
:::

## moved-from 状态：有效但不可知

移动操作完成之后，源对象处于一种标准称为**"有效但未指定的状态"（valid but unspecified state）**<RefLink :id="6" preview="C++ Standard, [lib.types.movedfrom] — moved-from objects are in a valid but unspecified state" />的状态。这几个字值得逐个拆解。

"有效"意味着：不会内存泄漏、不会资源泄漏、不会触发未定义行为。你可以安全地让这个对象析构——它的析构函数会正常执行，不会 double free，不会 crash。对于我们的 `MyString` 来说，移动后 `actual_str_` 被置成了 `nullptr`，`stored_length_` 变成了 0，所以析构时 `delete[] nullptr` 什么也不做。

"未指定"意味着：你不能对移动后对象持有的值做任何假设。标准没有规定移动后的 `std::string` 一定是空字符串，也没有规定移动后的 `std::vector` 一定是空的。不同的标准库实现可能有不同的行为。我们自己的 `MyString` 在移动后 `c_str()` 返回 `"(null)"`（这是我们自己的安全兜底），但 `std::string` 移动后可能返回空串，也可能返回原始值——你不能依赖它。

```cpp
MyString a("hello");
MyString b(std::move(a));

// 安全操作：
// 1. 析构 —— 永远安全
// 2. 赋新值 —— 永远安全
a = MyString("new value");  // OK

// 不安全操作：
// 1. 假设 a 仍持有 "hello"
// 2. 假设 a.size() 是 0
// 3. 假设 a.c_str() 返回空串
// 这些假设在某些实现上可能碰巧成立，但标准不保证
```

:::warning moved-from 对象的使用限制
Ben Saks 在 Q&A 环节被问到"移动后的对象能不能继续用"，他的回答非常干脆：**移动后，你对源对象唯一应该做的事情就是给它赋一个新值，或者让它析构**。任何其他操作（读取值、比较、传递给其他函数）都是在赌博——你可能赢了（碰巧实现给了你一个可预测的值），也可能输了（实现变了或者换了个标准库）。不要赌。

不要混淆"有效"和"有用"——moved-from 的对象是一个合法的对象，但不是一个内容确定的对象。如果你需要一个空对象，显式创建一个；如果你需要某个特定值，显式赋值。不要指望移动操作帮你做这些。
:::

## noexcept 的重要性：vector 扩容的隐藏陷阱

最后来说一个在实际工程中经常被忽略但影响巨大的问题：**移动构造函数应该是 `noexcept` 的**。

为什么？来看 `std::vector` 扩容的场景。当 `vector` 的容量不够时，它需要分配一块更大的内存，然后把旧元素转移到新内存中。如果元素的移动构造函数是 `noexcept` 的，`vector` 就会使用移动来转移——非常快。如果移动构造函数不是 `noexcept` 的，`vector` 会退回到拷贝<RefLink :id="7" preview="C++ Standard, [vector.modifiers] — if move ctor is not noexcept, vector uses copy during reallocation" />。

这是因为 `vector` 要提供强异常安全保证（strong exception safety guarantee）：如果扩容过程中抛了异常，`vector` 的状态必须回滚到扩容之前。如果用的是移动，一旦中途抛异常，已经被移动的元素没法恢复（它们的资源已经被偷走了）。如果用的是拷贝，原始数据还在，可以安全回滚。

我们写个简单的测试来验证这个行为：

```cpp
#include <iostream>
#include <vector>
#include <cstring>

class StringNoNoexcept
{
    std::size_t len_;
    char* str_;

public:
    StringNoNoexcept(const char* s)
        : len_(std::strlen(s))
        , str_(new char[len_ + 1])
    {
        std::memcpy(str_, s, len_ + 1);
        std::cout << "  ctor: " << str_ << "\n";
    }

    ~StringNoNoexcept()
    {
        delete[] str_;
    }

    StringNoNoexcept(const StringNoNoexcept& o)
        : len_(o.len_)
        , str_(new char[o.len_ + 1])
    {
        std::memcpy(str_, o.str_, len_ + 1);
        std::cout << "  COPY ctor: " << str_ << "\n";
    }

    // 没有 noexcept！
    StringNoNoexcept(StringNoNoexcept&& o)
        : len_(o.len_)
        , str_(o.str_)
    {
        o.str_ = nullptr;
        o.len_ = 0;
        std::cout << "  MOVE ctor: " << (str_ ? str_ : "(null)") << "\n";
    }

    const char* c_str() const { return str_ ? str_ : "(null)"; }
};

int main()
{
    std::vector<StringNoNoexcept> vec;
    vec.reserve(2);

    std::cout << "=== push 3 elements (triggers reallocation) ===\n";
    vec.emplace_back("AAA");
    vec.emplace_back("BBB");
    vec.emplace_back("CCC");  // 这里触发扩容

    std::cout << "\n=== final contents ===\n";
    for (const auto& s : vec) {
        std::cout << "  " << s.c_str() << "\n";
    }
    return 0;
}
```

编译运行后你会看到这样的输出（GCC 16.1.1，`-std=c++20 -O2`）：

```bash
$ g++ -std=c++20 -O2 test_noexcept.cpp && ./a.out
=== push 3 elements (triggers reallocation) ===
  ctor: AAA
  ctor: BBB
  ctor: CCC
  COPY ctor: AAA    # 扩容时用的是拷贝！不是移动！
  COPY ctor: BBB
```

看到了吗？当第三个元素触发扩容时，`vector` 把前两个元素**拷贝**到了新内存——尽管我们明明实现了移动构造函数。原因就是我们的移动构造函数没有标 `noexcept`。

现在把移动构造函数加上 `noexcept`：

```cpp
StringNoNoexcept(StringNoNoexcept&& o) noexcept  // 加上 noexcept
```

重新编译运行：

```bash
$ g++ -std=c++20 -O2 test_noexcept.cpp && ./a.out
=== push 3 elements (triggers reallocation) ===
  ctor: AAA
  ctor: BBB
  ctor: CCC
  MOVE ctor: AAA    # 现在用移动了！
  MOVE ctor: BBB
```

一个 `noexcept` 关键字的差异，直接决定了 `vector` 扩容时是拷贝还是移动。对于一个持有动态内存的类来说，在大量数据场景下，这个差异可能意味着数量级的性能差距。

这是一个真正的生产级陷阱。很多人写了移动构造函数但忘了加 `noexcept`，然后在性能测试中困惑于"为什么移动语义没有生效"。答案往往就在这两个字上。

## 完整的 MyString：五巨头齐聚

把本篇和前两篇的内容合在一起，我们得到了一个完整的、符合 Rule of Five 的 `MyString` 实现：

```cpp
#include <cstring>
#include <utility>

class MyString
{
    std::size_t stored_length_;
    char* actual_str_;

public:
    // 构造函数
    explicit MyString(const char* s = "")
        : stored_length_(std::strlen(s))
        , actual_str_(new char[stored_length_ + 1])
    {
        std::memcpy(actual_str_, s, stored_length_ + 1);
    }

    // 析构函数
    ~MyString()
    {
        delete[] actual_str_;
    }

    // 拷贝构造函数
    MyString(const MyString& other)
        : stored_length_(other.stored_length_)
        , actual_str_(new char[other.stored_length_ + 1])
    {
        std::memcpy(actual_str_, other.actual_str_, stored_length_ + 1);
    }

    // 移动构造函数 —— noexcept！
    MyString(MyString&& s) noexcept
        : stored_length_(s.stored_length_)
        , actual_str_(s.actual_str_)
    {
        s.actual_str_ = nullptr;
        s.stored_length_ = 0;
    }

    // 拷贝赋值运算符
    MyString& operator=(const MyString& other)
    {
        if (this != &other) {
            delete[] actual_str_;
            stored_length_ = other.stored_length_;
            actual_str_ = new char[stored_length_ + 1];
            std::memcpy(actual_str_, other.actual_str_, stored_length_ + 1);
        }
        return *this;
    }

    // 移动赋值运算符 —— noexcept！
    MyString& operator=(MyString&& s) noexcept
    {
        if (this != &s) {
            delete[] actual_str_;
            stored_length_ = s.stored_length_;
            actual_str_ = s.actual_str_;
            s.actual_str_ = nullptr;
            s.stored_length_ = 0;
        }
        return *this;
    }

    const char* c_str() const { return actual_str_ ? actual_str_ : "(null)"; }
    std::size_t size() const { return stored_length_; }
};
```

五个特殊成员函数——析构函数、拷贝构造、拷贝赋值、移动构造、移动赋值——全部到齐。这就是所谓的 Rule of Five：如果你需要自定义其中任何一个，那你大概率需要自定义全部五个。编译器生成的默认版本对持有裸指针的类来说是不安全的。

## 到这里搞清楚了什么

三篇文章走下来，我们从 `swap` 的三次深拷贝出发，经过左值右值的值类别体系，最终在这篇把移动操作的全部实现细节拆解清楚了。让我用一个简洁的清单来回顾本篇的核心要点。

移动构造函数的核心是"破坏性拷贝"——偷走源对象的资源指针，然后把源对象置成无害状态。重载决议自动选择拷贝还是移动，你不需要在调用点做额外判断。`std::move` 不移动任何东西，它只是一个到右值引用的类型转换，使得重载决议能够选择移动版本。右值引用参数在函数内部是左值——因为它有名字——所以你仍然需要 `std::move` 才能从中移动。`return` 语句是"有名字就是左值"规则的例外，编译器会自动识别隐式可移动的返回表达式。NRVO 可以让返回值零成本到达调用方——而 `return std::move(temp)` 会阻止 NRVO，千万别这么写。移动后的对象处于"有效但未指定"的状态，唯一安全的操作是赋新值或析构。移动构造函数一定要标 `noexcept`——否则 `std::vector` 扩容时会退回拷贝，性能差距可能非常大。

如果你想继续深入移动语义的更多应用场景——完美转发、万能引用、引用折叠——可以看 vol2 的 [完美转发：保持值类别的精确传递](../../../vol2-modern-features/ch00-move-semantics/04-perfect-forwarding.md)。移动语义和完美转发搭配使用，才是现代 C++ 模板编程的完整基础。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [expr.delete]"
    :year="2020"
    chapter="Deleting a null pointer is a safe no-op"
  />
  <ReferenceItem
    :id="2"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [over.match]"
    :year="2020"
    chapter="Overload resolution selects copy or move based on value category"
  />
  <ReferenceItem
    :id="3"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [class.copy.elision]"
    :year="2020"
    chapter="Implicitly movable entities in return statements"
  />
  <ReferenceItem
    :id="4"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [class.copy.elision]"
    :year="2020"
    chapter="Mandatory copy elision since C++17"
  />
  <ReferenceItem
    :id="5"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [class.copy.elision]"
    :year="2020"
    chapter="NRVO requires the return expression to be a local variable name"
  />
  <ReferenceItem
    :id="6"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [lib.types.movedfrom]"
    :year="2020"
    chapter="标准库类型的 moved-from 对象处于有效但未指定状态"
  />
  <ReferenceItem
    :id="7"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [vector.modifiers]"
    :year="2020"
    chapter="vector uses copy if move ctor is not noexcept"
  />
</ReferenceCard>
