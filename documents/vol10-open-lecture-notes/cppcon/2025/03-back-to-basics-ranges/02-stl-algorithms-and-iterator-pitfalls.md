---
chapter: 3
conference: cppcon
conference_year: 2025
cpp_standard:
- 11
- 17
- 20
description: CppCon 2025 演讲笔记 —— Mike Shah：STL 算法族实战、迭代器类别硬约束，补算法速查表与失效规则表，用 GCC 实测迭代器失效的静默
  UB 与 _GLIBCXX_DEBUG 捕获
difficulty: beginner
order: 2
platform: host
reading_time_minutes: 20
speaker: Mike Shah
tags:
- cpp-modern
- host
- beginner
- Ranges
- 容器
talk_title: 'Back to Basics: C++ Ranges'
title: STL 算法实战与迭代器陷阱
video_youtube: https://www.youtube.com/watch?v=Q434UHWRzI0
---
# STL 算法实战与迭代器陷阱

:::tip
这是 CppCon 2025 Mike Shah "Back to Basics: C++ Ranges" 系列的第二篇。上一篇我们把「遍历」从下标循环一路抽象到了迭代器，结论是：**一对 `begin`/`end` 迭代器定义了一个 range**。这一篇我们就把这对迭代器喂给 STL 算法——看它们怎么替你写循环，以及它们对迭代器有哪些硬性要求。本篇还会重点拆几个迭代器的经典陷阱，并全部用 GCC 16.1.1 实测给你看。环境同上：Arch Linux WSL，`-std=c++20`。
:::

上一篇结尾我们说，算法就建立在那对迭代器之上。这话要落到具体上，得先搞清楚 STL 到底由哪几块拼起来的。

## STL 的三大支柱

标准模板库（STL）的设计哲学，是把三样东西解耦开：**容器**（containers）负责存数据，**迭代器**（iterators）负责遍历数据，**算法**（algorithms）负责处理数据<RefLink :id="1" preview="cppreference, Standard library algorithms — containers, iterators, algorithms" />。三者通过迭代器这个「胶水」连接起来——算法不直接认识任何具体容器，它只认迭代器；容器只要能吐出符合要求的迭代器，就能被所有算法复用。这个解耦是 STL 能用一个 `std::sort` 通吃 `vector`、`array`、`deque` 的根本原因。

那算法到底在哪几个头文件里？

:::warning Shah 说的「两个头」有点窄
Shah 在演讲里说「算法主要在 `<algorithm>` 和 `<numeric>` 两个头文件」——这对入门理解没问题，但实际上**漏了好几块**。完整的图景是这样的：通用算法（`sort`、`find`、`copy`、`transform` 等）在 `<algorithm>`；数值算法（`accumulate`、`reduce`、`inner_product` 等）在 `<numeric>`；**并行算法**（带执行策略的 `sort(std::execution::par, ...)` 等）需要 `<execution>`（C++17）；C++20 的 ranges 算法和 views 在 `<ranges>`；甚至还有分散的——`std::midpoint` 在 `<numeric>`，但 C++23 的折叠算法 `std::fold_left` 却在 `<algorithm>`。所以别死记「算法=两个头」，记成「算法分散在几个头里，`<algorithm>` 是主力」更准确。
:::

## 算法速查表：按类别看，每个算法要什么迭代器

STL 算法有上百个，硬背没意义。更好记的方式是**按类别归类**，并且记住**每个类别对迭代器类别的硬性要求**——因为这直接决定了你能不能把它用在某个容器上。下面这张表是本篇的二创重点，Shah 在演讲里没展开：

| 分类 | 代表算法 | 所需迭代器类别 |
|------|------|------|
| 只读查找 | `find` / `find_if` / `count` / `accumulate` | input（最弱即可） |
| 修改拷贝 | `copy` / `transform` / `replace` / `fill` | forward / output |
| 分区 | `partition` / `stable_partition` | forward（稳定版需 bidirectional） |
| 排序 | `sort` / `stable_sort` / `partial_sort` | **random_access**（硬要求） |
| 二分查找 | `lower_bound` / `upper_bound` / `binary_search` | forward（**且区间必须已有序**） |
| 数值归约 | `reduce` / `transform_reduce` / `inner_product` | input |
| 堆操作 | `push_heap` / `pop_heap` / `sort_heap` | random_access |

这里最值得记住的一条是：**排序类算法要求随机访问迭代器（random access）**。这意味着它们只能用在 `vector`、`array`、`deque` 这种连续或随机访问的容器上，**用在 `std::list` 上直接编不过**。这不是建议，是硬约束。我们来实测一下。

## 实验：std::sort 不能用在 std::list 上

`std::list` 是双向迭代器（bidirectional），不支持 `it + n`、也不支持两个迭代器相减。而 `std::sort` 内部需要随机访问（它要做 `__last - __first` 来估算递归深度）。把 list 的迭代器塞进去会怎样？

```cpp
#include <algorithm>
#include <list>

int main()
{
    std::list<int> l{3, 1, 2};
    std::sort(l.begin(), l.end());  // 编不过！
}
```

GCC 16.1.1 的报错（截取关键几行）：

```bash
❯ g++ -std=c++20 list_sort.cpp -o list_sort
/usr/include/c++/16.1.1/bits/stl_algo.h:1914:50: error: no match for ‘operator-’
   (operand types are ‘std::_List_iterator<int>’ and ‘std::_List_iterator<int>’)
 1914 |                                 std::__lg(__last - __first) * 2,
   |                                           ~~~~~~~^~~~~~~~~
```

看到没——错误就出在 `__last - __first` 这一步：`std::sort` 想用迭代器减法算区间长度，但 `_List_iterator` 根本没定义 `operator-`（双向迭代器只认 `++`/`--`，不认减法）。这就是「迭代器类别不满足算法要求」的典型表现。如果你确实要排序一个 `list`，用它的成员函数 `l.sort()`——那是为链表量身定制的归并排序，复杂度还是 O(n log n)，但不依赖随机访问。

## sort、partition、copy、transform：常见算法长什么样

我们快速过一遍最常用的几个算法，建立直觉。它们的参数形态惊人地统一——绝大多数都是**一对迭代器 `(first, last)` 加上一个可选的谓词或目标**。

```cpp
#include <algorithm>
#include <vector>
#include <iterator>
#include <random>

void demo(std::vector<int>& v, const std::vector<int>& src)
{
    // 排序整个区间
    std::sort(v.begin(), v.end());

    // 局部排序：只排 [begin, begin+3)，后面元素顺序不定但都 >= 前 3 个
    // std::partial_sort(v.begin(), v.begin() + 3, v.end());

    // 分区：把满足谓词的元素挪到前面，返回分界点
    auto it = std::partition(v.begin(), v.end(), [](int x) { return x < 4; });

    // 拷贝：用 back_inserter 自动 push_back，不用预先算大小
    std::copy(src.begin(), src.end(), std::back_inserter(v));

    // 打乱：必须传一个随机数引擎（C++11 起 rand() 不推荐）
    std::shuffle(v.begin(), v.end(), std::mt19937{std::random_device{}()});
}
```

这里有两个细节值得多说一句。`std::back_inserter(v)` 返回的是一个**输出迭代器（output iterator）**，你往它里面写东西，它就自动调 `v.push_back()`——这就避开了「我得先知道要拷多少个、提前 reserve 好空间」的麻烦，是 `copy` 最常见的搭档。`std::shuffle` 则提醒我们：**C++11 之后，随机数应该用 `<random>` 头里的引擎（`std::mt19937` 等），而不是老的 `rand()`**——`rand()` 质量差、还有线程安全问题。

再看 `std::transform`，它把「对每个元素套一个函数」这件事封装好了。注意这里用了 `cbegin`/`cend`——**const 版本的迭代器**，表示「我只读源区间，不修改它」：

```cpp
#include <algorithm>
#include <string>
#include <iterator>

std::string s = "hello";
std::string out;
std::transform(s.cbegin(), s.cend(), std::back_inserter(out),
               [](char c) { return std::toupper(static_cast<unsigned char>(c)); });
// out == "HELLO"
```

`cbegin`/`cend` 返回 `const_iterator`，`rbegin`/`rend` 返回反向迭代器。一个容易踩的点是：**这些迭代器必须成对使用**——你不能拿 `cbegin()` 配 `end()`（一个 const 一个非 const，类型不匹配）。C++20 之后，`const_iterator` 在标准库里的地位又被抬高了一截（P0896 等提案），因为 ranges 体系大量依赖它。

## rotate：参数顺序是最大的坑

`std::rotate` 是个很有用、但也特别容易写错的算法。它的作用是「把区间里的元素循环挪位，让 `middle` 指向的元素变成新的首元素」。签名是三个迭代器：`std::rotate(first, middle, last)`。

```cpp
std::vector<int> v{1, 2, 3, 4, 5};
std::rotate(v.begin(), v.begin() + 2, v.end());
// 结果：{3, 4, 5, 1, 2}  —— middle(begin+2，即 3) 变成了新首元素
```

实测输出：

```bash
❯ g++ -std=c++20 rot_ok.cpp -o rot_ok && ./rot_ok
rotate(begin, begin+2, end) on {1,2,3,4,5} -> { 3 4 5 1 2 }
```

这里的陷阱在于：**绝大多数算法是两个迭代器 `(first, last)`，唯独 `rotate`（还有 `partial_sort`、`nth_element` 等）是三个 `(first, middle, last)`**。人一旦形成「两个参数」的肌肉记忆，写 `rotate` 时就特别容易把 `middle` 和 `last` 的位置搞反。Shah 自己也吐槽过，他用 `upper_bound` 找插入点再 `rotate` 来手动实现插入排序，评价是「too clever, ugly」（太聪明了、丑）。

那写反了会怎样？我把 `middle` 和 `last` 互换，写成 `rotate(first, last, middle)`：

```cpp
std::vector<int> w{1, 2, 3, 4, 5};
std::rotate(w.begin(), w.end(), w.begin() + 2);  // 参数顺序错了
```

```bash
❯ g++ -std=c++20 rot_bad.cpp -o rot_bad && ./rot_bad
about to call rotate(begin, end, begin+2)...
[程序崩溃，退出码 139 — SIGSEGV]
```

直接段错误（退出码 139 = SIGSEGV）。原因很直接：`std::rotate` 要求 `[first, middle)` 和 `[middle, last)` 都是合法子区间，换句话说三个迭代器必须满足 `first <= middle <= last` 的顺序。写成 `(first, last, middle)` 后，第二个子区间 `[middle_arg=last, last_arg=middle)` 就成了非法区间（终点在起点之前），算法去解引用越界位置，崩。

:::warning 三个迭代器的算法，参数顺序一定要看文档
`rotate`、`partial_sort`、`nth_element`、`stable_partition` 这些算法的参数都不是简单的 `(first, last)`，而是 `(first, middle, last)` 之类的三段。用之前一定要确认 `middle` 到底指什么。这一点在第三篇讲的 ranges 版本里会改善——因为 ranges 版本常常少传参数（直接传容器），减少了配对出错的机会。
:::

## 算法到底有多少个？「200 多个」要打折听

Shah 在演讲里提到一个流传很广的数字：「2018 年有场 CppCon 演讲说至少 105 个算法，现在有 200 多个了。」这个说法对不对？我们来较个真<RefLink :id="2" preview="cppreference, Standard library header <algorithm> — function template count" />。

先说「105」这个数字的出处：它来自 Jonathan Boccara 在 CppCon 2018 的演讲《105 STL Algorithms in Less Than an Hour》<RefLink :id="3" preview="Jonathan Boccara, CppCon 2018 — 105 STL Algorithms" />。那是个**很宽松的计数口径**——它把 `_if` 变体（`find` / `find_if`）、`_n` 变体（`copy` / `copy_n`）、`_copy` 变体（`remove` / `remove_copy`）都分别算成一个独立算法，目的是演讲时好记、好讲。

那严格的数字是多少？我对照 cppreference 查了一下，截至 C++23：

- `<algorithm>` 头里大约有 **91 个** `std::` 函数模板（不算 ranges 版本）。
- `<numeric>` 头里有 **14 个**数值算法（`accumulate`、`reduce`、`inner_product` 等；C++26 还会再加 5 个饱和算术，凑成 19 个）。
- `std::ranges::` 命名空间下大约有 **100 个**「受约束算法」（niebloid，就是 ranges 版本的算法）。
- 另外还有 `<memory>` 里约 14 个未初始化内存相关的算法。

所以「200 多个」这个说法，**只有在把 `std::` 和 `std::ranges::` 两套 API 各算一份、再算上各种变体重载的口径下才成立**。如果按「不重复的算法名字」来数，实际大约是 **110 到 120 个**。

:::tip 怎么表述才准
比起说「STL 有 200 多个算法」，更严谨的说法是：**STL 有 100 多个不重复的算法；如果把 `std::` 和 `std::ranges::` 两套接口都算作条目，API 入口确实超过 200 个。** 这个区分在面试或技术写作里挺重要——「200 多个」听起来唬人，但里面有大量是同一个算法的变体和 ranges 镜像版。
:::

## 陷阱一：迭代器失效——最隐蔽的杀手

算法本身用熟了不难，真正坑人的是**迭代器和容器的生命周期配合**。排第一的陷阱是**迭代器失效（iterator invalidation）**。

来看一段看起来人畜无害的代码：

```cpp
std::vector<int> v{1, 2, 3};
auto it = v.begin();        // it 指向 v 的第一个元素
v.push_back(4);             // 如果触发扩容，it 就悬空了！
std::cout << *it << '\n';   // 解引用悬空迭代器 —— UB
```

问题出在 `push_back`。`vector` 内部是一块连续的动态数组，容量不够时会**重新分配一块更大的内存**，把旧元素搬过去，然后释放旧内存。但你的 `it` 还指着那块**已经被释放的旧内存**——它成了悬空指针（标准叫「singular iterator」）。这时候解引用 `*it`，就是未定义行为。

可怕的地方在于：**UB 不一定立刻崩**。它经常表现为「读到一个看起来正常的值」，于是你以为没事，把代码合进主干，然后某天在客户的机器上莫名其妙地崩溃。我们实测一下普通编译（不开调试）的情况：

```cpp
#include <vector>
#include <iostream>
int main()
{
    std::vector<int> v{1, 2, 3};
    auto it = v.begin();
    std::cout << "before push_back: *it=" << *it << ", cap=" << v.capacity() << "\n";
    v.push_back(4); v.push_back(5); v.push_back(6); v.push_back(7);  // 必然扩容
    std::cout << "after  push_back: cap=" << v.capacity() << "\n";
    std::cout << "deref stale it: " << *it << "\n";   // UB：读已释放内存
}
```

```bash
❯ g++ -std=c++20 -O0 inval.cpp -o inval && ./inval; echo "退出码=$?"
before push_back: *it=1, cap=3
after  push_back: cap=12
deref stale it: -40771459
退出码=0
```

看到了吗——程序**正常退出（退出码 0），没有任何报错**，但读出来的值是 `-40771459` 这种垃圾值。`vector` 扩容后容量从 3 涨到 12，旧内存被释放了，`it` 指向的内存里残留着随机数据。这就是 UB 最阴险的样子：**静默错误**。

那怎么抓它？GCC/Clang 提供了一个调试宏 `-D_GLIBCXX_DEBUG`，开启后标准库的迭代器会带上边界和有效性检查，一旦你解引用失效迭代器，立刻 abort 并打印诊断。我们用同样的代码开调试编一遍：

```bash
❯ g++ -std=c++20 -O0 -g -D_GLIBCXX_DEBUG inval.cpp -o inval_dbg && ./inval_dbg; echo "退出码=$?"
before push_back: *it=1, cap=3
after  push_back: cap=12
/usr/include/c++/16.1.1/debug/safe_iterator.h:352:
Error: attempt to dereference a singular iterator.
Objects involved in the operation:
    iterator "this" @ 0x7fff6bd63820 {
      type = gnu_cxx::normal_iterator<int*, std::vector<int>>(mutable iterator);
      state = singular;   ← 迭代器已失效
      references sequence with type 'std::debug::vector<int>' @ 0x7fff6bd63850
    }
退出码=134   ← 134 = SIGABRT，被调试库主动 abort
```

这下被逮个正着：`state = singular` 明确告诉你这个迭代器失效了，`attempt to dereference a singular iterator` 精确指出你干了什么。一个 `-D_GLIBCXX_DEBUG` 宏，把「静默 UB」变成了「立刻炸+精准定位」——开发期开它，发布期关掉（它有性能开销）。MSVC 那边对应的开关是 `_ITERATOR_DEBUG_LEVEL=2`，Release 配置默认就是 0 或 1，Debug 配置才是 2。

:::tip 迭代器失效规则速查（已核对 cppreference）
不同容器，失效规则差别很大，记个大概就行，具体查表<RefLink :id="4" preview="cppreference, Iterator invalidation — rules per container" />：

- **`vector` / `string`**：`push_back` 仅在触发扩容（容量改变）时让**所有**迭代器失效；不扩容时只有 `end()` 会变。`reserve` 之后只要不超过预留容量，迭代器就不会失效。
- **`deque`**：在两端插入会让**所有迭代器**失效（哪怕不扩容），但**引用和指针不失效**——所以遍历 deque 时要小心，存迭代器不如存引用。
- **`list` / `forward_list`**：插入、`splice` **不失效**任何已有迭代器（链表节点不搬家），只有被 `erase` 掉的那个节点对应的迭代器失效。
- **`unordered_*`**：`rehash`（触发于插入导致桶数变化）会让**迭代器失效，但引用和指针不失效**。

记住一个总原则：**只要容器内部可能「搬家」（连续存储的容器扩容、哈希表 rehash），迭代器就可能失效；节点型容器（list、树的节点）不搬家，迭代器就稳。**
:::

## 陷阱二：配错迭代器对——begin 和 end 必须来自同一个对象

第二个陷阱和「配对」有关。算法要求 `first` 和 `last` 来自**同一个容器**，但 C++ 没法在运行时强制检查这件事——你传两个来自不同容器的迭代器，编译器照单全收，然后就是 UB。

最经典的翻车场景来自 Jason Turner 的 C++ Weekly（Shah 在演讲里专门引用了）：一个函数返回一个临时的 `vector`，你图省事直接 `.begin()` 和 `.end()` 连着调：

```cpp
std::vector<int> download_data();  // 每次调用返回一个全新的临时 vector

// 危险写法：
// process(download_data().begin(), download_data().end());
```

:::warning Shah 这里说轻了
Shah 对这段代码的点评是「也许有时能工作，也许我们运气好」——这个说法**可能误导新手**，因为它暗示「这玩意儿有合法的能工作的情况」。**没有。** 这就是未定义行为，不存在「合法能工作」的路径，只有「UB 偶然表现正常」的假象。

原因：两次 `download_data()` 是**两次独立的函数调用**，返回的是**两个不同的临时 `vector`**。它们的 `.begin()` 和 `.end()` 指向两块毫无关系的内存。把一个临时量的 `begin` 和另一个临时量的 `end` 配成一对喂给算法——区间根本不合法。更糟的是，这两个临时量在这条语句结束时就被析构了，算法拿着的迭代器一开始就悬空。**正确写法是先把结果存进一个具名变量**，让 `begin` 和 `end` 来自同一个存活的对象：

```cpp
auto data = download_data();          // 一个具名变量，一份内存
process(data.begin(), data.end());    // begin/end 来自同一个 data —— 安全
```

这种「函数名相同就以为指的是同一个对象」的错觉，是配对出错的高发区。
:::

## 陷阱三：空间不足——往固定大小的地方塞太多

第三个陷阱和输出目标有关。当你用 `std::copy` 把数据写到一个**固定大小**的目标时（比如原生数组、或者没加 `back_inserter` 的容器），如果源区间比目标空间大，就会**越界写**——同样是 UB，而且可能默默破坏相邻内存。

```cpp
int src[10] = {0,1,2,3,4,5,6,7,8,9};
int dst[3];   // 只有 3 个位置！
std::copy(std::begin(src), std::end(src), std::begin(dst));  // 越界写 —— UB
```

这段代码能编过、能跑、不会立刻报错，但你往 `dst` 后面的内存里写了 7 个不该写的值。这种 bug 用 AddressSanitizer（`-fsanitize=address`）能抓出来，它会报告一个 heap/stack buffer overflow。

规避办法很直接：要么用 `std::back_inserter`（让目标容器自动扩容），要么在 copy 前 `reserve` 足够空间、并确认源区间不大于目标容量。回到第一条经验：**让容器自己管大小（用 inserter），比你自己手算大小安全得多。**

## 报错质量：ranges 真的报错更友好吗

Shah 在总结里说「Ranges 用了 concepts，会给你更好的错误信息」。这话对，但需要打个折，我们实测对比一下「传错参数」时两套接口的报错。

先看经典 `std::sort` 传错——把 `vector` 的 `begin` 和 `list` 的 `end` 配在一起（类型不匹配）：

```cpp
std::vector<int> v{1,2,3};
std::list<int>   l{4,5,6};
std::sort(v.begin(), l.end());   // 两个不同容器的迭代器
```

再看 ranges 版本传错——把一个根本不是 range 的东西传给 `std::ranges::sort`：

```cpp
int not_a_range = 42;
std::ranges::sort(not_a_range);
```

两者 GCC 16.1.1 的报错行数：

```bash
❯ # 经典版
❯ g++ -std=c++20 err_classic.cpp 2>err_c.txt; wc -l < err_c.txt
32
❯ head -3 err_c.txt
err_classic.cpp:7:14: error: no matching function for call to
  'sort(std::vector<int>::iterator, std::__cxx11::list<int>::iterator)'

❯ # ranges 版
❯ g++ -std=c++20 err_ranges.cpp 2>err_r.txt; wc -l < err_r.txt
69
```

有意思的来了——**在这个具体例子里，ranges 版的报错（69 行）反而比经典版（32 行）更长**。这是因为传一个 `int` 给 `ranges::sort`，编译器要把整条 concept 约束链（`sortable` → `random_access_iterator` → ...）一路展开给你看，链条越长报错越铺张。所以我得诚实地纠正一个常见印象：**「ranges 报错一定更短更友好」并不成立**，它的可读性很依赖编译器版本和具体场景（GCC 10+ / Clang 12+ 之后才比较成熟，旧编译器照样一屏幕模板天书）。

那 ranges 在「报错」这件事上真正的优势是什么？不是行数，而是**它让你根本写不出某些 bug**。回想上面陷阱二——经典 `std::sort` 接收两个迭代器，你完全可以把两个不同容器的 `begin`/`end` 配错（像 `err_classic` 那样），编译器要等到实例化时才报错。而 `std::ranges::sort` **只接收一个容器**，你压根没法表达「begin 来自 A、end 来自 B」这种错误。**少一个出错的机会，比报错更友好要实在得多。** 这才是 ranges 在安全性上的核心收益，我们第三篇会展开。

## 过渡：迭代器必须滚蛋？

讲到这里，Shah 放了一张相当夸张的幻灯片——「迭代器必须滚蛋（Iterators must die）」。夸张归夸张，但他想表达的情绪是真实的：**迭代器这套接口虽然强大，但用起来坑多**——配对容易错、参数顺序（三个迭代器的算法）容易反、局部排序的写法丑陋。

好消息是，C++20 的 Ranges 正是冲着这些痛点来的。它没有抛弃迭代器（迭代器仍然是底层机制，连 C++26 都离不开它），而是在迭代器之上包了一层更安全、更好组合的接口：**直接传容器而不是迭代器对、用 concepts 在编译早期拦截类型错误、用 views 实现惰性组合**。这些是第三篇的主线。

下一篇我们就正式进入 Ranges——从「`ranges::sort` 为什么少传一个参数」开始，一路讲到 views 的惰性求值、管道操作符、`ranges::to`，以及一个让人眼前一亮的特性：**无限 range**。如果你对数值算法（`reduce`、`transform_reduce`）的并行版本感兴趣，可以提前看看 vol5 并发卷里关于 `<execution>` 执行策略和 `std::reduce` 并行归约的内容——那是算法和并发交汇的地方。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="Algorithms library"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/algorithm"
    chapter="containers / iterators / algorithms 三大支柱"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="Standard library header &lt;algorithm&gt;"
    :year="2024"
    url="https://en.cppreference.com/w/cpp/header/algorithm"
    chapter="截至 C++23 约 91 个函数模板"
  />
  <ReferenceItem
    :id="3"
    author="Jonathan Boccara"
    title="105 STL Algorithms in Less Than an Hour — CppCon 2018"
    :year="2018"
    url="https://www.youtube.com/watch?v=2olsGf6JIkU"
    chapter="宽松计数口径下 105 个"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Iterator invalidation rules"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/container"
    chapter="各容器 insert/erase 后的失效规则"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="std::rotate"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/algorithm/rotate"
    chapter="参数顺序 first, middle, last"
  />
  <ReferenceItem
    :id="6"
    author="cppreference.com"
    title="std::vector — Iterator invalidation"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/container/vector"
    chapter="push_back 扩容导致迭代器失效"
  />
  <ReferenceItem
    :id="7"
    author="cppreference.com"
    title="Standard library header &lt;numeric&gt;"
    :year="2023"
    url="https://en.cppreference.com/w/cpp/header/numeric"
    chapter="数值算法约 14 个"
  />
  <ReferenceItem
    :id="8"
    author="Mike Shah"
    title="Back to Basics: C++ Ranges — CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=Q434UHWRzI0"
  />
</ReferenceCard>
