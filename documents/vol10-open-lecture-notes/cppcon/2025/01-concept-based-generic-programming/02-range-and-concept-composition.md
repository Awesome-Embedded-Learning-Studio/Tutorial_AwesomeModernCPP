---
title: "Range、迭代器与 Concept 组合"
description: "CppCon 2025 演讲笔记 —— 从迭代器对的问题到 Range 抽象，再到 Concept 组合与 requires 表达式"
conference: cppcon
conference_year: 2025
talk_title: "Concept-based Generic Programming"
speaker: "Bjarne Stroustrup"
video_bilibili: "https://www.bilibili.com/video/BV1ptCCBKEwW"
video_youtube: "https://www.youtube.com/watch?v=VMGB75hsDQo"
tags:
  - cpp-modern
  - host
  - intermediate
difficulty: intermediate
platform: host
cpp_standard: [20, 23]
chapter: 1
order: 2
---

# 未检查的指针与泛型编程的边界

之前写 C++ 的时候，我遇到过一个特别典型的问题：拿到一个指针，想取它的前 10 个元素组成一个子视图，但写出来的代码怎么看怎么别扭。比如你有一个 `double*`，你想说"我要这个指针指向的前 10 个元素"，但仅仅看那段代码的话，不管是你还是编译器，都没办法知道这个指针到底指向了多少个元素、10 有没有越界。这是完全未检查的。我之前一直觉得这没什么办法，指针嘛，本来就是这样。但后来我意识到，如果你的代码审查没有把这种写法标记为潜在问题，那说明审查本身也不够严格。

当然，现实中我们有时候确实会从各种外部系统、C 接口、遗留代码那里拿到裸指针，你不可能说"我不碰指针"，所以这个能力必须得有。但关键在于：你能不能在拿到指针之后，尽快地把它包装进一个有边界信息、有类型安全检查的东西里面？这就是我之前一直没想明白的地方——我以为"用指针"和"类型安全"是矛盾的，其实不是，它们是两个不同阶段的事情。

## 先解决一个让我烦躁的小问题

在聊更深层的东西之前，我想先说一个让我打字打到崩溃的问题。之前我在搞类型安全数值的时候，要写类似 `number_of<double>` 这样的东西，每次都要把 `double` 显式写出来，太繁琐了。我打字本来就不快，而且说实话，说不定设计和迭代 C 和 Unix 的那批人打字也不快，所以你看 `int`、`double`、`ptr` 这些名字都短得离谱。但我们现在有类型推导了啊，为什么还要自己手写？

我的做法是：如果 `number` 有一个初始化器，那就直接取初始化器的类型作为 `number` 的基本类型。比如我可以写 `number_of{1}`，推导出来就是 `number_of<int>`；写 `number_of{3u}`，就是 `number_of<unsigned>`；写 `number_of{1.0}`，就是 `number_of<double>`。只有在真正需要的时候——比如你拿一个整数来初始化，但你心里想要的是 `double` 精度——你才需要显式写 `number_of<double>{1}`。这样一来，日常使用的时候几乎不用多打几个字，但类型安全一点没丢。

```cpp
#include <iostream>
#include <type_traits>

// number 的基础定义：携带一个值，类型由模板参数决定
template<typename T>
struct number {
    T value;
    // 禁止隐式转换到 T，防止你把它当普通数值用
    explicit operator T() const { return value; }
};

// CTAD（类模板参数推导）指引：从初始化器推导类型
template<typename T>
number(T) -> number<T>;

// 当你需要显式指定类型时，用这个别名简化书写
template<typename T>
using number_of = number<T>;

int main() {
    // 自动推导：number_of<int>
    number_of a{42};
    static_assert(std::is_same_v<decltype(a), number<int>>);

    // 自动推导：number_of<unsigned>
    number_of b{3u};
    static_assert(std::is_same_v<decltype(b), number<unsigned int>>);

    // 自动推导：number_of<double>
    number_of c{2.718};
    static_assert(std::is_same_v<decltype(c), number<double>>);

    // 需要显式指定的情况：用整数初始化，但想要 double
    number_of d = number_of<double>{1};
    static_assert(std::is_same_v<decltype(d), number<double>>);

    std::cout << a.value << "\n";  // 42
    std::cout << b.value << "\n";  // 3
    std::cout << c.value << "\n";  // 2.718
    std::cout << d.value << "\n";  // 1

    // 下面这行编译不过，因为 explicit 阻止了隐式转换
    // int x = a;
    // 但这样是可以的：
    int x = static_cast<int>(a);
    std::cout << x << "\n";  // 42
}
```

你看，编译一下就能跑通，`static_assert` 全部通过。我之前一直觉得 CTAD 就是个语法糖，但在这种场景下它真的让类型安全的代码写起来跟普通代码一样顺手。

## 这算不算泛型编程？

写到这里你可能会问：这玩意儿算泛型编程吗？不就是写了个模板类加个 CTAD 吗？

我之前也犹豫过，但是到这里，我认为这是泛型编程。它用泛型编程的技术解决了一个由于 C++ 历史原因存在的根本性问题：数值类型之间可以隐式转换，导致各种难以察觉的 bug。你可以设计一种没有这些历史包袱的新语言，但我们没那个条件，我们只能在 C++ 里面用一个小型库来消除这些问题。而且你注意看，实现带类型检查的 `number` 的代码，核心逻辑也就 37 行左右；实现一个带边界检查的 `span`，不到 100 行。这比描述语言行为的规范文档都短。用极少的代码，解决了一个系统性的问题，这难道不是泛型编程该干的事吗？（广义的讲，描述系统应该做什么而不用关心绝大多数常见的细节，这就是泛型编程）

## 真正让我头疼的经典问题：std::sort 的错误信息

好了，热身结束，我们来聊一个我折腾了很久、终于开始理解的问题。

你肯定用过 `std::sort`。它的签名大概长这样：接受两个随机访问迭代器 `first` 和 `last`，以及一个可选的比较函数。C++ 标准文档里写得清清楚楚：这两个迭代器必须满足 LegacyRandomAccessIterator 的要求，迭代器的值类型必须满足 MoveAssignable 和 MoveConstructible，比较函数必须满足 StrictWeakOrdering……

但问题是，这些要求从来没有被直接检查过。

它们只存在于文档里，存在于标准委员会的脑子里。编译器在实例化 `std::sort` 的时候，不会先去验证你的迭代器是不是随机访问迭代器。它就是硬实例化，然后在某个深层的模板展开过程中，如果你的类型不满足要求，就会在某个完全不相干的地方报一个几百行的错误。你可能传了一个 `std::list` 的迭代器进去，然后报错信息告诉你某个 `__move_assign` 失败了，或者某个 `__gap` 变量有问题。你看到那个错误信息的时候，整个人都是懵的。

### 先复现一下那个让我崩溃的错误

环境先说一下：我用的 GCC 16.1.1，开了 `-std=c++20`，在 Arch Linux WSL 上跑的。编译命令就是最普通的 `g++ -std=c++20 -Wall -Wextra`。

先写一段看起来完全没问题的代码：

```cpp
#include <list>
#include <algorithm>
#include <iostream>

int main() {
    std::list<int> lst = {5, 3, 1, 4, 2};
    std::sort(lst.begin(), lst.end());
    for (int x : lst) {
        std::cout << x << " ";
    }
    std::cout << "\n";
    return 0;
}
```

你猜怎么着？编译直接炸了。我截取报错信息里相对"可读"的一小段给你看看：

```text
/usr/include/c++/16/bits/stl_algo.h: In instantiation of 'void std::sort(_RandomAccessIterator, _RandomAccessIterator, _Compare) [with _RandomAccessIterator = std::_List_iterator<int>; _Compare = __gnu_cxx::__ops::_Iter_less_iter]':
...
error: no match for 'operator-' (operand types are: 'std::_List_iterator<int>' and 'std::_List_iterator<int>')
```

看到这个报错的时候，我知道是迭代器类型不对，因为我学过 `list` 是双向链表，不支持随机访问。但如果你是一个刚入门不到半年的新手呢？你会看到 `no match for 'operator-'`，然后开始想：我是不是忘了重载什么运算符？我是不是 include 漏了什么头文件？这个报错信息完全没有告诉你真正的问题所在——**你用了一个不支持随机访问的迭代器去调一个要求随机访问的算法**。

我之前一直觉得"模板报错难看"是个被过度吐槽的话题，心想多看几次不就熟了吗。但这次我认真想了想，不是这样的。问题不在于报错"长"，而在于报错信息说的是**症状**（找不到 `operator-`），而不是**病因**（迭代器类别不满足要求）。这两者之间的差距，对于不熟悉模板元编程的人来说，就是天堑。

## 那现在呢？

现在我们有了 concepts。

Concepts 是 C++20 引入的，但它的思想根源可以追溯到 Alex Stepanov（STL 之父）最初对泛型编程的设想<RefLink :id="4" preview="Stepanov & Lee, The Standard Template Library, 1995" />。他从一开始就认为：泛型算法应该对参数有明确的、可检查的要求。这不是什么可选的锦上添花，而是泛型编程的基础设施。只是 C++ 花了三十多年才把这个基础设施建好。

我现在回头看这件事，感觉就像一个房间里一直缺了一面墙，大家都习惯了风吹进来，甚至学会了怎么在风中生活，直到有一天终于有人把墙砌上了，你才发现：原来可以这么舒服。

接下来我想动手写点代码，看看 concepts 到底怎么改变我们写泛型代码的方式。不是那种教科书上的 `template<std::integral T>` 例子，而是真正解决实际问题的用法。我们从一个最简单的场景开始：自己写一个 `sort` 的约束，然后故意传错类型，看看错误信息到底能好到什么程度。

```cpp
#include <iostream>
#include <vector>
#include <list>
#include <concepts>
#include <algorithm>
#include <iterator>

// 先定义我们自己的 concept：随机访问迭代器范围
// 注意：这里用标准库的 concept 来组合，不需要从零写
template<typename Iter>
concept RandomAccessRange = 
    std::random_access_iterator<Iter> &&
    std::sentinel_for<Iter, Iter>;

// 一个受约束的 sort 包装
template<RandomAccessRange Iter, typename Comp = std::less<>>
    requires std::indirect_strict_weak_order<Comp, Iter>
void safe_sort(Iter first, Iter last, Comp comp = {}) {
    std::sort(first, last, comp);
}

int main() {
    // 正确用法：vector 的迭代器是随机访问迭代器
    std::vector<int> v = {5, 3, 1, 4, 2};
    safe_sort(v.begin(), v.end());
    for (int x : v) std::cout << x << " ";
    std::cout << "\n";
    // 输出：1 2 3 4 5

    // 错误用法：list 的迭代器不是随机访问迭代器
    // 取消下面注释会看到非常清晰的错误信息
    // std::list<int> lst = {5, 3, 1, 4, 2};
    // safe_sort(lst.begin(), lst.end());
}
```

你把最后两行注释去掉试试看。在我这边（GCC 16.1.1，`-std=c++20`），错误信息直接告诉你：约束不满足，`std::list<int>::iterator` 不满足 `random_access_iterator`。没有 400 行模板展开，没有 `__gap`、没有 `__move_assign`，就一句话：你的迭代器类型不对。

看到这个错误信息的时候我觉得太痛快了。我之前被 `std::sort` 的错误信息折磨过那么多次，原来解决起来就这么简单——不是加什么工具，不是用什么美化错误信息的脚本，就是在函数签名上把约束写出来。编译器本来就有能力检查，只是之前没有语法让你表达这个约束。

### 用 Concept 把错误拦截在门口

C++20 的标准库里，那些以前只存在于标准文档文字描述中的概念，现在变成了真正的代码实体。其中就包括 `std::random_access_iterator` 和 `std::sortable`。

我之前一直以为 concepts 只是用来做模板约束的语法糖，觉得 `enable_if` 也能干这活儿。但折腾完这个例子之后我才明白，concepts 真正的价值不在于"能不能编译通过"，而在于**编译失败时告诉你为什么失败**。

来看我自己写的一个带 concept 约束的排序函数：

```cpp
#include <concepts>
#include <iterator>
#include <functional>
#include <vector>
#include <iostream>
#include <list>

// 我自己写的排序包装，用 concept 把要求说清楚
template<std::random_access_iterator It, typename Comp = std::less<>>
    requires std::sortable<It, Comp>
void my_sort(It first, It last, Comp comp = {}) {
    std::sort(first, last, comp);
}

int main() {
    // 这个能正常编译
    std::vector<int> vec = {5, 3, 1, 4, 2};
    my_sort(vec.begin(), vec.end());
    for (int x : vec) std::cout << x << " ";
    std::cout << "\n";

    // 这个会在编译期被拦住
    std::list<int> lst = {5, 3, 1, 4, 2};
    my_sort(lst.begin(), lst.end());  // 编译错误！
    return 0;
}
```

现在编译 `list` 那个调用的时候，报错变成了这样：

```text
error: constraint not satisfied
required: 'std::random_access_iterator<std::_List_iterator<int>>'
note: no known conversion from 'std::bidirectional_iterator_tag' to 'std::random_access_iterator_tag'
```

**这说的不就完全是人话吗兄弟们！** 告诉你 `list` 的迭代器是双向迭代器，你要求的是随机访问迭代器，类型对不上。不需要你去翻 `stl_algo.h` 的源码，不需要你理解 SFINAE 的替换失败机制，错误信息直接指向了约束本身。

我特意去查了一下 `std::sortable` 到底规定了什么。它的定义链大致是：`std::sortable<I>` 要求 `std::permutable<I>`，而 `std::permutable<I>` 要求 `std::forward_iterator<I>`——注意，这里只要求**前向迭代器**，不需要随机访问迭代器。此外还要求迭代器的值类型满足 `indirect_strict_weak_order`（也就是能用给定的谓词进行比较），以及能进行 `swap` 操作。以前这些东西全藏在标准文档的散文描述里，只有库的实现者会去看。现在它变成了可查询、可引用的代码实体，你甚至可以在 IDE 里跳转过去看定义。

:::warning 原文错误更正
原文初稿将 `std::sortable` 的迭代器要求写为 `random_access_iterator`，这是错误的。

权威来源（cppreference）原文：
> `template<class I, class Comp = ranges::less, class Proj = std::identity> concept sortable = std::permutable<I> && std::indirect_strict_weak_order<Comp, std::projected<I, Proj>>;`
>
> 其中 `permutable<I>` 要求 `forward_iterator<I>`。
> — cppreference, std::sortable<RefLink :id="1" preview="cppreference, std::sortable" />

实际验证结果（GCC 16.1.1, `-std=c++20`）：

```cpp
static_assert(std::sortable<std::forward_list<int>::iterator>);  // 通过！
static_assert(std::sortable<std::list<int>::iterator>);           // 通过！
static_assert(std::sortable<std::vector<int>::iterator>);         // 通过！
```

`forward_list` 只有前向迭代器，但它同样满足 `std::sortable`。

需要区分的是：**`std::sort` 算法**要求随机访问迭代器，但 **`std::sortable` 这个 concept** 只要求前向迭代器。前者是算法的实现约束，后者是概念的最小要求。
:::

所以回顾来看：concepts 不是"让模板报错更好看一点"的语法糖，它是补齐了泛型编程缺失了三十多年的那块拼图。之前我们写的所谓泛型代码，其实都是"没有约束声明的泛型代码"——约束存在，但只存在于文档里、存在于程序员脑子里，编译器看不到。现在 concepts 让约束变成了代码的一部分，编译器终于能做它早就该做的事情了。

---

# 迭代器的坑与 Range 的解法

说实话，在学 C++ 的前两年里，我对标准库算法的调用方式早就习以为常了——传个 begin，传个 end，再传个比较函数，三件套走天下。直到前几天我手贱对一个 `std::list` 调了 `std::sort`，然后盯着屏幕上那一坨模板报错信息看了整整二十分钟，我才真正理解 C++20 引入 concepts 和 ranges 到底在解决什么问题。今天就把这个"从痛苦到恍然大悟"的过程完整记录下来。

## 但迭代器对还有更大的坑

错误信息好看了，我就满意了吗？没有。因为我想到了一个更恐怖的问题。

我之前在项目里见过这样的代码——有人把 `begin` 和 `end` 传反了：

```cpp
std::vector<int> vec = {1, 2, 3, 4, 5};
std::sort(vec.end(), vec.begin());  // 注意：反了！
```

你知道这会发生什么吗？不会立刻崩溃。`std::sort` 内部会计算 `last - first`，得到一个很大的数（因为指针相减，`end` 在 `begin` 后面，结果本应是正数，但反过来就是负数转成了无符号数，变成一个巨大的值），然后算法会开始疯狂地读写越界内存。它可能跑很久才 segfault，也可能"安静地"把你的堆内存写坏，然后在完全不相干的地方崩掉。这种 bug 我调试过一次，花了整整一个下午。

还有一种更离谱的情况——两个迭代器来自不同的容器：

```cpp
std::vector<int> a = {1, 2, 3};
std::vector<int> b = {4, 5, 6};
std::sort(a.begin(), b.end());  // 两个不同容器的迭代器！
```

这在 C++ 标准里是未定义行为，但编译器完全不会拦你。因为从类型系统的角度看，`a.begin()` 和 `b.end()` 的类型完全一样，都是 `std::vector<int>::iterator`。编译器没办法知道它们是不是来自同一个容器。

这些问题，光靠给迭代器加 concept 约束是解决不了的。因为问题不在于迭代器"是什么类型"，而在于这一对迭代器"之间的关系"是否合法。

## 所以 Range 才是正道

C++20 引入 range 不是为了炫技，而是为了从根本上解决"迭代器对"这个设计上的缺陷。

一个 range 天然地代表"一个容器的一段连续元素"，它不会出现 begin 和 end 来自不同容器的情况，也不容易出现前后颠倒的情况（虽然理论上你可以构造一个 sentinel 不匹配的 range，但正常用法下不会）。

而且说实话，每次写算法调用的时候，`xxx.begin(), xxx.end()` 这套东西真的太啰嗦了。而且之前闹出来过`A.begin(), B.end()`这种事情。。。嗯，range我喜欢你！

来看看用 range 的写法有多清爽：

```cpp
#include <ranges>
#include <algorithm>
#include <vector>
#include <string>
#include <iostream>

// 我自己包装的 range 版排序
template<std::ranges::random_access_range R,
         typename Comp = std::ranges::less>
    requires std::sortable<std::ranges::iterator_t<R>, Comp>
void my_sort(R&& r, Comp comp = {}) {
    std::ranges::sort(std::forward<R>(r), comp);
}

int main() {
    // vector of doubles，升序
    std::vector<double> vd = {3.14, 1.41, 2.72, 0.58};
    my_sort(vd);
    for (double x : vd) std::cout << x << " ";
    std::cout << "\n";

    // vector of strings，降序
    std::vector<std::string> vs = {"hello", "world", "cpp", "ranges"};
    my_sort(vs, std::ranges::greater{});
    for (const auto& s : vs) std::cout << s << " ";
    std::cout << "\n";

    return 0;
}
```

输出：

```text
0.58 1.41 2.72 3.14
world hello ranges cpp
```

你看，调用的时候只需要传一个 range 对象就行了。不需要 `begin()`、`end()`，不需要担心两个迭代器是否匹配。而且约束写的是 `std::ranges::random_access_range`，直接表达的是"这个东西得支持随机访问"，而不是"这个东西的迭代器得满足什么条件"。语义层面就高了一级。

如果你试图把一个 `list` 传进来：

```cpp
std::list<int> lst = {5, 3, 1, 4, 2};
my_sort(lst);  // 编译错误
```

报错会直接告诉你 `std::list<int>` 不满足 `random_access_range`。干净利落。

我之前一直觉得 range 只是个语法糖，`views::transform`、`views::filter` 那套管线写法看着酷炫但没必要。现在回头看，range 最核心的价值其实是**把"一对迭代器"这个容易出错的抽象，替换成了"一个范围"这个不容易出错的抽象**。管线写法只是顺带的好处。

到这里，从迭代器到 range 的演进逻辑我终于彻底想通了。但故事还没完——上面那个例子里，我对 `vector<string>` 做了降序排序，用的是 `std::ranges::greater{}`。这看起来没问题，但如果你对字符串排序有更细致的需求呢？比如按长度排序、按字典序忽略大小写排序？这就涉及到谓词的定制了，我们接着往下看。

---

# Concept 组合与重载决议

我对 concepts 的理解一直停留在"它就是 SFINAE 的语法糖"这个层面上。我觉得它不过是让编译错误好看一点，写起来稍微清爽一点，但本质上还是在搞模板那套东西。对吗？如果对，我恐怕没有这个笔记了。

## 从 sort 到 forward_sortable_range

事情起因是我需要给一个 `std::forward_list` 排序。我之前一直有个习惯，就是写一个通用的 `sort` 函数，什么都不加约束，模板参数一摆，什么类型都往里塞。结果你猜怎么着？编译器当然不会报错，但运行的时候直接炸了，因为 `std::sort` 底层需要随机访问迭代器，而 `forward_list` 只有前向迭代器。这种错误在编译期完全抓不到，到了运行时才暴露，排查起来简直让人崩溃。

所以，能不能在类型系统层面就把这种错误拦住？不是靠文档说"请勿对 list 使用此函数"（要知道现在大家都很忙，没时间陪你看文档，除非你被编译器干了一顿！），而是让代码本身就不允许你这么干。这就是 concepts 要解决的核心问题——不是"报错信息更好看"，而是"错误的用法根本写不出来"。

我动手写了一个针对前向可排序范围的约束，然后基于这个约束提供了 `sort` 的重载。先看看我定义的 concept 长什么样：

```cpp
#include <concepts>
#include <ranges>
#include <forward_list>
#include <vector>
#include <algorithm>
#include <iostream>
#include <iterator>

// 先定义一个"前向可排序范围"的 concept
// 它说的是：这个范围必须是 forward_range，并且它的元素必须能用给定的谓词进行比较
template<typename R, typename C = std::less<>>
concept forward_sortable_range =
    std::ranges::forward_range<R> &&
    requires(R& r, C comp) {
        // 需要能拿到前向迭代器
        { std::begin(r) } -> std::forward_iterator;
        // 元素之间需要能用谓词比较
        { *std::begin(r) < *std::begin(r) } -> std::convertible_to<bool>;
    };
```

你可能会问，为什么不直接用 `std::sortable`？好问题。`std::sortable` 在标准库里是有的，而且它实际上只要求前向迭代器就够了<RefLink :id="1" preview="cppreference, std::sortable" />——是的，`forward_list` 的迭代器也满足 `std::sortable`。不过我这里想表达的是"这个范围可以排序，但不一定用随机访问的方式排"这个语义层次，所以我还是选择自己定义一个更明确的约束。而且 `forward_sortable_range` 还额外检查了元素之间的比较操作，这在某些场景下比裸用 `std::sortable` 更能表达意图。这就是 concepts 的威力——你可以精确地表达你需要的语义，而不是被标准库的某个现成概念绑死。

然后我写了两个 `sort` 重载，一个给随机访问范围用，一个给前向范围用：

```cpp
// 重载1：给随机访问范围用的（vector、deque 等）
// 约束更严格，编译器会优先匹配这个
template<std::ranges::random_access_range R, typename C = std::less<>>
    requires std::sortable<std::ranges::iterator_t<R>, C>
void my_sort(R& r, C comp = C{}) {
    std::ranges::sort(r, comp);
    std::cout << "  [走随机访问路径]\n";
}

// 重载2：给前向可排序范围用的（forward_list 等）
// 关键：用 !random_access_range 显式排除随机访问范围，避免歧义
template<forward_sortable_range R, typename C = std::less<>>
    requires (!std::ranges::random_access_range<R>)
void my_sort(R& r, C comp = C{}) {
    // 简单实现：复制到 vector，排序，再复制回来
    // 生产环境可以用更高效的 list 排序算法，这里只是为了演示
    std::vector<std::ranges::range_value_t<R>> tmp(
        std::begin(r), std::end(r)
    );
    std::ranges::sort(tmp, comp);
    std::ranges::copy(tmp, std::begin(r));
    std::cout << "  [走前向迭代器路径：复制-排序-回写]\n";
}
```

这里有个特别重要的点，也是我之前踩过大坑的地方：**概念重载的消歧规则**。初稿中我以为"编译器会自动挑选约束最严格的那个重载"，但实际测试发现：当重载1的约束是 `std::ranges::random_access_range`，重载2的约束是自定义的 `forward_sortable_range` 时，两个约束之间没有 subsumption（包含）关系——编译器无法判断谁更严格，于是报**歧义错误**。

:::warning 原文错误更正：概念重载的消歧
原文声称"当多个重载都能匹配时，编译器会挑选约束最严格的那个"。这个说法在特定条件下成立（当两个约束之间有 subsumption 关系时），但对自定义 concept 不一定成立。

C++20 的约束偏序规则（[temp.constr.order]）要求：重载 A 的约束必须**包含**（subsume）重载 B 的约束，编译器才会选 A。`std::ranges::random_access_range` 确实包含 `std::ranges::forward_range`（因为前者是后者的精细化），但它**不包含**自定义的 `forward_sortable_range`（因为后者的 `requires` 子句包含不同的原子约束）。

实际验证结果（GCC 16.1.1, `-std=c++20`）：

```text
error: call of overloaded 'my_sort(std::vector<int>&)' is ambiguous
```

修正方式：在重载2上添加 `requires (!std::ranges::random_access_range<R>)`，显式排除随机访问范围，避免两个重载同时匹配。
:::

这个 `!random_access_range` 的技巧很实用——本质上就是告诉编译器"只有在不满足重载1的条件下，才考虑重载2"。传 `vector` 时重载2被排除，传 `forward_list` 时重载1不满足，各自匹配唯一的候选，没有歧义。

来跑一下验证：

```cpp
int main() {
    // 测试1：vector 走随机访问路径
    std::vector<int> v = {5, 3, 1, 4, 2};
    std::cout << "排序 vector: ";
    my_sort(v);
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';

    // 测试2：forward_list 走前向迭代器路径
    std::forward_list<int> fl = {5, 3, 1, 4, 2};
    std::cout << "排序 forward_list: ";
    my_sort(fl);
    for (int x : fl) std::cout << x << ' ';
    std::cout << '\n';

    // 测试3：用 greater 降序排
    std::vector<int> v2 = {1, 2, 3, 4, 5};
    std::cout << "降序排序 vector: ";
    my_sort(v2, std::greater<>{});
    for (int x : v2) std::cout << x << ' ';
    std::cout << '\n';

    return 0;
}
```

编译运行（GCC 16.1.1，`-std=c++20`）：

```text
排序 vector:   [走随机访问路径]
1 2 3 4 5 
排序 forward_list:   [走前向迭代器路径：复制-排序-回写]
1 2 3 4 5 
降序排序 vector:   [走随机访问路径]
5 4 3 2 1 
```

完美，两条路径各走各的，互不干扰。你注意看，谓词我给了默认值 `std::less<>`，这样常见情况就不用每次都传了，想降序的时候传个 `std::greater<>{}` 就行。这种"提供合理默认值"的习惯是我从标准库学来的，能大幅降低调用方的负担。

## Concepts 不是新发明，它一直都在

搞完上面这个例子之后，我回头想了想，突然意识到一个事情：concepts 这个东西，根本就不是 C++20 发明的。

你往历史里看，Dennis Ritchie 在早期的 C 语言里就隐式地用了 concepts——`int` 和 `float` 就是两个 concept，只不过那时候不叫这个名字，叫"类型"。你写一个函数接受 `int`，你其实就是在说"我需要一个满足整数语义的东西"。STL 也有，Stepanov 设计 STL 的时候脑子里就有 iterator、container、sequence 这些概念，只是当时 C++ 没有语言层面的支持，所以这些概念只存在于文档和设计者的脑海里，存在于隐式约定中。再往前看，数学领域几百年前就有 monad、group、ring 这些抽象概念了，图论的概念甚至可以追溯到 1736 年欧拉的那篇关于柯尼斯堡七桥问题的论文。

所以 concepts 的本质是什么？**它是对领域知识的形式化表达**。不管你用不用 C++ 的 `concept` 关键字，只要你做泛型编程，你脑子里就必须有 concept。区别只在于：以前这些概念是隐式的，藏在设计者的脑子和文档里，编译器不知道；现在你可以把它写成代码，编译器能帮你检查。

我之前看过很多所谓"泛型"的 C++ 代码，模板参数就写个 `typename T`，什么约束都没有，然后在注释里写"T 必须支持加法和乘法"。这不就是没有形式化的 concept 吗？注释写了我能不看吗？编译器能帮你检查吗？都不能。所以这种代码一传错类型就炸，而且炸的地方离真正的错误点十万八千里。

## 从"模板编程"到"基于 Concept 的泛型编程"

我现在越来越觉得，我们不应该再说"模板编程"这个词了，应该叫"基于 concept 的泛型编程"。这两个说法的区别在哪？

"模板编程"把注意力放在了"怎么实例化"上，你脑子里想的是类型推导、SFINAE、特化偏序这些机制层面的东西。而"基于 concept 的泛型编程"把注意力放在了"我需要什么"上，你脑子里想的是"我需要一个可排序的前向范围"，然后你把这个需求写成 concept，再写满足这个 concept 的函数。机制变成了实现细节。您看，这样咱们编程的思路就对了——注重"需要什么"而非"怎么实现"。

这个思维转变对我来说是关键性的。以前我写模板代码，总是先写函数体，写完发现编译不过，再加 SFINAE 修修补补，整个过程是"自底向上"的。现在我学会了先定义 concept，把需求想清楚，再写实现，整个过程是"自顶向下"的。不仅写起来顺畅，读起来也清晰——看到函数签名上的 concept 约束，你立刻就知道这个函数期望什么，不需要去翻实现。

而且 concepts 往往是层层组合的，就像我上面那个 `forward_sortable_range`，它是由 `forward_range`、`forward_iterator` 这些更基础的概念组合而成的。你定义的 concept 越多、越细粒度，复用起来就越灵活。这跟函数分解是一个道理——好的 concept 设计和好的函数设计一样，都是关于"正确的抽象层次"。

这样来看，concepts 不是 C++20 凭空造出来的新玩具，它是泛型编程一直缺失的那块拼图。没有它，泛型编程也能做，但就像蒙着眼睛走钢丝；有了它，你至少有了一根平衡杆。回头看看其实没那么难，但没想通之前就是觉得别扭。

---

# requires 表达式与使用模式

到底什么时候该用 `requires` 表达式，什么时候该定义一个具名的 concept？看到演讲里提到"如果你在你的代码里 require 了 requires，那你可能是做错了什么"这句话的时候<RefLink :id="3" preview="Stroustrup, Concept-based Generic Programming, CppCon 2025" />，我非常有共鸣——原来不是我一个人这么困惑，这确实是一个有明确判断标准的问题。

今天我们就把这块彻底理清楚。

## 从一个最简单的组合开始

之前我一直觉得 concept 组合是什么高深的东西，直到有一天我写了一个排序的泛型函数，需要同时要求"这个范围可以向前遍历"并且"这个范围里的元素可以排序"。我当时写了一堆乱七八糟的约束，后来发现其实就是把两个 concept 用 `&&` 连起来，跟写一个普通函数的逻辑与操作没有任何本质区别。

```cpp
#include <concepts>
#include <ranges>
#include <vector>
#include <algorithm>
#include <iostream>

// 我自己定义的一个 concept：可排序的范围
// 本质上就是 forward_range 和 sortable 的"与"操作
template<typename R>
concept sortable_range = std::ranges::forward_range<R> && std::sortable<std::ranges::iterator_t<R>>;

// 用这个组合出来的 concept 去约束函数模板
template<sortable_range R>
void my_sort(R&& r) {
    std::ranges::sort(std::forward<R>(r));
}

int main() {
    std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};
    my_sort(v);  // 编译通过，vector<int> 既满足 forward_range 又满足 sortable
    
    // my_sort("hello");  // 编译错误，字符串不满足 sortable
    // 报错信息会明确告诉你：约束 'sortable_range<R>' 未满足
    
    for (int x : v) std::cout << x << ' ';
    // 输出：1 1 2 3 4 5 6 9
}
```

你看，语法上虽然是在模板参数列表里写 `sortable_range R` 而不是普通的 `typename R`，但这个 concept 本身的定义就是一个返回 bool 的表达式，`std::ranges::forward_range<R>` 是 bool，`std::sortable<...>` 也是 bool，两个 bool 做 `&&`，得到一个 bool。就这么简单。我之前一直把它想复杂了，以为有什么特殊的语法魔法在里面，其实没有。

## requires 表达式：concepts 的底层砖块

搞懂了组合之后，下一个问题就是：这些标准库里的 concept 是怎么实现出来的？答案就是 `requires` 表达式。

我一开始看到 `requires` 关键字在两个地方出现就懵了——一个是 `requires` 子句（放在函数签名后面那种），一个是 `requires` 表达式（花括号里写一堆检查的那种）。这两个东西名字一样但职责完全不同。`requires` 表达式才是真正干活的那个，它检查某个构造是否有效。

我们来看经典的 `equality_comparable` 应该怎么自己写：

```cpp
#include <concepts>
#include <type_traits>

// 自己实现一个简化版的 equality_comparable
// 检查 T 和 U 之间是否可以进行相等和不相等比较
template<typename T, typename U>
concept my_equality_comparable =
    requires(const T& t, const U& u) {
        // 下面每一行都是一个"使用模式"的检查
        // 编译器会尝试编译这些表达式，如果都能编译通过，这一项就是 true
        { t == u } -> std::convertible_to<bool>;
        { u == t } -> std::convertible_to<bool>;
        { t != u } -> std::convertible_to<bool>;
        { u != t } -> std::convertible_to<bool>;
    };

// 验证一下
static_assert(my_equality_comparable<int, double>);   // int 和 double 可以比较
static_assert(my_equality_comparable<int, int>);      // 同类型当然可以
static_assert(!my_equality_comparable<int, std::nullptr_t>);  // int 和 nullptr 不能比较
```

这里有几个细节我之前踩过坑。第一，`requires` 花括号里的参数列表 `const T& t, const U& u` 是引入了一些"假设存在的变量"，仅供花括号内部的检查使用，它们不会真的被创建出来。第二，`{ t == u } -> std::convertible_to<bool>` 这个语法，花括号里是要检查的表达式，箭头后面是对返回类型的要求。注意这里用的是 `convertible_to<bool>` 而不是 `same_as<bool>`，因为 `==` 运算符返回的不一定是严格的 `bool` 类型，只要能隐式转换成 bool 就行——这是 C++20 标准里明确规定的。

## "require 了 requires"到底是什么意思

演讲里说"如果你在你的代码里 require 了 requires，那你可能是做错了什么"，我一开始没理解这句话，后来想了想，它说的是这种情况：

```cpp
// 反面教材：直接在函数约束里写 requires 表达式
template<typename T>
    requires requires(T t) { t + t; }
auto add_stuff(T a, T b) {
    return a + b;
}

// 正确做法：给它起个名字，定义成 concept
template<typename T>
concept addable = requires(T t) { t + t; };

template<addable T>
auto add_stuff(T a, T b) {
    return a + b;
}
```

为什么第一种写法不好？因为当你看到报错信息的时候，你看到的是一堆 `requires` 表达式的展开，根本不知道这个约束的"语义意图"是什么。而第二种写法，编译器报错会直接告诉你"约束 `addable<T>` 未满足"，你一看名字就懂了。这就是"具有明确含义名称的 concept"的价值所在。`requires` 表达式是砖块，concept 是用砖块盖出来的房子，你当然应该住在房子里而不是直接住在砖块上。

## 使用模式：为什么它改变了游戏规则

接下来要说的这个东西，是我觉得 concepts 里最精妙的设计，没有之一——使用模式（use patterns）。

我之前一直以为，如果要约束一个类型支持 `+` 运算，我需要精确指定这个 `+` 是怎么实现的。是成员函数 `T::operator+` 吗？是自由函数 `operator+(T, T)` 吗？参数带不带 `const`？返回类型到底是什么？如果我要在 concept 里写清楚这些，那简直是个噩梦，而且会给所有使用这个 concept 的人带来巨大的负担。

但使用模式完全换了一个思路：它不关心你怎么实现，它只关心"这件事能不能做"。

```cpp
#include <concepts>
#include <string>

// 我只要求 A + B 这个表达式能编译通过，并且结果能转成某种公共类型
// 至于 A + B 是通过成员函数实现还是自由函数实现，我完全不关心
template<typename A, typename B>
concept can_add = requires(A a, B b) {
    { a + b } -> std::convertible_to<std::common_type_t<A, B>>;
};

// 来验证一下使用模式的威力

// 情况1：内置类型的加法
static_assert(can_add<int, int>);

// 情况2：混合模式算术，int + double
static_assert(can_add<int, double>);

// 情况3：std::string 的加法（通过自由函数 operator+ 实现）
static_assert(can_add<std::string, std::string>);

// 情况4：自定义类型，用成员函数实现 operator+
class MyInt {
    int val;
public:
    MyInt(int v) : val(v) {}
    MyInt operator+(const MyInt& other) const { return MyInt(val + other.val); }
};
static_assert(can_add<MyInt, MyInt>);

// 情况5：另一个自定义类型，用自由函数实现 operator+
class MyFloat {
    float val;
public:
    MyFloat(float v) : val(v) {}
    float get() const { return val; }
};
MyFloat operator+(const MyFloat& a, const MyFloat& b) {
    return MyFloat(a.get() + b.get());
}
static_assert(can_add<MyFloat, MyFloat>);

// 情况6：int 和 std::string 不能相加
static_assert(!can_add<int, std::string>);
```

:::details 原文代码修正说明
初稿中 `can_add` 的定义使用了默认模板参数 `typename R = std::remove_cvref_t<decltype(std::declval<A>() + std::declval<B>())>` 来推导返回类型。这个写法有一个陷阱：当 `A + B` 不合法时（比如 `int + std::string`），默认参数的求值会在模板参数替换阶段就失败，导致**硬编译错误**而非 concept 返回 `false`。

实际验证结果（GCC 16.1.1, `-std=c++20`）：

```text
error: no match for 'operator+' (operand types are 'int' and 'std::__cxx11::basic_string<char>')
```

这是一个硬错误——`static_assert(!can_add<int, std::string>)` 根本无法编译。

修正方式：去掉默认模板参数中的返回类型推导，改用 `std::common_type_t<A, B>` 作为约束目标。这样当 `A + B` 不合法时，只有 requires 表达式内部的检查失败（在"直接上下文"中），concept 正确返回 `false`。
:::

看到这里我特别兴奋。`can_add` 这个 concept 对 `MyInt`（成员函数实现）和 `MyFloat`（自由函数实现）都适用，它根本不关心实现方式。这意味着接口变得极其稳定——你今天用成员函数实现 `operator+`，明天改成自由函数，只要 `a + b` 这个表达式还能用，所有依赖 `can_add` concept 的代码都不需要改。这个稳定性是以前用 SFINAE 和 tag dispatch 根本做不到的。

而且这种检查是隐式的。什么叫隐式？就是当你实例化模板的时候，编译器自动帮你检查了，你不需要写任何额外的代码。但如果你担心，你想尽早确认某个类型满足某个 concept，你也可以主动检查，就像上面我写的那些 `static_assert` 一样。这种灵活性特别好——类型集合是开放的，任何人都可以写一个新的类型，只要它满足使用模式就能用；但同时在你想加防护的地方，你可以显式地加。

## 混合模式算术和隐式转换的处理

使用模式还有一个好处，就是它天然地处理了 C++ 里那些复杂的隐式转换规则。比如 `int + double` 能工作，是因为 int 会隐式转换成 double。使用模式不关心这个转换是怎么发生的，它只验证 `int + double` 这个表达式最终能不能编译通过。

```cpp
#include <concepts>

template<typename A, typename B>
concept can_compare = requires(A a, B b) {
    { a == b } -> std::convertible_to<bool>;
};

// int 和 double 可以比较，因为 int 会隐式转换为 double
static_assert(can_compare<int, double>);

// int 和 long 可以比较
static_assert(can_compare<int, long>);

// int 和 std::string 不行，没有从 string 到 int 的隐式转换
static_assert(!can_compare<int, std::string>);
```

你可能会问：如果我想更精确地控制，不允许隐式转换，只允许精确类型匹配怎么办？那你可以用 `std::same_as` 替代 `std::convertible_to`，或者在 requires 表达式里加更多约束。使用模式给你的是最宽松的默认行为，但你随时可以收窄。这比以前那种"默认什么都不检查"的方式好太多了。

## 为什么 concepts 必须是语言的一部分，而不是一个孤立的子语言

最后说一点我之前没想明白、现在想通了的事情。演讲里提到"我不喜欢那些只能自成一派的孤立子语言"，这句话点醒了我。

concepts 不是 C++ 里的一个独立小世界。它可以和 `if constexpr` 配合，可以和 SFINAE 共存（虽然你不再需要手写 SFINAE 了），可以和 constexpr 函数配合，可以和模块配合。它用的就是 C++ 本身的语言特性——`requires` 表达式里可以写任何合法的 C++ 表达式，concept 的定义就是普通的 `template` + `bool` 常量表达式。

这意味着你不需要学一套"concept 专属的语法"然后再学一套"C++ 的语法"，你学的就是 C++ 本身。concepts 让泛型编程从"用模板元编程的黑魔法去模拟约束"变成了"用语言本身去表达约束"。到这里终于搞通了，回头看看其实没那么难，难的是把以前那些 SFINAE 的思维惯性给甩掉。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="std::sortable"
    :year="2020"
    url="https://en.cppreference.com/w/cpp/iterator/sortable"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="std::ranges::sort"
    :year="2020"
    url="https://en.cppreference.com/w/cpp/algorithm/ranges/sort"
  />
  <ReferenceItem
    :id="3"
    author="Bjarne Stroustrup"
    title="Concept-based Generic Programming"
    publisher="CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=VMGB75hsDQo"
  />
  <ReferenceItem
    :id="4"
    author="Alexander Stepanov, Meng Lee"
    title="The Standard Template Library"
    publisher="HP Laboratories"
    :year="1995"
    chapter="TR95-11(R.1)"
    url="https://www.stepanovpapers.com/stl.pdf"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="C++ named requirements: LegacyRandomAccessIterator"
    :year="2020"
    url="https://en.cppreference.com/w/cpp/named_req/RandomAccessIterator"
  />
</ReferenceCard>
