---
title: "新标准容器：flat_map、inplace_vector 与 mdspan"
description: "梳理 C++23/26 给容器家族补的新成员：flat_map 把红黑树拍平成排序 vector（有序 + cache 友好但插删 O(n)）、inplace_vector 定容不堆分配（C++26）、mdspan 多维视图（C++23，submdspan 切片在 C++26），以及还在路上的 hive 提案"
chapter: 7
order: 10
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
difficulty: intermediate
platform: host
cpp_standard: [23, 26]
reading_time_minutes: 15
prerequisites:
  - "map 与 set 深入"
  - "unordered_map 与 set 深入"
  - "span：非拥有的连续视图"
  - "array：编译期固定大小的聚合容器"
related:
  - "容器选择指南：按操作、内存与失效规则挑对容器"
---

# 新标准容器：flat_map、inplace_vector 与 mdspan

## 这一篇讲什么：C++23/26 补的几个长期缺口

标准库的 `container` 家族从 C++98 定下来后稳定了二十多年，`vector`/`map`/`unordered_map` 这一套几乎没动过。但实战里有几个一直被人念叨的缺口：有序的关联容器能不能别用红黑树、改用连续存储换 cache 友好？定长的 `array` 和会堆分配的 `vector` 之间，能不能有个「容量上限已知、运行期可变长、又绝不碰堆」的中间态？多维数据（矩阵、图像、体素）能不能有个像 `span` 一样的非拥有多维视图？C++23 和 C++26 这两波正好把这几样补上了——这一篇讲 `flat_map`/`flat_set`、`inplace_vector`、`mdspan` 三个已经标准化的，顺带提一句还在路上的 `hive`。

需要先打个预防针：这些组件都很新，`flat_map` 和 `mdspan` 是 C++23（要较新的 libstdc++/libc++），`inplace_vector` 是 C++26，工具链跟不上的话编不过。理解它们的设计思路比马上能用更重要——等你升上 C++23/26 工具链，这些就是现成的弹药。本文所有例子都在 GCC 16.1.1（libstdc++，`-std=c++23` / `-std=c++26`）上实跑通过：`<flat_map>` 和 `<mdspan>` 从 GCC 15 起就有，`<inplace_vector>` 要 GCC 16。

## flat_map / flat_set：把红黑树拍平成排序 vector（C++23）

先看 `std::flat_map` 和 `std::flat_set`（连同 `flat_multimap`/`flat_multiset`，一共四个）。它们的动机很直接：[map 与 set 深入](06-map-set-deep-dive.md) 讲过 `map`/`set` 底层是红黑树，每个元素一个堆节点，节点之间靠指针串，查找遍历都在节点间跳，cache 命中率差——虽然复杂度是 O(log n)，但常数因子被 cache 不友好吃掉一大块。`flat_map` 的做法是**把整棵树拍平成一个排序的连续容器**（默认底层就是 `std::vector`），键值对在内存里挨着排好，查找用二分（O(log n)），但因为是连续内存，cache 友好，实际常数比红黑树小一截。

接口上 `flat_map` 是 `map` 的**近乎 drop-in 的替换**——`insert`/`erase`/`find`/`operator[]`/范围遍历都在，甚至有序遍历也能用，迁移成本低。但代价也很清楚，全来自「底层是连续容器」这个事实。第一，**插入和删除是 O(n)**：要在有序数组中间塞一个元素，得把它后面所有元素往后挪；删一个同样要往前挪。这跟红黑树 O(log n) 的增删形成鲜明对比，所以 flat_map 适合「查找和遍历远多于增删」的场景。第二，**迭代器和引用不稳定**：任何插删都可能像 `vector` 一样触发搬移甚至重分配，让所有迭代器失效——而 `map` 的迭代器是永不失效的。一句话，flat_map 用「增删变贵 + 失效变凶」换「查找遍历更快的常数」，数据量不大、读多写少时这买卖划算。

```cpp
#include <flat_map>
#include <print>
#include <string>

int main()
{
    std::flat_map<int, std::string> m;
    m.insert({3, "three"});
    m.insert({1, "one"});
    m.insert({2, "two"});          // O(n)：维护有序要搬移

    auto it = m.find(2);           // O(log n)：二分，连续内存 cache 友好
    std::println("find(2) = {}", it->second);

    m.erase(1);                    // O(n)：删了要往前挪
    // it 在这里已失效——和 vector 一样，别再用

    for (auto [k, v] : m) {        // 有序遍历：1 已删，剩下 2, 3
        std::println("{}: {}", k, v);
    }
    return 0;
}
```

## inplace_vector：定容、不堆分配的变长容器（C++26）

第二个是 `std::inplace_vector<T, N>`，C++26 进的标准（提案 P0843）。它填的是 `array` 和 `vector` 中间的缝：`array<T, N>` 大小编译期定死、不能变；`vector<T>` 能变长但要堆分配（扩容时 new 新块、拷贝、释放旧块）。很多时候你要的是「容量上限编译期知道、运行期 size 可变、但绝不碰堆」——`inplace_vector` 就是干这个的。它的元素**直接存在对象内部**（对象本身占 `sizeof(T) * N` 那块空间，放栈上或静态区），运行期可以在 0 到 N 之间增删，不 new、不扩容、不拷贝搬移。

最讨喜的一个性质是：**当 `T` 是 trivially copyable 时，`inplace_vector<T, N>` 本身也是 trivially copyable**。这意味着它可以整体 `memcpy`、可以放进寄存器、可以安全交给 DMA——这些对嵌入式和系统编程极重要，[array 深入](02-array.md) 讲过的「连续内存 + trivially copyable」红利，inplace_vector 同样吃到，而 `std::vector` 因为持有一个堆指针、不是 trivially copyable，是吃不到的。容量超限时的行为也设计得克制：`push_back` 超过 N 会抛 `std::bad_alloc`（异常关闭时退化为 terminate），而想避免异常可以用 C++26 的 `try_push_back`/`try_emplace_back`，它们超限时不抛、返回一个错误指示，适合 `-fno-exceptions` 环境。

```cpp
#include <cstdio>
#include <inplace_vector>

int main()
{
    std::inplace_vector<int, 4> v;     // 容量上限 4，绝不堆分配
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);                     // size 现在 3，还能再塞一个
    std::printf("size = %zu, capacity = %zu\n", v.size(), v.capacity());
    // 再 push 到满：v.push_back(4) 成功；v.push_back(5) 超容量，抛 bad_alloc
    // 想避免异常用 try_push_back / try_emplace_back——超限不抛，返回失败指示
    return 0;
}
```

```bash
g++ -std=c++26 -O2 -o /tmp/ipv_demo /tmp/ipv_demo.cpp && /tmp/ipv_demo
```

```text
size = 3, capacity = 4
```

`inplace_vector` 和 `array` 的边界要拎清：`array<T, N>` 的 size 恒等于 N，是定长；`inplace_vector<T, N>` 的容量上限是 N，但 size 在 0 到 N 之间运行期可变。要定长用 array，要「上限已知 + 运行期可变 + 不堆分配」用 inplace_vector。

## mdspan：span 的多维版（C++23，切片在 C++26）

第三个是 `std::mdspan`，C++23 进的标准（提案 P0009）。[span 深入](08-span.md) 讲过 `span` 是一维的连续内存视图，但现实里到处是二维三维数据——矩阵、图像、体素场、张量。过去只能拿一个一维指针手动算下标（`data[i * cols + j]`），既丑又容易把行列搞反。`mdspan` 把这种「一块连续内存 + 一个多维形状」包成一个视图类型，让你用多维下标 `m[i, j]` 直接访问，零拷贝、不持有数据、只描述「这块内存怎么按多维解释」。

它的模板参数有四个：元素类型、`Extents`（形状，每个维度多大）、`LayoutPolicy`（怎么把多维下标映射成一维偏移，默认 `layout_right` 即行优先，C/C++ 风格）、`Accessor`（怎么读写元素，默认裸访问）。形状用 `std::extents<IndexType, dims...>` 描述，维度大小编译期已知就填常量、运行期才知道就填 `std::dynamic_extent`；嫌麻烦可以直接用 `std::dextents<IndexType, Rank>`，表示「Rank 个维度全动态」。访问用 `m[i, j]` 这种**多维方括号下标**（靠 C++23 的多维 `operator[]` 语言特性 P2128），不是老的 `m[i][j]`——后者会让人误以为返回子视图，实际上 mdspan 直接把多维索引算成一维偏移、返回元素引用。这里有个容易踩的坑：注意是方括号 `m[i, j]`、不是函数调用 `m(i, j)`；早期 mdspan 参考实现（Kokkos）确实用 `operator()`，但 C++23 标准化后统一改成了多维 `operator[]`，这也是不少老教程和博客还在写 `m(i, j)` 的原因——照抄会编译不过。

```cpp
#include <mdspan>
#include <cstdio>

int main()
{
    int raw[12] = {
        1, 2, 3, 4,
        5, 6, 7, 8,
        9, 10, 11, 12,
    };
    // 把 12 个 int 当成 3 行 4 列的二维视图，行优先
    std::mdspan<int, std::extents<size_t, 3, 4>> m(raw);

    std::printf("m[1,2] = %d\n", m[1, 2]);   // 第 1 行第 2 列 = 7
    std::printf("m[2,3] = %d\n", m[2, 3]);   // 第 2 行第 3 列 = 12

    // 维度运行期才知道：用 dextents
    std::mdspan<int, std::dextents<size_t, 2>> d(raw, 3, 4);
    std::printf("d[0,0] = %d, rank = %zu\n", d[0, 0], d.rank());
    return 0;
}
```

```bash
g++ -std=c++23 -O2 -o /tmp/mdspan_demo /tmp/mdspan_demo.cpp && /tmp/mdspan_demo
```

```text
m[1,2] = 7
m[2,3] = 12
d[0,0] = 1, rank = 2
```

值得提一句的坑：**`submdspan`（切片）是 C++26，不是 C++23**。mdspan 在 C++23 落地时，切行、切列、切子块的功能没赶上，挪到了 C++26（P2630）。所以如果你想在 C++23 里取一行，还得自己算偏移；要等 C++26 工具链才能用 `std::submdspan(m, std::full_extent, slice)` 这种零拷贝切片。mdspan 的更大意义在于它是 `std::linalg`（线性代数库）的底座——后面的标准里，矩阵运算 API 都建立在 mdspan 之上。

## 还在路上：hive 等提案

最后说一个常被提及、但**还没进标准**的：`std::hive`（来自 Matt Bentley 的 `plf::hive`，提案 P0909/P2826）。它是个「节点容器」，设计目标是元素地址稳定（插删不影响其它元素的地址）、擦除快、遍历 cache 友好（用块组织节点而不是纯链表），适合那种「要长期持有指向元素的引用、又要频繁增删」的场景。截至 C++26 它仍是提案，没被采纳——想用现在只能上第三方 `plf::hive` 库。这里提一句是为了说明方向：标准委员会在认真考虑「比 list 更好用的节点容器」，但它还不是 `std::` 的一员，别在文章或简历里写成「C++26 的 hive」。

## 临了收几句

这一波新容器各填一个坑：`flat_map` 给「想要有序、又想要 cache 友好」的场景（代价是增删 O(n) 和像 vector 一样的失效）；`inplace_vector` 给「容量上限已知、运行期变长、绝不堆分配」的中间态（C++26，trivially copyable 的特性对嵌入式很香）；`mdspan` 给多维数据一个零拷贝的视图类型（C++23，切片 submdspan 要等 C++26）。三个都依赖较新的工具链，flat_map 要 C++23 库支持、inplace_vector 要 C++26，落地前先确认编译器和标准库版本。容器主线到这里就收尾了——从 `array` 到新标准容器，存数据的家伙事儿讲全了；接下来 vol3 会转向「遍历和操作数据」的迭代器与算法。

想直接上手运行看看效果？点开下面的在线示例（能运行、也能看汇编）：

<OnlineCompilerDemo
  title="新标准容器：flat_map / inplace_vector / mdspan"
  source-path="code/examples/vol3/10_new_containers.cpp"
  description="flat_map 排序 vector 查找、inplace_vector 定容不堆分配、mdspan 多维下标 m[i,j]（C++26）"
  allow-run
  run-compiler="g162"
  run-options="-O2 -std=c++26"
/>

## 参考资源

- [std::flat_map — cppreference](https://en.cppreference.com/w/cpp/container/flat_map)
- [std::flat_set — cppreference](https://en.cppreference.com/w/cpp/container/flat_set)
- [std::inplace_vector（C++26）— cppreference](https://en.cppreference.com/w/cpp/container/inplace_vector)
- [std::mdspan — cppreference](https://en.cppreference.com/w/cpp/container/mdspan)
- [std::submdspan（C++26，P2630）— cppreference](https://en.cppreference.com/w/cpp/container/mdspan/submdspan)
- [Details of std::mdspan from C++23 — C++ Stories](https://www.cppstories.com/2025/cpp23_mdspan/)
- [plf::hive（提案库参考）— GitHub](https://github.com/mattreecebentley/plf_hive)
