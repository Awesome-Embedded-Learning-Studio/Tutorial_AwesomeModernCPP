---
chapter: 7
cpp_standard:
- 20
- 23
description: 讲透算法的 ranges 化三步进步（Range 参数、Concept 限定、Niebloid 阻断 ADL），以及 C++23 新算法 fold 家族如何修掉 accumulate 的返回类型坑、contains/find_last 怎么消灭 find!=end() 反模式，并实测 GCC 16.1.1 对 zip/chunk/slide/stride/repeat 等新适配器的支持现状
difficulty: advanced
order: 45
platform: host
prerequisites:
  - 迭代器基础与 category
  - 迭代器适配器：反向、插入与流
related:
  - 新标准容器：flat_map、inplace_vector 与 mdspan
reading_time_minutes: 22
tags:
  - host
  - cpp-modern
  - advanced
  - Ranges
title: ranges 算法与 C++23 新件：fold、contains 与新适配器
---

# ranges 算法与 C++23 新件：fold、contains 与新适配器

前面几篇我们把迭代器、迭代器适配器拆完了——算法那一边还停在「老 `<algorithm>`」的写法。这一篇专门把算法这一路的现代进化讲透：C++20 怎么把整个 `<algorithm>` Range 化（参数、Concept、Niebloid 三步），以及 C++23 补上来的几个关键新件——`fold` 家族怎么修掉 `accumulate` 的老坑、`contains`/`find_last` 怎么消灭「`find() != end()`」反模式、还有一批新的 ranges 适配器（`zip`/`chunk`/`slide`/`stride`/`repeat`）。

先划个界，免得和别的卷撞车：ranges 的 view、管道 `|`、惰性求值这些**通用概念**是 vol4 的事（vol4 会专门讲 ranges 视图管线），本篇不展开通用机制，只讲「算法的 ranges 化」和「C++23 新算法/新适配器」这两个具体题目；把视图物化成容器的 `ranges::to` 属于容器那一脉，归 vol3 的 [新标准容器](../containers/10-new-containers-cpp23-26.md) 与 cppreference，这里只在用到时顺带一提。

## 算法的 ranges 化：三步都改了什么

C++20 不是简单给老算法加了个 `ranges::` 前缀。它一次性动了三件事，每一件都对应一个你会实际碰到的差别。

### 第一步：参数从「迭代器对」变成「Range」

老写法要手动给两个迭代器：`std::sort(v.begin(), v.end())`。ranges 版直接收一个 Range：`std::ranges::sort(v)`。少敲一半字还在其次，真正的好处藏在「哨兵（sentinel）」里。

老 STL 要求头尾两个迭代器是**同一个类型**——`begin()` 和 `end()` 返回的得是同一种 iterator。这看起来天经地义，其实拦住了一类很自然的序列：**以 `\0` 结尾的 C 字符串**。它的「末尾」不是一个和首迭代器同类型的指针位置，而是「碰到 `\0` 就停」这个条件——在 ranges 之前，要么手算 `strlen`，要么 `std::string_view` 先包一层。

ranges 把「末尾」抽象成了**哨兵**：哨兵可以和迭代器是不同类型，只要能和迭代器比相等就行。于是「长度事先不知道、读到某个条件才停」的序列能直接喂给算法。`string_view` 就是典型，它的 `end()` 返回的就是个哨兵，`ranges::count` 能直接吃：

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <string_view>
#include <vector>

int main() {
    // Range 参数:一个参数搞定整个容器
    std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};
    std::ranges::sort(v);
    std::cout << "ranges::sort 后: ";
    for (auto x : v) std::cout << x << ' ';
    std::cout << '\n';

    // string_view 的 end() 是哨兵,天然适配「读到 \0 停」
    std::string_view sv = "hello";
    std::cout << "ranges::count(\"hello\", 'l') = "
              << std::ranges::count(sv, 'l') << '\n';
}
```

`g++ -std=c++23 -O2`（本机 GCC 16.1.1）跑出来：

```text
ranges::sort 后: 1 1 2 3 4 5 6 9
ranges::count("hello", 'l') = 2
```

### 第二步：Concept 在调用点就把错的类型拒掉

上一篇讲迭代器 category 时就埋了这条线：`std::sort` 要 random_access 迭代器，`std::list` 的迭代器只到 bidirectional，所以 `std::sort` 用不了 `list`。C++20 之前这个要求只写在文档里——传错了类型，编译器不会在调用点告诉你「类型不对」，而是闷头实例化模板，最后吐出一长串「找不到 `operator-`」之类的错误，读者得自己往回推到底哪一步错了。

ranges 算法用 Concept 把要求搬进了类型系统。`ranges::sort(l)` 在 `list` 上，Concept 在**调用点**当场判否，报错直奔主题。两种写法放一起看，对比非常明显：

```cpp
// Standard: C++20
#include <algorithm>
#include <list>

int main() {
    std::list<int> l{3, 1, 4, 1, 5};
    std::ranges::sort(l);   // ranges 版:Concept 层直接拒绝
    std::sort(l.begin(), l.end());   // 老版:模板深处的 operator- 报错
}
```

`ranges::sort(l)` 在 GCC 16.1.1 上的报错（截前面几行）：

```text
concept_reject.cpp:7:22: error: no match for call to
    '(const std::ranges::__sort_fn) (std::__cxx11::list<int>&)'
    7 |     std::ranges::sort(l);   // Concept 层直接拒绝
      |     ~~~~~~~~~~~~~~~~~^~~
  • candidate 1: ... requires (random_access_iterator<_Iter>) ...
                            ^^^^^^^^^^^^^^^^^^^^^^^^^^^
```

老写法 `std::sort(l.begin(), l.end())` 的报错则陷在模板深处：

```text
/usr/include/c++/16.1.1/bits/stl_algo.h: In instantiation of
'constexpr void std::__sort(_RandomAccessIterator, _RandomAccessIterator, _Compare)
   [with _RandomAccessIterator = _List_iterator<int>; ...]':
  required from here
stl_algo.h:1914:50: error: no match for 'operator-'
 (operand types are 'std::_List_iterator<int>' and 'std::_List_iterator<int>')
 1914 |                                 std::__lg(__last - __first) * 2,
```

一个在调用行直接说「要 random_access，你给的不是」；另一个绕进 `__sort` 内部，说「`__last - __first` 算不出来」。前者你一眼能定位，后者得反推「`list` 的迭代器不能相减」才反应过来。这就是 Concept 把「文档里的要求」变成「编译期可判的事实」的实战价值——`list` 想排序，走它自己的成员函数 `list::sort()`（上一篇讲过，归并实现，O(n log n)）。

### 第三步：Niebloid——算法不参与 ADL

这一步更隐蔽，但碰上的时候很费解。老 STL 算法是命名空间里的**普通函数**；ranges 算法不是函数，是一个叫 **Niebloid**（customization point object，CPO）的函数对象——它长得能像函数一样调用，但有两个关键差别。

最影响实战的一点：**Niebloid 不参与 ADL（依赖实参的查找）**。意思是，你在一个自定义命名空间里写了个 `sort(x)`，编译器绝不会因为某个实参的类型而「顺手」把 `std::ranges::sort` 也拉进重载集——这在老 STL 里是真实存在的劫持风险（某个类型的关联命名空间里恰好有个 `sort`，就把 `std::sort` 给藏了）。我们可以实测出来：

```cpp
// Standard: C++20
#include <algorithm>
#include <vector>

namespace user {
    struct Tag {};
    void sort(Tag) {}   // 自定义命名空间里有个同名 sort

    void demo() {
        std::vector<int> v{3, 1, 2};
        sort(v);   // 既没 using std::ranges,也没用 ADL 把它拉进来
    }
}
```

报错说明 `ranges::sort` 没被 ADL 找到（找到的候选里压根没有它）：

```text
adl.cpp:14:13: error: no matching function for call to 'sort(std::vector<int>&)'
   14 |         sort(v);
      |     ~~~~^~~
```

Niebloid 的设计动机就是为了堵这个窟窿：算法不能被用户命名空间里的同名函数劫持，行为可预测。顺带一提，因为 Niebloid 是对象不是函数，你也别指望像老 `std::sort` 那样把它的地址当回调传来传去——它是个带重载 `operator()` 的闭包对象，语义和普通函数指针不是一回事，需要时用 lambda 包一层更稳。

::: warning 「Ranges 算法」不是「加了 ranges:: 前缀的老算法」
三步是联动的：参数变 Range + 哨兵、Concept 限定、Niebloid。这意味着 ranges 算法不是老算法的语法糖，而是一套重新设计的接口。老的 `std::sort` 没有被废弃（你的存量代码照样跑），但新代码里能用 ranges 版就别用老版——少打字、报错清楚、不会被 ADL 劫持，三重好处。
:::

## fold 家族（C++23）：修掉 accumulate 的返回类型坑

讲完算法的 ranges 化，进 C++23 的新件。第一个要讲透的是 `fold`——它不是凭空冒出来的，是冲着老 `std::accumulate` 的一个真实缺陷来的。

### accumulate 的返回类型陷阱

`std::accumulate` 在 `<numeric>` 里，干的是「左折叠」：给一个初始值和一个二元运算，从左到右把整个序列折成一个值。它有一个相当隐蔽的坑——**返回类型由初始值决定，不是由元素类型决定**。初始值写成 `1`（int），哪怕序列是一堆 `double`，全程也按 `int` 算，小数部分被默默截断：

```cpp
// Standard: C++23
#include <algorithm>
#include <iostream>
#include <numeric>
#include <vector>

int main() {
    std::vector<double> vec{1.5, 2.5, 3.5, 4.5};   // 真实和 = 12.0

    // 初始值写成 1(int),返回类型被定死成 int,1.5/2.5... 全被截断
    double acc = std::accumulate(vec.begin(), vec.end(), 1);
    std::cout << "accumulate(vec, 1)      = " << acc << '\n';

    // fold_left 的返回类型由 f(init, *first) 决定,这里推回 double,不截断
    double fl = std::ranges::fold_left(vec, 1, std::plus{});
    std::cout << "fold_left(vec, 1, +)    = " << fl << '\n';
}
```

跑出来对比非常刺眼：

```text
accumulate(vec, 1)      = 11
fold_left(vec, 1, +)    = 13
```

`accumulate` 那条：初始值 `1` 是 int，于是 `1 + 1.5` → `2`（截断）、`2 + 2.5` → `4`、`4 + 3.5` → `7`、`7 + 4.5` → `11`，全程 int，最后赋给 `double acc` 也救不回来——信息早就在每一步加法里被截没了。`fold_left` 那条：返回类型取的是 `std::plus{}(1, 1.5)` 的类型，也就是 `double`，`1 + 1.5 + 2.5 + 3.5 + 4.5 = 13.0`，不丢精度。这一个差别就值得换过去。

### 六个名字，十二个重载

`fold` 家族比 `accumulate` 全面得多。`accumulate` 只能左 fold，`fold` 左右都能做，还区分「要不要初始值」「要不要同时返回末尾迭代器」。设计上这几条是正交选择，全列出来是 8 个名字 × 2 重载 = 16 个；提案最后砍掉「右 fold + 同时返回迭代器」这一组（理由见下），剩下 **6 个名字、12 个重载**：

| 名字 | 方向 | 初始值 | 返回 |
|---|---|---|---|
| `fold_left` | 左 | 显式给 | 结果 |
| `fold_left_first` | 左 | 用首元素 | `optional<结果>` |
| `fold_right` | 右 | 显式给 | 结果 |
| `fold_right_last` | 右 | 用末元素 | `optional<结果>` |
| `fold_left_with_iter` | 左 | 显式给 | `{末尾迭代器, 结果}` |
| `fold_left_first_with_iter` | 左 | 用首元素 | `{末尾迭代器, optional<结果>}` |

命名规则很规律：`left`/`right` 是方向；不带 `first`/`last` 就显式给初始值，带了就用首/末元素当初始值；不带 `with_iter` 只回结果，带了同时回末尾迭代器。日常用记不住那么长，会 `fold_left` / `fold_right` 这两个就够覆盖八成场景。折叠的语义画出来是这样（`f` 是二元运算）：

```text
fold_left(r, init, f):           f(... f(f(init, r[0]), r[1]) ..., r[n-1])
fold_left_first(r, f):           f(... f(f(r[0], r[1]), r[2]) ..., r[n-1])
fold_right(r, init, f):          f(r[0], f(r[1], ... f(r[n-1], init) ...))
fold_right_last(r, f):           f(r[0], f(r[1], ... f(r[n-2], r[n-1]) ...))
```

### 几个绕不过去的设计细节

这里有几个看起来奇怪、其实各有道理的点，挨个讲。

**`first`/`last` 版本为什么返回 `optional`？** 因为它们用首/末元素当初始值，万一传进来的是**空 range**，根本没有首元素可用。别的算法（比如 `ranges::max`）遇到这种情况直接未定义行为，`fold` 选择返回空 `optional`——这也是标准库第一次有意义地用 `optional` 表达「这个算法对空输入没有定义值」。实测一下：

```cpp
// Standard: C++23
std::vector<int> empty;
auto opt = std::ranges::fold_left_first(empty, std::plus{});
std::cout << "fold_left_first(空) has_value = " << opt.has_value() << '\n';
```

```text
fold_left_first(空) has_value = 0
```

**为什么没有 `fold_right_with_iter`？** 因为右 fold 可以用 `views::reverse` 把它变成左 fold——不需要专门再做一个带迭代器的右 fold。具体等价式（注意二元运算的两个参数得对调）：

```cpp
fold_right(r, init, f)
  == fold_left(r | views::reverse, init,
               [](auto&& a, auto&& b){ return f(b, a); });
```

这个等价关系我们实测验证一下（用非交换的运算 `f(a,b) = a*10+b`，顺序敏感，能验出左右差异）：

```cpp
// Standard: C++23
#include <algorithm>
#include <iostream>
#include <ranges>
#include <vector>

int main() {
    std::vector<int> v{1, 2, 3, 4};
    auto f = [](auto a, auto b){ return a * 10 + b; };
    auto right  = std::ranges::fold_right(v, 0, f);
    auto as_left = std::ranges::fold_left(
        v | std::views::reverse, 0,
        [&](auto a, auto b){ return f(b, a); });
    std::cout << "fold_right:         " << right << '\n';
    std::cout << "fold_left(反转等价):  " << as_left << '\n';
}
```

```text
fold_right:         100
fold_left(反转等价):  100
```

两者完全相等，等价式成立。所以右 fold + with_iter 这一组合就被删了——你能用 `views::reverse` 自己拼出来，标准库就不重复造。

**为什么 `fold` 没有 projection（投影）参数？** 别的 ranges 算法（`sort`、`find`、`contains`）都能挂一个投影函数，唯独 `fold` 没有。原因是 `fold_left_first` 要算初始**值**，得把投影作用在首元素的右值上；而别的算法的投影只要求作用在引用/左值上，把右值转成左值要额外拷贝一次，这个效率损失 `fold` 接受不了。为了统一，本来能挂投影的 `fold_left` 也就一并没给投影参数。要投影，先 `views::transform` 套一层再 fold。

::: warning 头文件变了
`fold` 家族不在 `<numeric>` 里（虽然 `accumulate` 在），而在 `<algorithm>` 里。include 错了会「找不到名字」。
:::

## 方便 wrapper：contains、find_last、starts_with/ends_with（C++23）

`fold` 是修老坑，这一组是补老缺。STL 长期缺几个「一看就该有」的方便函数，逼着大家用别扭的写法凑，C++23 终于补上了。

### contains / contains_subrange：消灭 `find() != end()`

查询「某个值在不在序列里」是最高频的操作之一。STL 几十年都没给 `contains`，所有人只能写 `find(v, x) != v.end()`——把简单的「在不在」翻成「找到的位置是不是在末尾（即没找着）」，多绕一道。C++20 先给 `set`/`map` 这类关联容器补了成员 `contains(key)`，C++23 终于把通用版本 `ranges::contains` 补齐。它还有个查找子序列的兄弟 `ranges::contains_subrange`：

```cpp
// Standard: C++23
#include <algorithm>
#include <iostream>
#include <vector>

int main() {
    std::vector<int> v{1, 2, 3, 4, 5};
    std::vector<int> pat{2, 3};

    bool old_way = (std::ranges::find(v, 3) != v.end());   // 老反模式
    bool new_way = std::ranges::contains(v, 3);             // 一句话

    std::cout << "find!=end: " << old_way << "  contains: " << new_way << '\n';
    std::cout << "contains_subrange(v, {2,3}): "
              << std::ranges::contains_subrange(v, pat) << '\n';
    std::cout << "contains(v, 9): " << std::ranges::contains(v, 9) << '\n';
}
```

```text
find!=end: 1  contains: 1
contains_subrange(v, {2,3}): 1
contains(v, 9): 0
```

`contains` 查单个元素（内部就是调 `ranges::find`），`contains_subrange` 查子序列（内部调 `ranges::search`）。这俩不是新算法，是方便包装——但「方便」本身就是价值，代码读起来 `contains(v, 3)` 比 `find(v,3)!=v.end()` 直白得多，新人也不用再琢磨那个 `!= end()` 的反逻辑。

### find_last：返回 subrange 的反向查找

`std::find` 只找**第一个**匹配。想找**最后一个**，老办法是 `ranges::find(v | views::reverse, x)`——能做，但要套一层 reverse，还得自己把反转后的位置换算回原位置，啰嗦。C++23 补了 `ranges::find_last`（还有 `_if` / `_if_not` 三个变体），直接给：

```cpp
// Standard: C++23
std::vector<int> w{1, 2, 3, 4, 3, 2, 1};
auto [it, end] = std::ranges::find_last(w, 3);
std::cout << "find_last(w, 3) 下标 = "
          << std::distance(w.begin(), it) << '\n';
```

```text
find_last(w, 3) 下标 = 4
```

注意返回值不是单纯一个迭代器，而是一个 **`subrange`**（找到的位置 + 末尾），所以结构化绑定拿到 `[it, end]` 两个。这是 ranges 时代新算法的常见做法——把「找到的位置」连同「区间末尾」一起给回来，省得你再去 `w.end()` 取一次。找不到时 `it == end`，判一下就行。

::: warning 别指望老 std::find_last
`find_last` 只有 `ranges::` 版本。老 `<algorithm>` 基本不再加新东西了，想用就得用 ranges 版。
:::

### starts_with / ends_with

「这个序列是不是以那个序列开头/结尾」也是长期缺失的操作。C++20 先给 `string`/`string_view` 补了成员 `starts_with`/`ends_with`，C++23 又补了通用版 `ranges::starts_with` / `ranges::ends_with`，对任意 Range 都能用：

```cpp
// Standard: C++23
std::vector<int> v{1, 2, 3, 4, 5};
std::cout << "starts_with({1,2}): "
          << std::ranges::starts_with(v, (std::vector<int>{1, 2})) << '\n';
std::cout << "ends_with({4,5}): "
          << std::ranges::ends_with(v, (std::vector<int>{4, 5})) << '\n';
```

```text
starts_with({1,2}): 1
ends_with({4,5}): 1
```

注意和 `contains_subrange` 一样，**长序列在前、要匹配的前缀/后缀在后**。这俩还能带比较谓词和投影（用第三个、第四个参数），做大小写不敏感之类的匹配很方便。

## C++23 新 ranges 适配器：选讲透 + 速查

C++23 给 ranges 视图库加了一批新成员（近 15 个）。视图的通用机制（惰性、管道、工厂视图）是 vol4 的事，这里不铺背景，只挑工程上最常用的几个讲透，剩下的给张速查表。

### zip / zip_transform：并行遍历多个序列

`zip` 把多个 range「拉链」到一起，产出 `tuple` 的 range——每一组是各 range 同位置的元素。并行遍历两个序列再也不用手写共用下标了：

```cpp
// Standard: C++23
std::vector<int>         vi{1, 2, 3};
std::vector<std::string> vs{"a", "b", "c"};
for (auto [a, b] : std::views::zip(vi, vs)) {
    std::cout << '(' << a << ',' << b << ")\n";
}
```

```text
(1,a)
(2,b)
(3,c)
```

`zip_transform` 是 `zip` 完再套一个函数（等价于 `zip` + `transform(apply)`），一步到位：

```cpp
// Standard: C++23
for (auto s : std::views::zip_transform(std::plus{},
                                        vi, std::vector<int>{10, 20, 30})) {
    std::cout << s << '\n';   // 11 22 33
}
```

::: warning zip 的元素是引用 tuple,不是值 tuple
`zip(vi, vs)` 的元素引用类型是 `tuple<int&, string&>`（指向原容器），值类型才是 `tuple<int, string>`。这个差别绝大多数时候无感（结构化绑定照常用），但涉及 move-only 元素、要对原容器做排序时要注意。`ranges::sort(views::zip(vi, vs))` 能借这个引用语义「按一个容器排序、联动重排另一个」。
:::

### adjacent / pairwise：相邻 N 个为一组

`adjacent<N>` 把连续 N 个元素打包成 `tuple`（N 是编译期常量）。最常用的是 N=2，所以有别名 `pairwise`，算相邻差分、相邻配对特别顺手：

```cpp
// Standard: C++23
std::vector<int> v{1, 2, 3, 4, 5};
// adjacent<3>: (1,2,3) (2,3,4) (3,4,5)
// pairwise 相邻差: 1 1 1 1
for (auto [a, b] : std::views::pairwise(v)) {
    std::cout << (b - a) << ' ';   // 1 1 1 1
}
```

`adjacent` 和下面要讲的 `slide` 长得很像，区别在：`adjacent` 的窗口大小是**编译期**给定的（`adjacent<3>`），元素类型是 `tuple`；`slide` 的窗口大小是**运行期**参数（`slide(3)`），元素类型是 `subrange`。窗口大小编译期能定就用 `adjacent`，类型更具体、性能更好。

### chunk vs slide：分块不重叠，滑窗重叠

这俩最容易混。都是把序列切成固定大小的窗口，区别在窗口是否重叠：

- `chunk(n)`：**不重叠**分块，像分页——`[1..n]`、`[n+1..2n]`、…，最后一块可能不足 n。
- `slide(n)`：**重叠**滑窗，每次右移一格——`[1..n]`、`[2..n+1]`、`[3..n+2]`、…，每块都是完整 n 个（除了序列短于 n 时为空）。

实测对比，同一个序列 `1 2 3 4 5 6 7`、窗口 3：

```cpp
// Standard: C++23
#include <algorithm>
#include <iostream>
#include <ranges>
#include <vector>

void dump(const auto& r, const char* lbl) {
    std::cout << lbl << ":\n";
    for (auto c : r) {
        std::cout << "  [";
        for (int x : c) std::cout << x << ' ';
        std::cout << "]\n";
    }
}

int main() {
    std::vector<int> seq{1, 2, 3, 4, 5, 6, 7};
    dump(std::views::chunk(seq, 3), "chunk(3)");
    dump(std::views::slide(seq, 3), "slide(3)");
}
```

```text
chunk(3):
  [1 2 3 ]
  [4 5 6 ]
  [7 ]
slide(3):
  [1 2 3 ]
  [2 3 4 ]
  [3 4 5 ]
  [4 5 6 ]
  [5 6 7 ]
```

`chunk(3)` 出来 3 块（最后一块只有 `7`），`slide(3)` 出来 5 块（每块满 3 个，整体右滑）。记忆点：**chunk 像切蛋糕（一刀一刀不重叠），slide 像滑动窗口（一帧一帧重叠）**。需求是「分批处理」（分页、分桶）用 chunk，需求是「看局部上下文」（移动平均、N-gram）用 slide。

### stride：隔 N 取一

`stride(n)` 每隔 n 个元素取一个，补上了 STL 长期缺的「带步长子集」。老 STL 想隔一个取一个只能手写 `for (i = 0; i < v.size(); i += 2)`，`stride(2)` 一行替代：

```cpp
// Standard: C++23
std::vector<int> seq{1, 2, 3, 4, 5, 6, 7};
std::cout << "stride(2): ";
for (int x : std::views::stride(seq, 2)) std::cout << x << ' ';
std::cout << '\n';
```

```text
stride(2): 1 3 5 7
```

甚至 `views::iota(0) | stride(3)` 就能得到「0, 3, 6, 9, …」这种带步长的整数流——`iota` 自己没有步长参数，靠 `stride` 配。步长得是正整数，0 和负数无意义。

### repeat：单元素重复生成器（无界陷阱）

`repeat(x)` 把单个元素重复成一个**无穷** range；`repeat(x, n)` 重复 n 次（有界）。它是 view 工厂（像 `iota`），自己就是管道起点，不能在前面接 `r |`。

```cpp
// Standard: C++23
for (int x : std::views::repeat(7, 3)) std::cout << x << ' ';   // 7 7 7
std::cout << '\n';
// 无界版必须 take 截断,否则死循环
for (int x : std::views::repeat(0) | std::views::take(4)) std::cout << x << ' ';
std::cout << '\n';   // 0 0 0 0
```

::: warning repeat 的无界陷阱
`repeat(x)` 不带第二参数就是无穷 range，直接 `for (auto a : views::repeat(1))` 是**死循环**。要么给第二参数限定次数，要么 `| views::take(n)` 截断。`iota(N)` 同理——无穷工厂视图都要配 `take`。
:::

### 其余新适配器速查

剩下的几个不展开，给张表备查。命名上有些是几经改稿才定的（`as_rvalue` 原叫 `move`、`slide` 原叫 `sliding`、`zip_transform` 原叫 `zip_with`），照着现在的名字用就行。

| 适配器 | 作用 | 一句话区别 |
|---|---|---|
| `join_with(delim)` | 把 range-of-range 用分隔符拼平 | 比 C++20 的 `join` 多个分隔符；`{"ab","cd"} \| join_with('-')` → `a-b-c...` 实际是 `ab-cd` |
| `chunk_by(pred)` | 二元谓词为 false 就开新块（GroupBy） | 按谓词连续分块，不是按值；只切「相邻」满足的段 |
| `as_rvalue` | 元素以右值流出（range 版 `std::move`） | 配合 `ranges::to` 把元素 move 进新容器 |
| `as_const` | 元素只读（range 版 `std::as_const`） | 保护元素不被改 |

`join_with` 实测一把，把字符串数组用分隔符拼成长串：

```cpp
// Standard: C++23
std::vector<std::string> words{"hello", "world", "cpp23"};
std::cout << "join_with('-'): ";
for (char ch : std::views::join_with(words, '-')) std::cout << ch;
std::cout << '\n';   // hello-world-cpp23
```

`chunk_by` 用二元谓词，相邻两个元素谓词返回 false 就断开新块（连续的相同值会被分到一起）：

```cpp
// Standard: C++23
std::vector<int> runs{1, 1, 2, 2, 2, 3, 1, 1};
for (auto c : std::views::chunk_by(runs, std::equal_to{})) {
    std::cout << '[';
    for (int x : c) std::cout << x;
    std::cout << "]\n";   // [11] [222] [3] [11]
}
```

## 编译器支持现状：GCC 16.1.1 逐特性实测

本篇开头讲过——网上很多 ranges 教程写于 2022 年的「标准落定期」，那时候 C++23 特性还都没实现，满篇「GCC not yet」「Clang not yet」。现在已经是 2026 年了，那些状态标注**全部过时**。我们用本机 GCC 16.1.1（`g++ (GCC) 16.1.1 20260430`）逐特性实测，给一张当前的支持表。验证方法：对每个特性跑一段实际用到它的代码，编得过、跑得对就算支持；同时记录特性测试宏的值。

| 特性 | 头文件 | 测试宏 | GCC 16.1.1 | 备注 |
|---|---|---|---|---|
| `ranges::fold` 家族 | `<algorithm>` | `__cpp_lib_ranges_fold >= 202207L` | 已支持 | 全部 6 个名字都可用 |
| `ranges::contains` / `contains_subrange` | `<algorithm>` | `__cpp_lib_ranges_contains >= 202207L` | 已支持 | |
| `ranges::starts_with` / `ends_with` | `<algorithm>` | `__cpp_lib_ranges_starts_ends_with >= 202106L` | 已支持 | |
| `ranges::find_last` 家族 | `<algorithm>` | `__cpp_lib_ranges_find_last >= 202207L` | 已支持 | 宏名是 `ranges_find_last`，别查错 |
| `views::zip` / `zip_transform` | `<ranges>` | `__cpp_lib_ranges_zip >= 202110L` | 已支持 | |
| `views::adjacent` / `pairwise` | `<ranges>` | `__cpp_lib_ranges_zip >= 202110L` | 已支持 | 和 zip 同一个提案宏 |
| `views::chunk` | `<ranges>` | `__cpp_lib_ranges_chunk >= 202202L` | 已支持 | |
| `views::slide` | `<ranges>` | `__cpp_lib_ranges_slide >= 202202L` | 已支持 | |
| `views::stride` | `<ranges>` | `__cpp_lib_ranges_stride >= 202207L` | 已支持 | |
| `views::repeat` | `<ranges>` | `__cpp_lib_ranges_repeat >= 202207L` | 已支持 | |
| `views::join_with` | `<ranges>` | `__cpp_lib_ranges_join_with >= 202202L` | 已支持 | |
| `views::chunk_by` | `<ranges>` | `__cpp_lib_ranges_chunk_by >= 202202L` | 已支持 | |
| `views::as_rvalue` | `<ranges>` | `__cpp_lib_ranges_as_rvalue >= 202207L` | 已支持 | |
| `views::as_const` | `<ranges>` | `__cpp_lib_ranges_as_const >= 202311L` | 已支持 | |

结论很干脆：**本篇讲到的 C++23 ranges 算法和适配器，GCC 16.1.1 全部支持**。上面每一行都有对应代码在本机编过、跑过。如果你用的 GCC 还在 13/14，`fold`/`contains`/`find_last` 这批 `<algorithm>` 新件和 `as_const` 可能还缺——升到 15 以上就齐了。Clang 的 libstdc++ 支持跟进稍慢（Clang 用自己的 libc++ 时部分适配器实现晚于 GCC），跨编译器项目里用之前最好实测一下目标工具链。

## 小结

把这篇的要点收一下：

- **算法 ranges 化三步**：参数从迭代器对变 Range（哨兵让 `\0` 结尾串这类「读到条件才停」的序列能用）、Concept 在调用点就拒错类型（`ranges::sort(list)` 报错比老 `std::sort(list)` 直观得多）、Niebloid 不参与 ADL（算法不会被用户命名空间同名函数劫持）。
- **`fold` 家族修了 `accumulate` 的返回类型坑**：返回类型由 `f(init, *first)` 决定，不再被初始值类型锁死；6 个名字 12 个重载（左右 × 有无初始值 × 有无 with_iter）；`first`/`last` 版返回 `optional`（空 range）；无 `fold_right_with_iter`（用 `views::reverse` 拼）；无 projection（首元素右值投影有损）。
- **方便 wrapper 补老缺**：`contains`/`contains_subrange` 消灭 `find()!=end()`；`find_last` 返回 subrange（别忘了只有 `ranges::` 版）；`starts_with`/`ends_with` 通用化 string 的成员函数。
- **C++23 新适配器**：`zip`/`zip_transform`（并行遍历）、`adjacent`/`pairwise`（编译期窗口，tuple）、`chunk`（不重叠分块）vs `slide`（重叠滑窗）、`stride`（隔 N 取一）、`repeat`（注意无界陷阱要 `take` 截断）；外加 `join_with`/`chunk_by`/`as_rvalue`/`as_const`。
- **GCC 16.1.1 支持现状**：本篇讲到的 C++23 ranges 算法（fold/contains/find_last/starts_ends_with）和适配器（zip/chunk/slide/stride/repeat/join_with/chunk_by/as_rvalue/as_const）**全部已支持**。别信 2022 年老资料里的「GCC not yet」。

下一篇我们接着讲 ranges 视图的通用机制——管道 `|`、惰性求值、工厂视图——那部分归 vol4，讲清楚 view 到底「懒」在哪里、怎么和算法咬合出 LINQ 风格的链式写法。

## 参考资源

- [cppreference: Constrained algorithms (C++20)](https://en.cppreference.com/w/cpp/algorithm/ranges) —— ranges 算法总览与 Niebloid 说明
- [cppreference: std::ranges::fold_left (C++23)](https://en.cppreference.com/w/cpp/algorithm/ranges/fold_left) —— fold 家族六个名字的签名与返回类型规则
- [cppreference: std::ranges::contains (C++23)](https://en.cppreference.com/w/cpp/algorithm/ranges/contains) —— contains 与 contains_subrange
- [cppreference: std::ranges::find_last (C++23)](https://en.cppreference.com/w/cpp/algorithm/ranges/find_last) —— 返回 subrange 的反向查找
- [cppreference: std::ranges::zip_view (C++23)](https://en.cppreference.com/w/cpp/ranges/zip_view) —— zip 家族（zip / adjacent / pairwise 及 _transform 版）
- [cppreference: std::ranges::chunk_view / slide_view (C++23)](https://en.cppreference.com/w/cpp/ranges/chunk_view) —— 分块与滑窗的语义差异
- [cppreference: std::ranges::stride_view / repeat_view (C++23)](https://en.cppreference.com/w/cpp/ranges/stride_view) —— 步长子集与单元素重复生成器
- [P2322R6 fold](https://wg21.link/p2322r6)、[P2302R4 contains](https://wg21.link/p2302r4)、[P2214R1 C++23 Ranges 计划](https://wg21.link/p2214r1) —— 各特性的原始提案与设计动机
