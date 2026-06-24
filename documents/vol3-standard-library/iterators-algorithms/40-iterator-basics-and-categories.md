---
chapter: 7
cpp_standard:
- 11
- 20
description: 讲透迭代器 category：迭代器是指针的泛化、容器和算法之间的通用接口，五层强弱等级（含 C++20 contiguous）如何决定能用哪些算法、如何靠编译期标签派发影响 std::distance 性能，以及为什么 std::sort 用不了 std::list
difficulty: intermediate
order: 40
platform: host
prerequisites:
- vector 深入：三指针、扩容与迭代器失效
- array：编译期固定大小的聚合容器
reading_time_minutes: 10
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- intermediate
- Ranges
title: 迭代器基础与 category：容器和算法靠什么对接
---

# 迭代器基础与 category：容器和算法靠什么对接

容器这一路我们走完了——`array`、`vector`、`list`、`map`，能存数据的家伙基本到齐。可一旦想把它们交给 `std::sort`、`std::find`、`std::transform` 这些算法，一个有意思的问题就冒出来了：`std::sort` 凭什么对 `vector` 和 `array` 都能用，对 `list` 却连编译都过不了？算法里又没写死认哪个容器。

答案藏在容器和算法中间那层薄薄的通用接口上——迭代器。这篇我们就把迭代器拆开看：它到底是什么、为什么有「强弱等级」（也就是 category），以及这个等级怎么在编译期就决定了一段代码能不能跑、跑得快不快。

## 迭代器是什么：把指针的用法泛化

先回到最熟悉的指针。给定一个数组，我们能用 `*p` 取值、`++p` 前进一位、`p != end` 判断到没到头——这三板斧就能从头遍历到尾。迭代器干的事，就是把「这套指针的用法」抽象出来：只要某个类型支持解引用、自增、比较，它就能当迭代器用，至于它背后是连续数组、链表节点还是别的什么结构，算法根本不用关心。

换句话说，裸指针就是一种「原生迭代器」，而 `vector::iterator`、`list::iterator` 这些，是「长得像指针、但背后挂着各自容器」的迭代器。算法只认这套统一接口，所以一份 `std::find` 能通吃所有容器。这就是 STL 当年最关键的设计决定：**容器和算法解耦，靠迭代器这层接口对接**。

## category：迭代器有强弱等级

「支持解引用和自增」只是最低门槛。不同的迭代器能做的事差很多：有的只能往前走、还只能读一遍；有的能随机跳到任意位置。能做的操作越多，这个迭代器的「等级」就越高，标准里管这叫 iterator category。

从弱到强，经典是这么几层，后一层总在前一层基础上加能力（这是 C++20 之前的老五类，加上 C++20 新增的最强一类）：

- **input**：能读、能 `++`、能比相等，但只能单遍往前走（典型如 `istream_iterator`）。
- **forward**：在 input 基础上允许多遍遍历（典型如 `forward_list`）。
- **bidirectional**：再加 `--`，能往回退（典型如 `list`、`set`、`map`）。
- **random_access**：再加 `+n`、`[]`、大小比较，能随机跳转（典型如 `vector`、`deque`、裸指针）。
- **contiguous**（C++20 新增）：在 random_access 基础上，还保证元素在内存里连续存放（典型如 `vector`、`array`、`string`、裸指针）。

另外还有个 **output**，专门只写不读，单列在外。

光说层级有点虚，我们直接拿 C++20 的 concept 在编译期判一下，各种容器的迭代器到底落在哪一档。concept 是 C++20 给的编译期谓词，`std::random_access_iterator<T>` 为真，就说明 `T` 满足随机访问迭代器的全部要求，跑不了题。思路很直白：写一个 `print_row` 模板，对每个容器的迭代器依次查 `input_iterator` / `forward_iterator` / `bidirectional_iterator` / `random_access_iterator` / `contiguous_iterator` 五个谓词，把是/否打印成一行——点开下面这个在线示例直接运行，看真实的判定结果：

<OnlineCompilerDemo
  title="用 C++20 concept 给迭代器量等级"
  source-path="code/examples/vol3/40_iterator_categories.cpp"
  description="print_row 对 vector/array/string/裸指针/list/set/forward_list 的迭代器逐一查五个 concept 谓词，是/否打印成表，强弱一目了然"
  allow-run
/>

运行结果把层级关系讲得很直白：`vector`、`array`、`string` 还有裸指针五项全亮，是能在内存里随机跳转、还连续存放的最强一类（contiguous）；`list` 和 `set` 到 bidirectional 为止——能前后走，但不能 `it + 5` 一下跳过去；`forward_list` 最弱，只能单向往前。强弱不是「谁写得更好」，而是数据结构本身决定的：链表的节点在内存里东一个西一个，你压根没法 `it + n` 直接算出第 n 个节点的地址。

## 为什么 category 重要：它决定能用哪些算法

回到开头那个问题。算法在标准里都写明了对迭代器 category 的要求：`std::find` 只要 input（顺着找就行），`std::reverse` 要 bidirectional（得往回走），`std::sort` 要 random_access（快排得随机跳转取 pivot、做分区）。这些要求不是文档里写写而已——传进去的迭代器达不到，直接编译失败。

所以把 `std::sort` 套到 `std::list` 上，会撞墙：

```text
=== std::sort 要求 random_access_iterator ===
  vector::iterator 是 random-access? 是
  list::iterator   是 random-access? 否
```

`list` 的迭代器只到 bidirectional，够不着 random_access，`std::sort` 用不了。那链表就没办法排序了吗？有，只不过它走自己的路——成员函数 `list::sort()`，内部用归并排序，天然适合链表（归并不需要随机访问，只要能前后走和拆分），复杂度同样是 O(n log n)：

```text
  vector 用 std::sort 后: 1 1 2 3 4 5 6 9
  list 用 list::sort() 后: 1 1 2 3 4 5 6 9
```

这其实是个挺常见的坑：新手习惯了对啥容器都 `std::sort(c.begin(), c.end())`，在 `list` 上就编不过。记住一句话——**算法挑迭代器，不挑容器；容器提供什么等级的迭代器，决定了它能用哪些泛型算法**。

## category 还偷偷影响性能：编译期标签派发

category 不光管「能不能用」，还管「用起来多快」。看 `std::distance`，它返回两个迭代器之间的距离，对谁都给一样的结果，但复杂度不一样：

```text
=== std::distance(begin, end)（值相同，复杂度不同）===
  vector(10): 10   [random-access -> O(1)]
  list(10):   10   [bidirectional -> O(n)]
```

同样是 10 个元素，`vector` 那条是 O(1)，`list` 那条是 O(n)。差别在哪？`vector` 的迭代器是 random_access，`std::distance` 直接算 `last - first`，一步到位；`list` 只能 bidirectional，只能老老实实从头 `++` 数到尾，几个元素就走几步。

这件事是怎么做到对调用者完全透明、又零运行期开销的？靠的是 C++ 模板里一个经典手法——标签派发（tag dispatch）。每个迭代器类型都带一个「类别标签」，通过 `std::iterator_traits<It>::iterator_category` 能取到；`std::distance` 内部按这个标签选不同的函数重载：random_access 的版本走减法，其余版本走循环。这个选择发生在**编译期**，运行期根本不存在「先判断一下 category」这步开销。`std::advance`、`std::iter_swap` 等一堆设施都是这么干活的。

::: warning 容易踩的点
在 `list`、`set` 这种非随机访问容器上，凡是内部依赖「算距离」或「跳 n 步」的操作（比如 `std::distance`、`std::advance(it, n)`），都是 O(n)，别当成常数时间随手用，数据量一大就现原形。
:::

## C++20 视角：把「要求」从文档搬进类型系统

最后说一句 C++20 带来的变化。在 concept 出现之前，算法对迭代器的要求只能写在文档里（"requires ForwardIterator"），编译器不检查——你要是传了个达不到要求的迭代器，报出来的是一长串模板实例化错误，很难看出到底哪不对。

C++20 用 concept 把这些要求搬进了类型系统：`std::forward_iterator`、`std::random_access_iterator` 这些本身就是编译期可判定的谓词。前面那张表之所以能用代码打出来，正因为 concept 把「文档里的要求」变成了「编译期就能查的事实」。我们甚至能在自己的代码里直接 `static_assert(std::random_access_iterator<It>);` 卡住模板参数，传错类型在调用点就报错，信息清楚得多——上面在线示例里那个 `print_row` 模板，其实就是在用 concept 给迭代器「量等级」。

## 小结

我们从头到尾把迭代器和它的 category 串了一遍，几条关键结论收一下：

- 迭代器是指针用法的泛化，是容器和算法之间那层统一接口；算法只认迭代器，不认具体容器。
- 迭代器分强弱等级（category）：input → forward → bidirectional → random_access → contiguous（C++20 最强），由数据结构本身决定。
- category 决定两件事：能用哪些泛型算法（达不到要求就编译失败），以及某些操作的复杂度（靠编译期标签派发，零运行期开销）。
- 两个高频坑：`std::sort` 要 random_access，`list` 用不了（改用 `list::sort()`）；`std::distance` / `std::advance` 在非随机访问容器上是 O(n)。

下一篇我们会接着讲迭代器适配器（`reverse_iterator`、`insert_iterator` 这些），看怎么用现成工具把迭代器「改造」出新的行为。

## 参考资源

- [cppreference: Iterator library](https://en.cppreference.com/w/cpp/iterator) —— 迭代器总览与 category 定义
- [cppreference: std::iterator_traits](https://en.cppreference.com/w/cpp/iterator/iterator_traits) —— `iterator_category` 与标签派发的基石
- [cppreference: std::distance](https://en.cppreference.com/w/cpp/iterator/distance) —— 复杂度随 category 变化的官方说明
- [cppreference: std::contiguous_iterator (C++20)](https://en.cppreference.com/w/cpp/iterator#Iterator_concepts) —— C++20 iterator concepts 与最强一类 contiguous
