---
title: "从循环到迭代器：遍历数据的抽象之路"
description: "CppCon 2025 演讲笔记 —— Mike Shah：从 for 循环、指针遍历到迭代器抽象，补全迭代器类别体系并用 GCC 16.1.1 实测 legacy tag 与 C++20 concept 的差异"
conference: cppcon
conference_year: 2025
talk_title: "Back to Basics: C++ Ranges"
speaker: "Mike Shah"
video_youtube: "https://www.youtube.com/watch?v=Q434UHWRzI0"
tags:
  - cpp-modern
  - host
  - beginner
  - Ranges
  - 容器
difficulty: beginner
platform: host
cpp_standard: [11, 17, 20]
chapter: 3
order: 1
---

# 从循环到迭代器：遍历数据的抽象之路

:::tip
本文基于 CppCon 2025 Mike Shah 的 "Back to Basics: C++ Ranges" 做深度二创。上面是 YouTube 链接。本系列计划拆成三篇：本篇讲清楚「遍历数据」这条线（循环 → 指针 → 迭代器 → range-based for），第二篇讲 STL 算法与迭代器陷阱，第三篇才正式进入 Ranges、Views 与管道组合。实验环境为 Arch Linux WSL，GCC 16.1.1，编译选项 `-std=c++20`。
:::

Mike Shah 在演讲开场抛了一句很朴素、但我越想越觉得有道理的话：**算法（algorithm）本质上就是循环**。他说自己读研究生时读到一篇 2012 年做算法性能经验评估的论文，得到的启发是——面对一个陌生代码库，想搞清楚「计算到底发生在哪里」，最快的办法就是去找程序里的循环。因为咱们作为工程师，一半的工作是**转换数据**，另一半是**存储数据**，而循环就是「转换数据」这件事最直接的载体。

:::warning 这里要给 Shah 老师的话打个折
「算法 = 循环」是他自己反复强调的"极度简化"（a gross oversimplification），咱们听个意思就行。严格来说，算法是求解问题的有限步骤序列——递归算法、并行算法（`<execution>`）、协程式的算法，都不一定长着 `for` 的样子。循环只是最常见的载体之一。但作为理解 STL 和 Ranges 的切入点，这个简化是好用的：**先把循环看懂，再看 STL 怎么把循环抽象掉。**
:::

这一篇我们就从最原始的下标循环开始，一步步看 C++ 是怎么把「遍历数据」这件事一层层抽象出来的。我们的终点不是 Ranges（那是第三篇），而是**迭代器**——它是连接「循环」和「算法」的那座桥。

先把实验环境摆出来，后面所有输出都基于它：

```bash
❯ g++ --version
g++ (GCC) 16.1.1 20260430

❯ uname -sr
Linux 6.18.33.1-microsoft-standard-WSL2
```

## 最朴素的遍历：下标 for 循环

一切从这里开始。假设我们有一串字符要逐个打印，绝大多数人下意识写出来的就是三段式的 `for`：

```cpp
#include <iostream>
#include <array>

int main()
{
    std::array<char, 5> message{'H', 'e', 'l', 'l', 'o'};

    for (std::size_t i = 0; i < message.size(); ++i) {
        std::cout << message[i];
    }
    std::cout << '\n';
}
```

这段代码里其实藏着两个隐含的假设，只是我们用得太顺手、不会去想。第一，它假设容器支持 `operator[]` 下标访问；第二，它假设容器自己知道自己的 `size()`。`std::array`、`std::vector`、`std::string` 都满足这两条，所以跑起来没问题。但只要换成 `std::list` 或 `std::set`——它们没有下标访问——这段代码就编不过了。同一份「遍历」的逻辑，换个容器就得重写，这正是抽象不够的信号。

不过先别急着抽象，下标循环该不该用、什么时候用，是个有讲究的问题，但不是这里的重点。我们关心的是：**它表达了「遍历」这件事，但它把遍历和「容器恰好是连续存储、恰好支持下标」这两件事绑死了。** 我们想把前者单独拎出来。

## 换个视角：用指针遍历

Shah 在幻灯片上换了一种写法，当时我愣了一下——这居然也行？他不用下标，而是拿到数组的首地址，然后用指针去走：

```cpp
char* begin = message.data();
char* end   = message.data() + message.size();
for (char* p = begin; p != end; ++p) {
    std::cout << *p;
}
```

这里的 `data()` 返回底层数组的首地址，`end` 就是首地址加上元素个数——指针加法。然后循环体里 `*p` 解引用、`++p` 前进一步。运行结果和下标版本一模一样，但视角完全不同了：**我们不再依赖「下标」这个抽象，而是直接操作「地址」。**

为什么要换这个视角？Shah 的动机很直接——**泛化**。下标假设了「连续存储 + 随机访问」，但现实里很多数据结构不是连续的：链表、树、图。一棵二叉树你怎么 `tree[i]`？你没法用一个整数去索引它。但「从某个起点出发，一步步走到下一个元素」这件事，是所有数据结构遍历的共同内核。指针 `++` 只是最简单的一种「走到下一个」的实现。

:::tip 顺带说一句 STL 的来历
把「递增指针」这件事抽象成一个可替换的对象，是 90 年代 Alexander Stepanov 和 Meng Lee 在惠普（HP）实验室完成的工作——这就是 STL 的原型，1993–94 年提交给委员会，后来并入 C++98 标准。迭代器从一开始就是为了「把算法和数据结构解耦」而生的，不是后来拍脑袋加的。
:::

## 迭代器：指针的泛化

既然「走到下一个元素」可以有不同的实现，那我们干脆把它抽象成一个类型——这就是**迭代器（iterator）**。cppreference 上对迭代器的第一句话就是：**「迭代器是指针的泛化（a generalization of pointers）」**<RefLink :id="1" preview="cppreference, Iterator library — iterators are a generalization of pointers" />。

我们用 `std::begin` 和 `std::end` 这对自由函数来获取容器首尾的迭代器：

```cpp
for (auto it = std::begin(message); it != std::end(message); ++it) {
    std::cout << *it;
}
```

你看，和指针版本的写法几乎一模一样——`begin`、`end`、`!=`、`++`、`*`。区别只在于 `it` 的类型不再是 `char*`，而是一个「表现得像指针」的对象。换成 `std::list`、`std::set`，这段代码一个字都不用改就能跑（只要它们的迭代器支持这些操作）。抽象在这里开始回报我们了。

这里有两个细节值得停一下。第一个是 `begin()` 指向第一个元素，而 `end()` 指向**最后一个元素的下一个位置**（one-past-the-end），它本身不可解引用。这个半开区间 `[begin, end)` 的约定不是随便定的：**它让「空容器」的判断变得极其自然**——空容器就是 `begin == end`，循环条件直接为假，根本不用特判。如果 `end` 指向最后一个元素本身，那空容器就没有「最后一个元素」，处理起来就别扭了。

第二个细节是 `std::begin` / `std::end` 这种**自由函数**形式，和容器的 `.begin()` / `.end()` **成员函数**形式的区别。

:::warning Shah 这里说得不够准
Shah 在演讲里说「只有部分容器拥有 `.begin()`、`.end()`，但并非所有容器都有，所以自由函数更通用」——这个说法其实**不准确**。事实是：**所有 STL 容器都有 `.begin()` / `.end()` 成员函数**，没有例外。

自由函数 `std::begin` / `std::end` 真正的价值在三件事上：一是对**原生数组**（比如 `int arr[5]`）做了重载——数组没有成员函数，只能靠自由函数拿到首尾指针；二是让**泛型代码**写起来更统一（模板里不用区分「这是容器还是数组」）；三是 C++20 的 `std::ranges::begin` 还能处理哨兵（sentinel）和代理类型（比如 `vector<bool>`）。所以更准确的说法是：**自由函数对内置数组和自定义类型更统一，而不是「有些容器没有成员函数」。**
:::

## 迭代器类别体系：不是所有迭代器都一样能干

到这一步，Shah 在演讲里直接说了一句「迭代器的类别我就不展开讲了」，然后跳过去了。但这恰恰是新手最容易栽跟头的地方，我们这篇既然是二创，就把它补齐——这也是本篇的**重头戏**。

不是所有迭代器能力都一样。`std::vector` 的迭代器能 `it + 5` 一下跳五格，但 `std::list` 的迭代器不行，它只能 `++` 一步步走。标准把迭代器按能力分成了几个**类别（category）**，从弱到强大致是：输入（input）→ 前向（forward）→ 双向（bidirectional）→ 随机访问（random access）→ 连续（contiguous，C++20 新增）。

关键问题是：**你怎么知道某个迭代器属于哪个类别？** C++20 之前，靠的是一个叫 `std::iterator_traits<T>::iterator_category` 的类型特征（一个 tag 类型）；C++20 之后，改成了一组**概念（concepts）**，比如 `std::random_access_iterator<T>`、`std::contiguous_iterator<T>`。这两套东西在 C++20 里并存，但它们对同一个迭代器可能给出**不同**的答案——这背后藏着一个非常重要的演进。

我写了个小程序，用 GCC 16.1.1 把常见容器的两套结果都打出来：

```cpp
#include <array>
#include <vector>
#include <string>
#include <deque>
#include <list>
#include <forward_list>
#include <set>
#include <map>
#include <iterator>
#include <type_traits>
#include <cstdio>

// 旧的 C++98 风格：从 iterator_traits 取 tag
template<class Iter>
const char* legacy_tag()
{
    using cat = typename std::iterator_traits<Iter>::iterator_category;
    if constexpr (std::is_same_v<cat, std::contiguous_iterator_tag>) return "contiguous";
    else if constexpr (std::is_same_v<cat, std::random_access_iterator_tag>) return "random_access";
    else if constexpr (std::is_same_v<cat, std::bidirectional_iterator_tag>) return "bidirectional";
    else if constexpr (std::is_same_v<cat, std::forward_iterator_tag>) return "forward";
    else if constexpr (std::is_same_v<cat, std::input_iterator_tag>) return "input";
    else return "?";
}

// 新的 C++20 风格：用 concept 探测
template<class Iter>
const char* cpp20_concept()
{
    if constexpr (std::contiguous_iterator<Iter>) return "contiguous_iterator";
    else if constexpr (std::random_access_iterator<Iter>) return "random_access_iterator";
    else if constexpr (std::bidirectional_iterator<Iter>) return "bidirectional_iterator";
    else if constexpr (std::forward_iterator<Iter>) return "forward_iterator";
    else if constexpr (std::input_iterator<Iter>) return "input_iterator";
    else return "(none)";
}

template<class Iter>
void row(const char* name)
{
    std::printf("%-26s legacy_category=%-15s cpp20_concept=%s\n",
                name, legacy_tag<Iter>(), cpp20_concept<Iter>());
}

int main()
{
    row<std::array<int, 5>::iterator>("std::array<int,5>");
    row<std::vector<int>::iterator>("std::vector<int>");
    row<std::string::iterator>("std::string");
    row<std::deque<int>::iterator>("std::deque<int>");
    row<std::list<int>::iterator>("std::list<int>");
    row<std::forward_list<int>::iterator>("std::forward_list<int>");
    row<std::set<int>::iterator>("std::set<int>");
    row<std::map<int, int>::iterator>("std::map<int,int>");
    row<int*>("int* (raw pointer)");

    static_assert(std::contiguous_iterator<int*>);
    static_assert(std::random_access_iterator<std::vector<int>::iterator>);
    static_assert(!std::contiguous_iterator<std::deque<int>::iterator>);
    static_assert(!std::random_access_iterator<std::list<int>::iterator>);
    std::printf("static_assert checks: PASS\n");
}
```

编译运行：

```bash
❯ g++ -std=c++20 -O2 -Wall iter.cpp -o iter && ./iter
std::array<int,5>          legacy_category=random_access   cpp20_concept=contiguous_iterator
std::vector<int>           legacy_category=random_access   cpp20_concept=contiguous_iterator
std::string                legacy_category=random_access   cpp20_concept=contiguous_iterator
std::deque<int>            legacy_category=random_access   cpp20_concept=random_access_iterator
std::list<int>             legacy_category=bidirectional   cpp20_concept=bidirectional_iterator
std::forward_list<int>     legacy_category=forward         cpp20_concept=forward_iterator
std::set<int>              legacy_category=bidirectional   cpp20_concept=bidirectional_iterator
std::map<int,int>          legacy_category=bidirectional   cpp20_concept=bidirectional_iterator
int* (raw pointer)         legacy_category=random_access   cpp20_concept=contiguous_iterator
static_assert checks: PASS
```

看出门道了吗？**最有意思的就是前几行和最后一行**。`std::array`、`std::vector`、`std::string`，还有裸指针 `int*`——它们的旧 tag 全是 `random_access`，但 C++20 concept 探测出来却是 `contiguous_iterator`。

这就是问题所在：**旧的 tag 体系里，根本就没有 `contiguous`（连续）这个档位**（`contiguous_iterator_tag` 是 C++20 才加进去的）。在 C++20 之前，`int*` 的 `iterator_category` 只能被标成 `random_access`，没法表达「这块内存不仅是随机可访问的、而且是在物理上连续存储的」这个更强的性质。这个区分为什么重要？因为「连续存储」意味着你可以安全地把迭代器 underlying 的数据当成一块连续内存喂给 C 接口（比如 `memcpy`、CUDA kernel、或者 SIMD 指令）——而 `std::deque` 虽然也支持 `it + 5`，但它内部是一段一段的分块存储，**不连续**，所以它的 concept 是 `random_access_iterator` 而不是 `contiguous`。

:::tip 这就是 concepts 比 tag 强的地方
旧 tag 是个继承链（`random_access_iterator_tag` 继承自 `bidirectional_iterator_tag` 继承自……），表达能力有限，只能分层。C++20 的 concept 是一组**正交的、可组合的约束**，能精确说出「随机可访问」和「连续存储」是两件可以独立成立的事。这也是为什么 Ranges 整套体系必须等 C++20 的 concepts 落地才能进标准——没有 concept，很多约束根本表达不出来。关于 concepts 更系统的讲解，可以看 vol4 的相关文章，我们第三篇讲 Ranges 时也会用到。
:::

## 迭代器算术与 std::advance

有了类别概念，再看迭代器的算术操作就清楚了。对随机访问迭代器，你可以直接 `it + 5`、`it - 2`、`it1 - it2`（求距离），这些都是 O(1)。但对双向或前向迭代器，`it + 5` 直接编不过——它们只认 `++` 和 `--`。

那如果我写的是泛型代码，想「往前走 n 步」但又不限定迭代器类别怎么办？标准库给了 `std::advance`<RefLink :id="2" preview="cppreference, std::advance — advances an iterator by n positions" />：

```cpp
auto it   = std::begin(message);
auto last = std::end(message);
std::ptrdiff_t available = std::distance(it, last);
if (5 < available) {
    std::advance(it, 5);   // 安全：确认走得到
}
```

`std::advance` 的妙处在于它会根据迭代器类别**自动选实现**：传给它 `vector::iterator`，它走的是 `it + n`（O(1)）；传给它 `list::iterator`，它退化成 n 次 `++`（O(n)）。同一个调用接口，背后是不同的算法复杂度——这就是泛型编程的甜头。

:::warning advance 不做边界检查
但有一点必须提醒：**`std::advance` 自己不检查边界**。如果你让它往前走 100 步，而容器里只有 5 个元素，它不会报错，而是直接越界——解引用就是段错误（UB）。所以上面那段代码我才先用 `std::distance` 算了剩余长度、做了判断。实战里如果想要带边界检查的迭代器，GCC/Clang 可以加 `-D_GLIBCXX_DEBUG` 编译宏，让标准库的迭代器在调试模式下带上下界检测——下一篇我们会用它抓一个真实的越界 bug。MSVC 那边对应的是 `_ITERATOR_DEBUG_LEVEL=2`。
:::

## range-based for：循环的语法糖

讲了半天迭代器，回到日常写代码——我们绝大多数时候并不会手写 `for (auto it = begin; it != end; ++it)`，而是用 C++11 给的**范围 for 循环（range-based for）**：

```cpp
for (char c : message) {
    std::cout << c;
}
```

干净、不容易写错、不用操心 `end`。但这个语法糖背后到底是什么？其实它就是上面手写迭代器循环的等价改写。按标准规定<RefLink :id="3" preview="cppreference, Range-based for loop — equivalent expansion" />，它大致等价于：

```cpp
{
    auto&& __range = message;
    auto  __begin  = std::begin(__range);   // 或 __range.begin()
    auto  __end    = std::end(__range);     // 或 __range.end()
    for (; __begin != __end; ++__begin) {
        char c = *__begin;
        std::cout << c;                      // 你的循环体
    }
}
```

这就解释了一个常见的疑惑：**range-based for 是怎么知道去调 `begin`/`end` 的？** 答案是编译器在背后帮你插了这两句。它先拿 `__range`，再取首尾迭代器，然后就是普通迭代器循环。所以 range-based for 对迭代器类别没有任何额外要求——只要你的类型能提供 `begin`/`end`（成员或自由函数都行），它就能用。这也是为什么后面我们自定义类型只要实现这两个函数，就能直接塞进 range-based for。

如果遍历的是 `std::map` 这种键值对容器，C++17 的**结构化绑定（structured binding）**配合 range-based for 会非常顺手：

```cpp
const std::map<std::string, int> scores{
    {"alice", 90}, {"bob", 85}
};

for (const auto& [name, score] : scores) {
    std::cout << name << ": " << score << '\n';
}
```

:::warning 给结构化绑定补个版本号
Shah 在演讲里用到了结构化绑定，但**没标它是哪个标准的特性**——这里补一下：**结构化绑定是 C++17（提案 P0217）引入的**<RefLink :id="4" preview="cppreference, Structured binding declaration (since C++17)" />。如果你的工程还在 C++14，这段代码编不过。

另外 Shah 提到一句「省略号语法可以进一步解包」，这个表述其实有点含糊。结构化绑定本身并不支持变长解包（它绑定的元素个数是固定的，得和右侧类型的成员数对上）；省略号在 C++ 里属于模板参数包展开（pack expansion）和折叠表达式（fold expression）的语境，和结构化绑定不是一回事。建议把这句当成口误，别深究。
:::

## 实验：range-based for 和手写循环，编译出来一样吗

每次跟人讲「range-based for 只是语法糖」，总会有人将信将疑——那几个 `__range`、`__begin`、`__end` 临时变量，会不会拖慢性能？我们来实测。我把同一个「求和」用四种写法写出来：

```cpp
#include <vector>

int sum_index(const std::vector<int>& v)
{
    int s = 0;
    for (std::size_t i = 0; i < v.size(); ++i) s += v[i];
    return s;
}

int sum_ptr(const std::vector<int>& v)
{
    int s = 0;
    for (const int* p = v.data(), *e = p + v.size(); p != e; ++p) s += *p;
    return s;
}

int sum_iter(const std::vector<int>& v)
{
    int s = 0;
    for (auto it = v.begin(), e = v.end(); it != e; ++it) s += *it;
    return s;
}

int sum_rangefor(const std::vector<int>& v)
{
    int s = 0;
    for (int x : v) s += x;
    return s;
}
```

然后开 `-O2` 让编译器生成汇编：

```bash
❯ g++ -std=c++20 -O2 -S codegen.cpp -o codegen.s
```

去 `.s` 文件里翻这四个函数的热循环，你会发现它们清一色长成这样（以 `sum_rangefor` 为例）：

```asm
.L19:
    addl    (%rax), %edx      ; s += *p
    addq    $4, %rax          ; p++  (int 占 4 字节)
    cmpq    %rcx, %rax        ; p == e ?
    jne     .L19              ; 不等就继续
```

四种写法生成的循环体**字节级几乎一致**——编译器在 `-O2` 下把那些临时变量、下标计算、指针算术全都归约成了同一段 `add / cmp / jne`。也就是说，**range-based for 在优化开启后没有任何额外开销**，你可以放心地为了可读性用它。代价只有在 `-O0`（不优化）时才显现：那几个 `__begin`/`__end` 临时量会老老实实存在栈上，但谁会在 `-O0` 下追求性能呢。

:::tip 一个 C++17 才修好的小坑
顺带提一句 range-based for 本身的历史：它是 C++11（提案 N2930）进的标准。但 C++11 那版的展开规则有个毛病——它会把 `__end` 每次循环都重新求值（或者说，对 `.end()` 的缓存策略对某些代理类型不友好）。C++17（提案 P0184）专门修了这个，让 `__end` 在循环开始时只求值一次。所以你今天用的 range-based for，是 C++17 修订过的版本，更稳。这也提醒我们：能用新标准就尽量用新标准，很多「语法糖」在后续版本里被悄悄打磨过。
:::

## 一对迭代器，就是一个 range

到这里我们可以给「遍历」画一条完整的线了：**一个起点迭代器 `begin`，加一个终点标记 `end`，中间用 `++` 一步步走**——这一对迭代器，就定义了一段可遍历的数据。标准库管这种「一对迭代器」叫一个 **range**<RefLink :id="5" preview="cppreference, Ranges library — a range is defined by begin and end" />。

这个概念重要在哪里？因为它把「数据在哪里」和「怎么处理数据」彻底解耦了。我写一个求和函数，只要它能接收一对迭代器，那它对 `vector`、`list`、`set`、甚至你自己手写的链表，统统适用——只要这些容器能提供符合要求的迭代器。算法不再绑死在某种具体容器上。

而迭代器这个抽象本身，其实是经典的设计模式——**迭代器模式（Iterator pattern）**，属于 GoF《Design Patterns》里的行为型模式。它的核心思想就是「提供一种方法，顺序访问一个聚合对象中的元素，而又不暴露该对象的内部表示」。C++ 把它做成了语言级的设施（`begin`/`end`/`operator++`/`operator*` 的约定），让任何类型只要遵守这个约定，就能接入整套 STL 算法生态。

这个「一对迭代器即 range」的定义，正是第三篇我们要讲的 `std::ranges::range` concept 的前身。区别在于，C++20 的 range 概念允许 `end` 返回一个**和 `begin` 不同类型**的哨兵（sentinel）——这会解锁一些很有意思的能力（比如遍历以 `'\0'` 结尾的 C 字符串时，不用先算长度）。这个我们留到第三篇展开。

## 到这里我们搞清楚了什么

我们从最原始的下标 `for` 出发，看到了「遍历」这件事如何一步步被抽象：下标循环把遍历和「连续存储 + 随机访问」绑死；指针遍历把它解放到「地址」层面；迭代器把它进一步抽象成「一个能 `++`、能 `*` 的对象」，从此算法和数据结构解耦。我们还补全了 Shah 跳过的迭代器类别体系，并用 GCC 16.1.1 实测了一个关键事实：**旧 tag 把 `vector`/`string`/裸指针都笼统标成 `random_access`，而 C++20 的 concept 能精确说出它们其实是更强的 `contiguous_iterator`**——这正是 concepts 比 tag 强、也是 Ranges 必须等 C++20 才能落地的原因。

核心就一句话：**一对迭代器（一个 `begin`、一个 `end`）定义了一个 range，而 STL 算法就建立在这对迭代器之上。**

下一篇我们就把这对迭代器交给 STL 算法——看 `std::sort`、`std::partition`、`std::transform` 这些「循环的替代品」怎么用，以及它们对迭代器类别有什么硬性要求（比如 `std::sort` 为什么不能用在 `std::list` 上）。那里还有几个迭代器的经典陷阱等着我们：迭代器失效、配错 `begin`/`end`、参数顺序写反。如果你想先复习一下容器本身的内存布局，vol3 的 [span：不拥有数据的视图](../../../../vol3-standard-library/08-span.md) 和容器相关文章是很好的前置阅读。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="Iterator library"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/iterator"
    chapter="Iterators are a generalization of pointers"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="std::advance, std::distance"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/iterator/advance"
    chapter="按迭代器类别自动选择实现复杂度"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="Range-based for loop (since C++11)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/language/range-for"
    chapter="等价展开为 begin/end 迭代器循环"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Structured binding declaration (since C++17)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/language/structured_binding"
    chapter="P0217"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="Ranges library (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/ranges"
    chapter="A range is defined by begin and end"
  />
  <ReferenceItem
    :id="6"
    author="cppreference.com"
    title="std::contiguous_iterator, iterator tags"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/iterator"
    chapter="C++20 引入 contiguous 类别与 concept 体系"
  />
  <ReferenceItem
    :id="7"
    author="Mike Shah"
    title="Back to Basics: C++ Ranges — CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=Q434UHWRzI0"
  />
</ReferenceCard>
