---
chapter: 7
cpp_standard:
- 11
- 20
description: 讲透 STL 三类迭代器适配器——back_inserter 怎么把赋值变成 push_back、front_inserter 为何用不了 vector、reverse_iterator 的 base() 为何差一位，以及适配器「长得像迭代器就能塞进算法」的本质
difficulty: intermediate
order: 41
platform: host
prerequisites:
- 迭代器基础与 category
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 12
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- intermediate
- Ranges
title: 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
---

# 迭代器适配器：反向、插入与流，把现成迭代器改出新行为

上一篇我们把迭代器和它的 category 串了一遍：迭代器是容器和算法之间那层统一接口，还分强弱等级。这一篇接着解决一个你一定会撞上的实战痛点。

假设要把一个 `deque` 里的元素追加到另一个 `deque` 末尾。第一反应大概是 `std::copy`：

```cpp
std::deque<int> d1{1, 2, 3, 4, 5};
std::deque<int> d2;   // 空的
std::copy(d1.begin(), d1.end(), d2.end());   // 想追加到末尾？
```

这一行直接是**未定义行为**。`d2.end()` 是个"past-the-end"位置，`copy` 会老老实实把元素往这个越界位置上写——它只负责"把元素赋值到目标迭代器指向的位置"，压根不管目标容器有没有那个空间。算法不扩容，这是 STL 的铁律。

那怎么办，自己手写一个循环 `for` 着 `push_back` 吗？能用，但不够优雅——我们明明在用算法，却因为"目标不会长"被逼回手写循环。标准库给了一个更聪明的解法：**别换算法，换个迭代器**。给它一个"接到赋值就自动往容器里塞"的迭代器，`copy` 还是那个 `copy`，痛点就没了。

这就是**迭代器适配器**干的事：不新建容器，把现成的迭代器（或容器）包一层，改造成新的行为。STL 配了三类现成的——反向、插入、流。这一篇我们把三类都拆开跑一遍，顺带把"适配器凭什么能这么干"的本质讲透。

## 反向迭代器：把 `++` 变成 `--`

最直观的一类。`rbegin()` / `rend()` 返回的是 `reverse_iterator`，它把底层迭代器的 `++` / `--` 语义整个反过来：`++` 往前退、`--` 往后走。于是从头到尾的反向遍历，一行循环就出来了：

```cpp
std::vector<int> v{1, 2, 3, 4, 5};
std::cout << "rbegin/rend 反向遍历: ";
for (auto it = v.rbegin(); it != v.rend(); ++it) std::cout << *it << ' ';
std::cout << '\n';
```

用 `g++ -std=c++20 -O2`（本机 GCC 16.1.1）跑出来：

```text
rbegin/rend 反向遍历: 5 4 3 2 1
```

反向迭代器最实用的搭配是排序。`std::sort` 默认升序，但把反向迭代器喂给它，升序排出来的元素是"反着写回去"的，效果就是降序——不用再写自定义比较器：

```cpp
std::vector<int> s{3, 1, 4, 1, 5, 9, 2, 6};
std::sort(s.rbegin(), s.rend());
// s 现在: 9 6 5 4 3 2 1 1
```

这里有个伏笔先埋下：`reverse_iterator` 内部其实存了一个"正向位置"，但它解引用访问的并不是这个位置，而是这个位置的**前一个**。这个设计直接决定了后面要讲的 `base()` off-by-one 坑，我们先记着，等下拿实测验证。

## 插入迭代器：把"赋值"变成"插入"

回到开头那个 `copy` 越界的痛点。把目标从 `d2.end()` 换成 `std::back_inserter(d2)`，问题消失：

```cpp
std::deque<int> d1{1, 2, 3, 4, 5};
std::deque<int> d3;   // 空的
std::copy(d1.begin(), d1.end(), std::back_inserter(d3));
// d3 现在: 1 2 3 4 5
```

`back_inserter` 返回的是一个"插入迭代器"，它把"对它赋值"这个动作翻译成了容器的 `push_back`。空容器也能接，因为每次赋值都会让容器长一格。再 `copy` 一次，是在原来的基础上**追加**，不是覆盖：

```text
back_inserter 追加到空 d3: 1 2 3 4 5
再 back_inserter 一次: 1 2 3 4 5 1 2 3 4 5
```

插入迭代器有三兄弟，区别只在"塞到哪个位置"：

- `back_inserter(c)` —— 调 `push_back`，塞到末尾；
- `front_inserter(c)` —— 调 `push_front`，塞到开头；
- `inserter(c, it)` —— 调 `insert`，塞到 `it` **之前**。

`front_inserter` 有个反直觉的地方：因为每个新元素都插到最前面，后插的反而排前面，整体顺序会反过来：

```cpp
std::deque<int> d4;
std::copy(d1.begin(), d1.end(), std::front_inserter(d4));
// d4 现在: 5 4 3 2 1（d1 是 1 2 3 4 5，反过来了）
```

`inserter` 则是在指定位置之前插入。注意是"之前"——`it` 指向 20，那新元素就排到 20 的前面：

```cpp
std::deque<int> d5{10, 20, 30};
auto pos = d5.begin() + 1;   // 指向 20
std::copy(d1.begin(), d1.end(), std::inserter(d5, pos));
// d5 现在: 10 1 2 3 4 5 20 30
```

### 三兄弟各自的容器要求

这里有个真实的坑。`back_inserter` 调 `push_back`，`front_inserter` 调 `push_front`——可并不是所有容器都有这两个成员。`push_back` 几乎人人有（`vector`、`deque`、`list` 都行），但 `push_front` 只有 `deque` 和 `list` 有，`vector` 没有。

所以把 `front_inserter` 套到 `vector` 上，连编译都过不了：

```cpp
std::vector<int> v;
int src[]{1, 2, 3};
std::copy(std::begin(src), std::end(src), std::front_inserter(v));
```

```text
/usr/include/c++/16.1.1/bits/stl_iterator.h:819:20:
  error: ‘class std::vector<int>’ has no member named ‘push_front’
```

报错直白：`vector` 压根没有 `push_front`。这其实符合上一篇讲过的道理——`vector` 是连续存储，往头部插要搬动后面所有元素，O(n) 操作太贵，标准库干脆不给这个接口。想要头插，换 `deque` 或 `list`。

`inserter` 没这个限制，任何有 `insert` 的容器都能用（基本就是所有序列容器），代价是中间插入的复杂度由容器决定（`vector` 是 O(n)，`list` 是 O(1)）。

### 小应用：保序插入

插入迭代器配合算法能写出很干净的代码。一个常见需求是"往一个有序 `vector` 里插一个新元素，插完还是有序的"。思路是用 `std::lower_bound` 找到第一个"不小于新值"的位置，再用 `inserter`（或直接 `insert`）插进去：

```cpp
std::vector<int> sorted{1, 3, 5, 7, 9};
int new_val = 4;
auto it = std::lower_bound(sorted.begin(), sorted.end(), new_val);
sorted.insert(it, new_val);
// sorted 现在: 1 3 4 5 7 9
```

这是 `<algorithm>` 和容器协作的经典小技法——把 O(n) 的"逐个比较找位置"压成 O(log n) 的二分查找（搬运那一下 O(n) 躲不掉，因为是连续存储）。完整的算法总览我们放到下一篇展开，这里先借它感受一下"算法 + 适配器 + 容器"三者怎么咬合。

## 流迭代器：把流当序列遍历

第三类是把 I/O 流也包装成迭代器。

`ostream_iterator` 把"对它赋值"翻译成"往流里写一个值 + 一个分隔符"。于是把容器内容打印到 `cout`，一行 `copy` 就行：

```cpp
std::cout << "ostream_iterator 打印: ";
std::copy(d1.begin(), d1.end(), std::ostream_iterator<int>(std::cout, ", "));
std::cout << '\n';
```

```text
ostream_iterator 打印: 1, 2, 3, 4, 5,
```

注意末尾多出来的那个分隔符——分隔符是在**每次写入之后**追加的，所以最后一个元素后面也会跟一个。想干净收尾的话，得自己处理末尾，或者用 `std::format` / 范围 `for` 循环代替。

反方向的 `istream_iterator` 把输入流当成一个"可读序列"。它的妙处在于配一个**默认构造的哨兵**表示流结束（EOF）：你不用提前知道流里有几个元素，读到 EOF 哨兵自动终止。下面从字符串流里读一堆 `int` 进 `vector`：

```cpp
std::istringstream iss("10 20 30 40 50");
std::vector<int> from_stream{
    std::istream_iterator<int>(iss),
    std::istream_iterator<int>()};   // 默认构造 = EOF 哨兵
// from_stream: 10 20 30 40 50
```

::: warning 别被老资料带歪
有些教程和笔记把输入流迭代器写成 `istream_adapter`——标准库里**没有**这个名字，正确的是 `istream_iterator`。这类笔误在网上转载的文章里很常见，照抄会编不过。
:::

这个"迭代器 + 哨兵"的模式，正是上一篇讲 category 时提到的：`istream_iterator` 是典型的 **input_iterator**，只能单遍往前读。哨兵机制让算法能处理"长度事先不确定"的序列——流的长度要读到末尾才知道，靠的就是这个 EOF 哨兵。

## 适配器凭什么能这么干：剥开看一层

讲到现在你可能会好奇：`back_inserter` 返回的对象，凭什么能塞进 `std::copy` 当目标？`copy` 又不认识什么"插入迭代器"。

答案正是上一篇那句核心论点的延伸——**算法只认迭代器接口，不认具体类型**。`copy` 对目标迭代器的全部要求，就是"能解引用赋值、能 `++`"（也就是满足 output_iterator 的语义）。只要某个对象支持这两个操作，`copy` 就把它当迭代器用，至于这个对象背后到底是真的内存位置、还是偷偷调了 `push_back`，`copy` 根本不关心。

把标准库的包装剥掉，`back_insert_iterator` 的全部"魔法"就这点：

```cpp
// Standard: C++20
template <typename Container>
class BackInsertIterDemo {
    Container* c_;
public:
    explicit BackInsertIterDemo(Container& c) : c_{&c} {}
    // 赋值 = push_back：这就是"赋值即插入"的全部秘密
    BackInsertIterDemo& operator=(const typename Container::value_type& v) {
        c_->push_back(v);
        return *this;
    }
    BackInsertIterDemo& operator*() { return *this; }      // 解引用返回自己
    BackInsertIterDemo& operator++() { return *this; }     // ++ 是空操作
    BackInsertIterDemo operator++(int) { return *this; }
};
```

`operator=` 重载成了 `push_back`，`*` 和 `++` 都是返回自己的空操作——凑齐了 output_iterator 要的三件套。于是它能被任何要 output_iterator 的算法直接用，**算法一个字都不用改**：

```cpp
std::vector<int> v;
int src[]{1, 2, 3, 4, 5};
std::copy(std::begin(src), std::end(src), BackInsertIterDemo(v));
// v 现在: 1 2 3 4 5
```

跑出来正是 `1 2 3 4 5`。这就是适配器的本质：**一个"长得像迭代器、背后挂别的行为"的对象**。STL 当年"容器和算法靠迭代器解耦"的设计决定，到这里威力才真正显现——不光容器的迭代器能进算法，连这种"伪装成迭代器"的小对象也能。

顺着这个思路，标准库还有个 `move_iterator`（C++11 引入，C++20 随 ranges 做了改进）：它把"解引用得到左值引用"变成"解引用得到右值引用"，套在源区间上，`copy` 就变成了 `move`——元素被搬走而不是拷贝。背后的机制和上面一模一样：包一层，换一种解引用行为。移动语义那卷我们会专门讲它，这里知道有这么个同类就行。

## 几个真实容易踩的点

把这一路容易翻车的位置集中收一下，每个都是上面实测验证过的：

::: warning 反向迭代器的 base() 差一位
`reverse_iterator` 有个 `base()` 成员，返回它包裹的正向迭代器。但 `*rit` 访问的**不是** `rit.base()`，而是 `rit.base() - 1`：

```text
*rit            = 40
*rit.base()     = 50
*(rit.base()-1) = 40
```

这就是开头埋的伏笔。后果是：当你想用正向迭代器去 erase 一个由反向迭代器界定的范围时，端点要写成 `(rit+1).base()` 而不是 `rit.base()`，否则差一格。记不住没关系，记住"反向解引用访问的是 base 的前一位"就不会错。
:::

::: warning front_inserter 挑容器
`front_inserter` 只能用在有 `push_front` 的容器上，也就是 `deque` 和 `list`。`vector`、`array`、`string` 都没有 `push_front`，套上去直接编译失败（见上面的真实报错）。想要头插，换容器。
:::

::: warning inserter 插在"之前"
`inserter(c, it)` 是把元素插到 `it` **之前**，不是替换 `it` 指向的元素。而且连续 `inserter` 插入时，插入点会跟着后移（因为前面插了东西），行为和 `back_inserter` 的"追加"不一样，用时心里有数。
:::

::: warning ostream_iterator 末尾多个分隔符
分隔符在每次写入后追加，所以输出末尾会多一个。要干净的逗号分隔，别用它，用 `std::format` 或循环手动处理边界。
:::

## 小结

迭代器适配器的思路其实就一句话——**不换算法、不新建容器，换个"会变形"的迭代器**。几条关键结论收一下：

- 三类现成的：`reverse_iterator`（`rbegin`/`rend`，反向遍历、配合 `sort` 拿降序）、插入迭代器（`back_inserter`/`front_inserter`/`inserter`，把赋值变插入）、流迭代器（`ostream_iterator`/`istream_iterator`，流与序列互转）。
- 插入迭代器各有所求：`back_inserter` 要 `push_back`（几乎都有）、`front_inserter` 要 `push_front`（只有 `deque`/`list`）、`inserter` 要 `insert`（序列容器都有）。
- 适配器的本质是"长得像迭代器、背后挂别的行为"——满足 output_iterator 的语义（解引用赋值 + `++`）就能塞进任何算法，算法一个字不用改。
- 四个高频坑：`reverse_iterator::base()` 差一位、`front_inserter` 用不了 `vector`、`inserter` 插在之前、`ostream_iterator` 末尾多分隔符。

下一篇我们正式进算法这一块——把 `<algorithm>` 那一大家子按"非修改式 / 修改式 / 排序 / 查找"理一遍，看面对一个具体问题该怎么挑工具。

## 参考资源

- [cppreference: Iterator adaptors](https://en.cppreference.com/w/cpp/iterator#Iterator_adaptors) —— 三类适配器总览
- [cppreference: std::back_insert_iterator](https://en.cppreference.com/w/cpp/iterator/back_insert_iterator) —— `back_inserter` 的返回类型与"赋值即 push_back"机制
- [cppreference: std::reverse_iterator](https://en.cppreference.com/w/cpp/iterator/reverse_iterator) —— `base()` 与解引用的 off-by-one 关系
- [cppreference: std::istream_iterator](https://en.cppreference.com/w/cpp/iterator/istream_iterator) —— 流迭代器与 EOF 哨兵
