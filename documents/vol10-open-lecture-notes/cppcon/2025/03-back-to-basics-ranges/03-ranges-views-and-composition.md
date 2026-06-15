---
chapter: 3
conference: cppcon
conference_year: 2025
cpp_standard:
- 20
- 23
description: CppCon 2025 演讲笔记 —— Mike Shah：受约束算法、views 惰性求值、管道操作符、ranges::to，补 eager
  vs lazy 实测基准、无限 range、views 版本归属表（C++20/23/26）
difficulty: intermediate
order: 3
platform: host
reading_time_minutes: 19
speaker: Mike Shah
tags:
- cpp-modern
- host
- intermediate
- Ranges
talk_title: 'Back to Basics: C++ Ranges'
title: Ranges、Views 与管道组合：惰性求值的力量
video_youtube: https://www.youtube.com/watch?v=Q434UHWRzI0
---
# Ranges、Views 与管道组合：惰性求值的力量

:::tip
这是 CppCon 2025 Mike Shah "Back to Basics: C++ Ranges" 系列的收官篇。前两篇我们走完了「循环 → 迭代器 → 算法」这条线，也把迭代器的几个经典陷阱（失效、配对、参数顺序）拆了一遍。这一篇正式进入 Ranges 的核心：受约束算法、views 的惰性求值、管道组合，以及把结果物化回容器的 `ranges::to`。本篇实验较多，且横跨 C++20 与 C++23，所以编译选项会在 `-std=c++20` 和 `-std=c++23` 之间切换——这一点本身就是本篇的一个伏笔。环境：Arch Linux WSL，GCC 16.1.1。
:::

上一篇结尾，Shah 用一张「迭代器必须滚蛋」的夸张幻灯片收尾。这一篇我们就来看 Ranges 是怎么在迭代器之上，重新设计一层更安全、更好组合的接口的。先从最基础的问题开始：**Ranges 到底改了什么？**

## range 还是那对迭代器，但 end 可以是「哨兵」

底层定义没变——一个 range 仍然由一个起点和一个终点界定。但 C++20 给了它一个重要的扩展：**终点可以是一个和起点不同类型的东西，叫做哨兵（sentinel）**<RefLink :id="1" preview="cppreference, Ranges library — sentinel may differ in type from iterator" />。

为什么要允许不同类型？看一个经典例子：遍历一个以 `'\0'` 结尾的 C 字符串。在传统迭代器模型里，你得先 `strlen` 算出长度，才能确定 `end`——但你明明只需要「一直走，直到遇到 `'\0'`」就行了。sentinel 就是表达「走到某个条件成立为止」的终点，它的类型可以和迭代器不同，只要它们之间能比较（`it == sentinel`）就行。这让遍历「不知道长度的序列」变得自然——而这一点，正是后面「无限 range」能成立的基础。

## 从 range-v3 到标准 Ranges：concepts 是关键拼图

Ranges 这套东西不是 C++20 凭空冒出来的。它的原型是 Eric Niebler 的 **range-v3** 库<RefLink :id="2" preview="Eric Niebler, range-v3 — C++14 library, prototype of standard Ranges" />，在 C++14 时代就能用了。如果你现在的工程还卡在 C++14/17，可以直接用 range-v3 练手——它的 API 和标准库 Ranges 高度相似，将来迁移成本很低。

那为什么标准库版本拖到了 C++20？**因为 Ranges 的落地严重依赖 concepts（概念）**<RefLink :id="3" preview="cppreference, Concepts library (C++20) — constraints enable Ranges" />。Ranges 需要精确表达「什么东西算一个 range」「什么迭代器算随机访问的」这类约束。在 concepts 出现之前，这些约束只能靠 SFINAE（替换失败不是错误）来实现——结果是：一旦你传错类型，编译器吐出来的错误信息动辄几十行模板天书，根本没法读。concepts 让约束可以被命名、被早期求值，这才是 Ranges 能进标准的最后一块拼图。

## 受约束算法：少传一个参数，少一个出错的机会

Ranges 最直接的体感改进，就是**受约束算法（constrained algorithms）**——cppreference 上的正式叫法。它们和经典算法同名，但放在 `std::ranges::` 命名空间下。区别在于：**经典算法要你传一对迭代器 `(first, last)`，ranges 版本只要传一个容器（或任何 range）就行**<RefLink :id="4" preview="cppreference, Constrained algorithms — pass the whole range, not iterator pair" />。

```cpp
#include <algorithm>
#include <ranges>
#include <vector>

std::vector<int> v{3, 1, 4, 1, 5, 9};

std::sort(v.begin(), v.end());   // 经典：传一对迭代器
std::ranges::sort(v);            // ranges：传整个容器
```

`ranges::sort(v)` 做的事和 `sort(v.begin(), v.end())` 完全一样，但它少传了两个参数。这带来的好处不只是少敲字——回到上一篇陷阱二「配错 begin/end」，**经典算法允许你把两个不同容器的迭代器配错，ranges 版本根本没给你这个机会**，因为它只收一个对象。少一种出错的可能，就是实打实的安全性提升。

受约束算法也支持 span、自定义容器、任何符合 `std::ranges::range` 概念的东西：

```cpp
int arr[] = {3, 1, 4};
std::ranges::sort(arr);                       // 原生数组也行

std::ranges::find_if(v, [](int i) { return i > 4; });
// ranges::find_if 同样返回迭代器（指向找到的元素），
// 用 ranges::end(v) 判断是否没找到
```

:::tip 迭代器知识没作废
注意 `ranges::find_if` 仍然返回一个迭代器——**这意味着上一篇讲的迭代器知识全部还有用**，迭代器失效、配对这些问题在 ranges 里依然存在，只是 Ranges 的接口让你更难犯这些错（不是消除，是变难）。C++26 里我们仍然需要迭代器。
:::

## views：惰性求值，Ranges 的灵魂

受约束算法只是开胃菜，Ranges 真正的杀手锏是 **views（视图）**。一个 view 是一种**惰性（lazy）**访问 range 的方式——它不拷贝数据、不预先计算结果，而是在你遍历它的时候，**一次处理一个元素**<RefLink :id="5" preview="cppreference, Ranges library — views are lazy" />。

对比一下两种风格。`std::ranges::sort(v)` 是**急切求值（eager）**——它立刻、当场把整个区间排好序，跑完才返回。而 `std::views::filter(...)` 是**惰性求值（lazy）**——它只是搭好一个「过滤管道」，什么计算都不做，直到你真正去遍历它，每遍历到符合条件的一个元素，才把它交给你。

```cpp
#include <ranges>
#include <vector>
#include <iostream>

std::vector<int> v{1, 2, 3, 4, 5, 6};

// 搭管道：此时 filter 一个元素都没处理
auto gt3 = v | std::views::filter([](int x) { return x > 3; });

// 遍历时才真正执行过滤
for (int x : gt3) {
    std::cout << x << ' ';   // 4 5 6
}
```

那个 `|` 是**管道操作符（pipe operator）**，借鉴自 Unix 管道——把左边的 range 喂给右边的 view 适配器（range adaptor）。你可以把多个 view 串起来，像流水线一样组合：

```cpp
auto result = v
    | std::views::filter([](int x) { return x > 1; })    // 过滤
    | std::views::transform([](int x) { return x * x; }) // 变换
    | std::views::take(3);                                // 只取前 3 个
// 遍历 result 时：3²=9, ... 一路惰性求值
```

## 实验：eager vs lazy，到底差多少

光说「惰性更省」不够直观，我们上基准。造一个一千万个元素的 `vector`，比较两种写法：**eager**——先用 `ranges::to` 把过滤结果物化成一个临时 `vector`，再遍历求和；**lazy**——直接遍历 `views::filter`，不建临时容器。

```cpp
#include <algorithm>
#include <ranges>
#include <vector>
#include <numeric>
#include <chrono>
#include <iostream>

int main()
{
    constexpr int N = 10'000'000;
    std::vector<int> v(N);
    std::iota(v.begin(), v.end(), 0);
    const auto pred = [](int x) { return x > N / 2; };

    // EAGER：物化过滤结果到一个临时 vector，再求和
    long long se = 0;
    auto t0 = std::chrono::high_resolution_clock::now();
    {
        auto tmp = v | std::views::filter(pred) | std::ranges::to<std::vector<int>>();
        for (int x : tmp) se += x;
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    // LAZY：直接遍历 view，不建临时容器
    long long sl = 0;
    auto t2 = std::chrono::high_resolution_clock::now();
    for (int x : v | std::views::filter(pred)) sl += x;
    auto t3 = std::chrono::high_resolution_clock::now();

    auto ms_e = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    auto ms_l = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
    std::cout << "sum eager=" << se << " lazy=" << sl << "\n";
    std::cout << "eager (ranges::to 临时 + 求和): " << ms_e << " ms\n";
    std::cout << "lazy  (直接遍历 view):       " << ms_l << " ms\n";
}
```

GCC 16.1.1，`-std=c++23 -O2`：

```bash
❯ g++ -std=c++23 -O2 -Wall bench.cpp -o bench && ./bench
sum eager=37499992500000 lazy=37499992500000
eager (ranges::to 临时 + 求和): 23 ms
lazy  (直接遍历 view):       7 ms
```

两种写法算出来的和完全一致（`37499992500000`，校验通过），但 **eager 花了 23ms，lazy 只花了 7ms——快了 3 倍多**，而且 lazy 版本**没有分配那个几百万元素的临时 `vector`**。eager 慢就慢在两件事：一是要把五百万个符合条件的元素拷进临时 vector（一堆 `push_back` + 可能的扩容），二是多了一次完整的遍历（先物化、再求和，等于遍历两遍）。lazy 只遍历一遍，边过滤边求和，过滤掉的元素直接跳过，连拷贝的影子都没有。

:::tip 怎么亲眼看见「惰性」
想直观感受「管道搭好不执行、遍历才执行」，有个简单办法：在 filter 和 transform 的 lambda 里各加一句 `std::cout`，然后**只搭管道不遍历**——你会发现什么都不会打印。一旦你写 `for (auto x : pipeline)`，每个元素才会**走完整个管道再处理下一个**：第一个元素先过 filter、过了才进 transform、再进 take……是一个元素贯穿到底，而不是先把所有元素都 filter 完再 transform。这就是惰性执行模型，也是后面「短路」能成立的原因。
:::

## 无限 range：惰性启用的魔法

惰性求值解锁了一个很酷的能力——**无限 range**。如果求值是急切的，无限序列根本没法表达（你没法预先算出无穷多个元素）。但有了惰性，只要你不真正去遍历「无穷」，它就能存在。

`std::views::iota(x)` 从 `x` 开始，生成一个**无限递增**的序列<RefLink :id="6" preview="cppreference, std::views::iota — infinite counting range factory (C++20)" />。配合 `take` 截断，就能安全使用：

```cpp
// 生成 0², 1², 2², ... 的前 5 个
for (int x : std::views::iota(0)
            | std::views::transform([](int n) { return n * n; })
            | std::views::take(5)) {
    std::cout << x << ' ';
}
```

```bash
❯ g++ -std=c++23 -O2 iota.cpp -o iota && ./iota
0 1 4 9 16
```

`iota(0)` 本身是无限的（0, 1, 2, 3, ...），但 `take(5)` 把它截断成 5 个元素。惰性求值保证：`take` 之外的无限部分**永远不会被求值**。这种「定义一个无限的源，再用 view 限定用到多少」的模式，在处理流式数据、生成序列时非常顺手。`iota` 是 C++20 就有的 range 工厂。

## 管道短路：lazy 带来的效率

惰性的另一个直接收益是**短路（short-circuiting）**。当你把多个 filter 串起来时，一个元素只要在某一关被过滤掉，**后面的关卡完全不会处理它**——因为它是「一个元素贯穿到底」的执行模型。

Shah 举的例子是过滤字符串集合：先筛「以 M 开头」，再筛「长度大于 4」。如果一个字符串不以 M 开头，它第一个 filter 就被拦下了，第二个 filter 的谓词**根本不会被调用**。我们来量化一下这个效果——给 filter 的谓词加个计数器，对比「全量遍历」和「加 `take(5)` 提前终止」时谓词被调用的次数：

```cpp
long long calls_all = 0, calls_take = 0;
auto cp_all  = [&](int) { ++calls_all;  return true; };
auto cp_take = [&](int) { ++calls_take; return true; };

for ([[maybe_unused]] int x : v | std::views::filter(cp_all)) {}
for ([[maybe_unused]] int x : v | std::views::filter(cp_take) | std::views::take(5)) {}

std::cout << "filter 谓词调用次数: 全量=" << calls_all
          << "  加 take(5)=" << calls_take << "\n";
```

在一千万个元素的 `v` 上：

```bash
filter 谓词调用次数: 全量=10000000  加 take(5)=6
```

**一千万次 vs 6 次**。加了 `take(5)` 之后，谓词只被调用了 6 次（取到 5 个元素需要判断 6 次）就停了，剩下的一千万次求值全部被惰性短路掉。如果你只关心「前几个满足条件的元素」，这种写法比「先过滤出一个完整列表再取前 5 个」快了不止一个数量级——因为后者（eager）必须把所有元素都过一遍谓词。

## ranges::to：把惰性结果物化回容器（C++23）

views 是惰性的，但很多时候你最后还是想要一个**实实在在的容器**（比如要多次随机访问、要传给只收容器的接口）。把 view 物化成容器，就是 `std::ranges::to` 的活：

```cpp
auto collected = std::vector{1, 2, 3, 4, 5, 6}
    | std::views::filter([](int x) { return x % 2 == 0; })
    | std::ranges::to<std::vector<int>>();
// collected == {2, 4, 6}
```

```bash
❯ ./ranges_to_demo
ranges::to (evens): 2 4 6
```

:::warning 这里有个版本陷阱，Shah 漏标了
Shah 在演讲里说「我们有了 `ranges::to`」，语气像是它和受约束算法一起、从 C++20 就有。**不是。** `std::ranges::to` 是 **C++23** 才进标准的（提案 P1206R7，特性测试宏 `__cpp_lib_ranges_to_container=202202L`）<RefLink :id="7" preview="cppreference, std::ranges::to (since C++23) — P1206R7" />，比 C++20 的受约束算法晚了一个版本。

我用同一个程序在两个标准下编译，结果一目了然：

```cpp
auto col = v | std::views::filter(pred) | std::ranges::to<std::vector<int>>();
```

```bash
❯ g++ -std=c++20 probe.cpp
probe.cpp:12:78: error: ‘to’ is not a member of ‘std::ranges’
   12 |     ... | std::ranges::to<std::vector<int>>();
      |                                              ^~

❯ g++ -std=c++23 probe.cpp && echo OK
OK
```

`-std=c++20` 直接报 `'to' is not a member of 'std::ranges'`，`-std=c++23` 才编得过。所以如果你的工程还在 C++20，`ranges::to` 用不了——得手动 `reserve` + 循环 `push_back`，或者用 `std::copy` 配 inserter。最低工具链版本大概是 GCC 14 / Clang 18+libc++ / MSVC VS2022 17.5。

:::tip 管道支持也是 C++23，不是「后来补的」
`r | ranges::to<C>()` 这种管道写法，来自提案 P2387R3。它和 P1206 是**同期在 C++23 一起落地**的，不是「先有 `ranges::to`、后来才补上管道」。所以你不用担心「管道版是个补丁」——它从一开始就是 C++23 的完整部分。
:::
:::

## views 速查表：哪个是哪个标准来的

这是本篇的另一个二创重点。views 在 C++20 之后还在持续膨胀，C++23 加了一大票，C++26 还在加。Shah 在演讲里笼统地把 `drop_while`、`chunk_by`、`zip`、`zip_transform` 都叫「新东西」，但**没标版本**——这几个其实分属不同标准，搞混了会编不过。我把 cppreference 上核对过的版本归属列出来：

| 标准 | views（代表性） |
|------|------|
| **C++20** | `filter`、`transform`、`take`、`drop`、`take_while`、`drop_while`、`reverse`、`join`、`split`、`keys`、`values`、`elements`、`iota`（无限）、`lazy_split`、`common`、`counted`、`all` |
| **C++23** | `zip`、`zip_transform`、`chunk`、`chunk_by`、`slide`、`join_with`、`stride`、`cartesian_product`、`as_const`、`as_rvalue`、`enumerate`、`adjacent`、`adjacent_transform`、`pairwise`、`pairwise_transform`、`repeat`（工厂） |
| **C++26** | `cache_latest`（另有 `concat`、`as_input`、`indices` 等在推进） |

:::warning 几个容易记错的版本

- **`drop_while` 是 C++20**，不是 C++23——别因为它「看起来新」就归到 23。
- **`chunk_by`、`zip`、`zip_transform` 是 C++23**（`zip`/`zip_transform` 来自 P2210，`chunk_by` 来自 P2442）<RefLink :id="8" preview="cppreference, std::views::zip / chunk_by — C++23, P2210 / P2442" />，需要 `-std=c++23`。
- **`as_rvalue` 是 C++23**，特别容易被误记成 C++26——因为它听起来「很新」，其实是和 zip 那批一起进来的。
- **`join` 是 C++20，但 `join_with` 是 C++23**——别把带 `_with` 的版本当成 C++20。
:::

我们实测跑几个 C++23 的 view，感受一下它们的威力。`chunk_by` 按连续相等的元素分组：

```cpp
std::vector<int> run{1, 1, 2, 3, 3, 3, 4, 5};
for (auto ch : run | std::views::chunk_by([](int a, int b) { return a == b; })) {
    std::cout << '[';
    for (int x : ch) std::cout << x;
    std::cout << ']';
}
```

```bash
❯ g++ -std=c++23 -O2 chunk.cpp -o chunk && ./chunk
[11][2][333][4][5]
```

连续相等的元素被各自分到了一组。`zip` 则是把多个 range「拉链」式并行遍历，长度取最短的那个：

```cpp
std::vector<int>  a{1, 2, 3};
std::vector<char> b{'x', 'y', 'z'};
for (auto [x, y] : std::views::zip(a, b)) {
    std::cout << '(' << x << y << ')';
}
```

```bash
❯ ./zip_demo
(1x)(2y)(3z)
```

以前要并行遍历两个容器，你得手写两个下标、担心越界；`zip` 把这件事变成了一行管道，还能直接用结构化绑定解包。这些 C++23 新 view 大大拓宽了「用管道表达数据处理流水线」的能力边界。

## 自定义迭代器：迭代器就是个「可替换前进逻辑的伪指针」

:::tip 这一小节是进阶，可跳过
如果你想更扎实地理解「迭代器到底是什么」，可以自己手写一个。下面是一个最小化的单向链表节点迭代器——它证明了：**迭代器的本质就是一个「能 `++`、能 `*`、能比较」的对象，前进逻辑完全可替换。**
:::

```cpp
struct Node
{
    int data;
    Node* next;
};

struct NodeIterator
{
    Node* current;

    int& operator*() const { return current->data; }
    NodeIterator& operator++() { current = current->next; return *this; }
    bool operator!=(const NodeIterator& other) const { return current != other.current; }
};
```

只要这四个操作齐了（解引用、前置 `++`、不等比较、可默认构造/拷贝），它就能当 forward iterator 用，塞进 range-based for、塞进受约束算法。容器内部是链表、是树、是图，对外都可以伪装成「一个能一步步走的伪指针」。这就是迭代器抽象的力量——也是为什么 Ranges 选择在迭代器之上构建，而不是另起炉灶。

## 坑位清单：用 Ranges 也要留神

最后把本系列三篇里散落的坑位集中一下，方便你复习。Ranges 让很多错误**变难犯**了，但没消灭它们：

1. **`std::advance` 不做边界检查**——越界即段错误，泛型代码里先 `std::distance` 判断。
2. **`begin`/`end` 必须来自同一个容器**——`process(f().begin(), f().end())` 是 UB，存进具名变量。
3. **`list`/`set` 迭代器不支持 `+n`/`-n`**——排序用成员 `sort()`，别硬塞 `std::sort`。
4. **view 不拥有数据**——它只是底层 range 的一个视图，底层容器一旦失效（扩容、rehash、析构），view 就悬空了。**别让 view 的生命周期超过它观察的容器。**
5. **`ranges::to` 没有 `take` 兜底会吃光内存**——把一个无限的 `iota` 直接 `ranges::to<vector>()` 会无限物化，内存撑爆；务必先 `take` 限定。
6. **`reverse` 配合单遍迭代器的 view 可能编不过**——有些 view 要求双向迭代器，单向的 `forward_list` 视图上用 `reverse` 会编译失败。
7. **算法报错不一定更短**——ranges 用 concepts 拦截错误更早、更准，但深嵌套约束的报错可能很长；真正的收益是「写不出某些 bug」，不是「报错行数少」。

## 三篇走下来，我们搞清楚了什么

从第一篇的下标循环，到这一篇的 views 管道组合，我们把 C++「遍历与处理数据」的抽象演进走了一遍。这一篇的核心可以浓缩成几点：受约束算法让你**少传参数、少配错迭代器对**；views 的惰性求值是 Ranges 的灵魂——它**不拷贝、不预计算，遍历时一个元素贯穿整个管道**，实测比 eager 物化快 3 倍多（7ms vs 23ms）、内存还省；惰性启用了**无限 range**（`iota`）和**短路**（加 `take(5)` 让谓词调用从一千万次降到 6 次）；`ranges::to` 把惰性结果物化回容器，但**它是 C++23**，别被「我们有了 ranges::to」的口气误导；views 还在持续进化，`chunk_by`/`zip`/`zip_transform` 都是 C++23，`cache_latest` 等是 C++26。

回头看 Shah 那句「算法本质上就是循环」——现在我们能补完它了：现代 C++ 的目标，正是**让你不用亲手写那些循环**。用受约束算法替代手写排序/查找循环，用 views 管道替代「过滤→变换→收集」的多趟循环，让代码更接近「描述你要什么」而不是「描述怎么做」。这正是 Ranges 的设计哲学。

如果你想继续往深里走，有几个方向：vol4 的 concepts 文章能帮你理解 ranges 背后的约束体系；vol6 性能卷里的完美转发、SIMD 内容，和 views 的「避免不必要拷贝」一脉相承；cppreference 的 [Ranges library](https://en.cppreference.com/w/cpp/ranges) 和 [Constrained algorithms](https://en.cppreference.com/w/cpp/algorithm/ranges) 是最权威的速查表。Ranges 不完美——迭代器失效等问题它只是让你更难犯，但它确实让「写出更好、更安全、更高性能的数据处理代码」这件事，比 C++11 时代顺了一大截。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="Ranges library (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/ranges"
    chapter="sentinel 可与 iterator 不同类型"
  />
  <ReferenceItem
    :id="2"
    author="Eric Niebler"
    title="range-v3 (C++14 library)"
    :year="2014"
    url="https://github.com/ericniebler/range-v3"
    chapter="标准 Ranges 的原型"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="Concepts library (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/concepts"
    chapter="concepts 是 Ranges 落地的关键拼图"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Constrained algorithms (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/algorithm/ranges"
    chapter="传整个 range 而非迭代器对"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="Ranges library — Views (lazy)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/ranges"
    chapter="views 惰性求值"
  />
  <ReferenceItem
    :id="6"
    author="cppreference.com"
    title="std::views::iota (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/ranges/iota_view"
    chapter="无限计数 range 工厂"
  />
  <ReferenceItem
    :id="7"
    author="cppreference.com"
    title="std::ranges::to (since C++23)"
    :year="2024"
    url="https://en.cppreference.com/w/cpp/ranges/to"
    chapter="P1206R7 / __cpp_lib_ranges_to_container=202202L"
  />
  <ReferenceItem
    :id="8"
    author="cppreference.com"
    title="std::views::zip / zip_transform / chunk_by (C++23)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/ranges/zip_view"
    chapter="P2210 (zip) / P2442 (chunk_by)"
  />
  <ReferenceItem
    :id="9"
    author="WG21"
    title="P2387R3: Pipe support for user-defined range adaptors"
    :year="2022"
    url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2387r3.html"
    chapter="range_adaptor_closure（C++23 同期落地）"
  />
  <ReferenceItem
    :id="10"
    author="Mike Shah"
    title="Back to Basics: C++ Ranges — CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=Q434UHWRzI0"
  />
</ReferenceCard>
