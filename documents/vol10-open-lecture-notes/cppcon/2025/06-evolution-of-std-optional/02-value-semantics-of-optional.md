---
title: std::optional 的值语义底子
description: CppCon 2025 笔记 —— 在啃 optional<T&> 之前先把值版本说透：拥有所有权、值语义、T 加一个状态、C++26 当 range 用、以及默认参数背后的重载集噩梦
chapter: 6
order: 2
conference: cppcon
conference_year: 2025
talk_title: 'The Evolution of std::optional: From Boost to C++26'
speaker: Steve Downey
cpp_standard: [17, 23, 26]
difficulty: intermediate
platform: host
reading_time_minutes: 11
tags:
  - cpp-modern
  - host
  - intermediate
  - optional
prerequisites:
  - "为什么 optional 引用折腾了二十年"
related:
  - "optional：把『可能没有』做成类型"
  - "为什么 optional 引用折腾了二十年"
---

# std::optional 的值语义底子

[上一篇](./01-why-optional-reference-took-20-years.md)咱们讲完 `optional<T&>` 为什么难、为什么最终落地为指针。但在啃这个硬骨头之前，笔者觉得有必要先把普通的 `optional<T>` 说透。因为一旦 `T` 换成引用，您会发现 `optional<T>` 的几乎每一条前提都被推翻了。得先知道"正常情况"长什么样，才能体会"引用情况"怪在哪。

## 它到底是个什么类型

`optional<T>` 首先是个拥有所有权的类型，内部真的存放了一个 `T` 对象，而且是值语义的。这意味着您可以拷贝它、移动它，底层 `T` 允许的操作它都允许。它没有什么代理行为，就是个老老实实装着东西（或者没装东西）的值类型。

它和指针的 nullable 行为有几分像，都表达"可能没有值"。指针靠空指针来做到这一点，地址零在正常情况下不是有效地址，等于指针类型凭空多出一个带外值来表示"这里什么都没有"。`optional<T>` 干的是同一件事，在代数上就是 `T` 加上一个额外状态。您可以把它理解成 `variant<T, monostate>`，`monostate` 扮演那个"什么都没有"的状态。实际实现不会真用 `variant`（那样太重），但记住这个等价关系有用，因为后来关于 optional 参数推导、expected 设计的大量讨论，本质上都是在替咱们真正想用代数数据类型做的事情铺路。

## 实测：值语义到底"值"在哪

口说无凭，跑一下。写一个会打印自己每次构造、拷贝、析构的 `Tracer`，塞进 optional，看它什么时候真构造、什么时候析构、拷贝时是不是各自独立。

```cpp
// tracer.cpp
#include <iostream>
#include <optional>

struct Tracer {
    Tracer()                   { std::cout << "Tracer()\n"; }
    ~Tracer()                  { std::cout << "~Tracer()\n"; }
    Tracer(const Tracer&)      { std::cout << "copy ctor\n"; }
    Tracer(Tracer&&) noexcept  { std::cout << "move ctor\n"; }
};

int main() {
    std::optional<Tracer> a;
    std::cout << "a has_value=" << a.has_value() << "\n";

    a.emplace();                                     // 这里才真正构造
    std::optional<Tracer> b = a;                     // 拷贝构造
    std::cout << "b has_value=" << b.has_value() << "\n";

    a.reset();                                       // 析构 a 里的对象
    std::cout << "after reset a has_value=" << a.has_value()
              << " b has_value=" << b.has_value() << "\n";
}
```

`-std=c++17` 就够，跑出来：

```bash
$ g++ -std=c++17 tracer.cpp -o tracer && ./tracer
a has_value=0
Tracer()
copy ctor
b has_value=1
~Tracer()
after reset a has_value=0 b has_value=1
~Tracer()
```

读一下这个输出。声明 `optional<Tracer> a` 的时候，`Tracer()` 没被调用，`has_value` 是 0。直到 `emplace()` 才真正构造出对象。拷贝 `b = a` 走的是 `Tracer` 的拷贝构造。`a.reset()` 析构了 `a` 里的对象，但 `b` 完全不受影响，它有自己那一份。这就是值语义的所有权行为，非常干净。

## 一个让笔者深有体会的用例：读配置

Steve Downey 提到读配置文件的场景，笔者深有体会。以前写配置读取，某个配置项可能不存在，要么用 `map::find` 查迭代器再判 end，要么返回一个指针（nullptr 表示不存在），要么搞个 `bool` 加输出参数的组合。这些写法的问题在于，"这个值可能不存在"这个信息太容易丢。你可能五层函数调用之后，忘了检查那个指针是不是 null。

换成 optional 之后，类型系统本身就在盯着您：这个值可能没有，您必须处理。笔者之前写的一段业务代码，配置项嵌套了三四层，用指针的方式每次都要回头翻"我到底检查了没有"，改成 optional 之后，编译器直接逼着在用之前做判断。那种安全感是完全不一样的。这部分 vol3 有一篇专门的 [optional 深入](../../../../vol3-standard-library/error-utils/61-optional.md)，讲得更细，您可以对照着看。

## C++26：optional 当 range 用

C++26 给 optional 加了 `begin` 和 `end`，让它能当 range 用。提案是 P3168，特性测试宏 `__cpp_lib_optional_range_support` 在 GCC 16.1.1 上是 `202406L`。笔者刚看到这个提案的时候心里想的是"就为遍历一个值？有必要吗"。后来仔细想想自己的代码，改观了。

您想想这种模式：

```cpp
std::optional<User> maybe_user = find_user(id);
if (maybe_user) {
    // 接下来几十行都在操作 maybe_user.value()
    // 中间可能又嵌套了判断
    // 你得一直记住"我在 if 里面，它是安全的"
}
```

当 if 的 body 很长，您在中间某一行读到 `maybe_user.value()`，得往上翻才能确认外面有 if 保护着。C++26 改成这样写：

```cpp
for (auto& user : maybe_user) {
    // 进到这里，user 就是 User&，不是 optional
    // 几十行里随便用 user，它就是一个确定的值
}
```

原理很简单。optional 是 engaged 的，`begin()` 返回指向内部对象的指针，`end()` 返回 `begin() + 1`，循环执行一次；disengaged 的，`begin()` 等于 `end()`，循环零次。它不是 container（container 有一大堆要求），只是恰好提供了 `begin` 和 `end` 的 range。跑一下验证：

```cpp
// opt_as_range.cpp
#include <iostream>
#include <optional>

int main() {
    std::optional<int> engaged = 42;
    std::optional<int> empty;

    int count = 0, sum = 0;
    for (auto&& x : engaged) { count++; sum += x; }
    for (auto&& x : empty)    { count++; sum += x; }

    std::cout << "count=" << count << " sum=" << sum << "\n";
}
```

```bash
$ g++ -std=c++26 opt_as_range.cpp -o opt_as_range && ./opt_as_range
count=1 sum=42
```

engaged 的 optional 迭代一次拿到 42，empty 的迭代零次。不是什么惊天动地的特性，但在那种大段业务逻辑里，能让你在循环体内把 optional 完全忘掉、只跟裸类型打交道，这个心智负担的降低是实打实的。

:::warning
遍历 optional 时循环变量写 `auto&&`，别写 `auto`。`auto` 会丢掉引用性，下一篇您会看到 `optional<T&>` 这种特化，那时候 `auto x` 拿到的是拷贝而不是引用，语义就错了。`auto&&` 是万能引用，能正确保持原始值类别。这个点其实是 Steve Downey 自己幻灯片上的 bug，Q&A 环节被人指出来他才承认。
:::

## 默认 optional 参数：好用，实现却是噩梦

optional 还有个很常见的用法，函数的默认参数：

```cpp
void process(std::optional<int> timeout = std::nullopt);

process();                        // timeout 是 nullopt
process(42);                      // timeout 是 optional<int>(42)
process(std::optional<int>{});    // 也可以显式传
```

用起来非常自然，`int` 会自动提升成 `optional<int>`。但您可能没想过，为了支持这种隐式转换，optional 的构造函数设计变得极其复杂。它得同时处理从 `T` 构造、从 `nullopt` 构造、从另一个 `optional<U>` 构造、拷贝、移动，这些构造函数和转换操作符组合在一起，形成一个庞大的重载集。

笔者以前一直觉得"重载决议不就是找最匹配的嘛"。但实际上当重载集大到一定程度，结果经常出人意料。人脑思考重载决议倾向于走决策树，"这个类型走这条分支，那个类型走那条分支"。编译器不这么干，它把所有候选函数摊在一个平面的集合里，按隐式转换等级、模板特化排序这些规则打分，选最优。候选一多，打分结果就可能跟直觉对不上。optional 实现里那一大坨 SFINAE 和 concepts 约束，本质上就是在驯服这个庞大的重载集，确保"传 int 走 int 的路径，传 nullopt 走 nullopt 的路径，不会出现意外的歧义"。每一行 SFINAE 背后都是有血有泪的重载决议踩坑史。

## 接下来

值版本的底子摸清了：拥有所有权、值语义、`T` 加一个额外状态、C++26 能当 range、构造函数为了隐式转换背着沉重的重载集。但一旦 `T` 换成引用，这些前提全部推翻。`optional<T&>` 不拥有任何东西，赋值不再是值拷贝，构造函数链要重新设计。[下一篇](./03-optional-reference-and-assignment.md)咱们正式进 `optional<T&>` 的核心：它到底是什么，它的赋值为什么一定是重新绑定，以及 `make_optional` 和 CTAD 在引用上挖的坑。
