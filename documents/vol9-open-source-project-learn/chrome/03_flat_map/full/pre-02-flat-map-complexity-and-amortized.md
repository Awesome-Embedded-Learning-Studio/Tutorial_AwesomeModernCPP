---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: 讲清大 O、单次 vs 摊还复杂度,落到 flat_map 的 O(lg n) 查找、O(n) 插入、
  range 构造的 O(N lgN),并用真实实测佐证 shift 代价
difficulty: intermediate
order: 2
platform: host
prerequisites:
- flat_map 前置知识（一）：std::vector 内部表示与扩容
reading_time_minutes: 10
related:
- flat_map 实战（三）：查找与插入
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 优化
title: "flat_map 前置知识（二）：复杂度与摊还分析"
---
# flat_map 前置知识（二）：复杂度与摊还分析

[pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md) 那会儿笔者甩出一句"flat_map 是 `O(log n)` 查找、`O(n)` 插入",[pre-01](./pre-01-flat-map-vector-internals-and-growth.md) 又说 vector `push_back` 是摊还 `O(1)`。这两句话里头藏着一个很容易被您忽略、但又会真金白银决定 flat_map 性能的区分——单次代价和摊还代价是两回事。这一篇笔者就想把这个工具本身给咱们掰开,因为 flat_map 后面所有性能结论,根都扎在这套分析上。理解透了,您自然就能判断什么时候该上 flat_map、什么时候用了是给自己挖坑。

## 大 O 记号:渐近复杂度

大 O 记号描述的是操作代价随输入规模 `n` 增长的方式,常数因子和低阶项一律忽略。最常用的几档:`O(1)` 是常数时间,跟 `n` 无关,比如 `vector::size()` 直接读个字段;`O(log n)` 是对数时间,有序数组二分查找每步把范围砍半就是这一档;`O(n)` 是线性,跟 `n` 成正比,遍历或者在数组中间插一个要挪后面全部元素都算;`O(n log n)` 常见于排序,或者对 N 个元素逐个二分插入;`O(n²)` 就是更狠的,对 N 个元素逐个在头部插,每次付 `O(n)`、共 N 次。

大 O 回答的是"`n` 趋向无穷时谁赢"。但 [pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md) 笔者已经提醒过,大 O 把常数因子扔了,而真实程序里常数因子——也就是每次操作到底烧多少周期——能差出一个数量级。这正是 flat_map 在小 N 下能赢 std::map 的根:两者查找渐近都是 `O(log n)`,可 flat_map 是连续存储,cache 一行能装好几个元素,std::map 的红黑树节点散在堆上,一趟指针跳下来 cache miss 一堆。所以看复杂度结论时,大 O 只是上半场,下半场是常数因子,在咱们这套语境里主要就是 cache 行为。

## 单次 vs 摊还:一个关键区分

这一篇的核心就在这里。同一个操作,有两种复杂度口径:单次(single)看的是做这一次最坏要多少时间;摊还(amortized)看的是连续做 N 次平均每次多少时间,把偶尔冒出来的大代价摊平到 N 次上。

vector 的 `push_back` 是教科书级例子。单次最坏 `O(n)`——触发扩容,得把现有元素全部搬一遍;摊还却是 `O(1)`,因为扩容按 2 倍几何增长,扩完之后接下来 N 次都不用再扩,那一次 `O(n)` 摊到 N 次上,平均下来每次常数。咱们日常觉得 `push_back` 很快,就是不知不觉吃了这个摊还优惠。

### flat_map 单元素插入没有摊还优惠

问题来了,flat_map 的 `insert(key, value)` 享受不到这茬。回忆 [pre-01](./pre-01-flat-map-vector-internals-and-growth.md):flat_map 得保持有序,`insert` 先 `lower_bound` 找到位置(多半落在数组中间),然后**把那个位置之后的所有元素往后挪一格**。这次挪动是实打实的 `O(n)`,而且每次插入都得挪,不是偶尔来一次。

所以 flat_map 的单元素插入单次是 `O(n)`,摊还还是 `O(n)`——因为每次都付 `O(n)`,根本没有"偶尔一次大代价"可以摊平。连续做 N 次单元素 `insert`,总代价堆到 `O(n²)`。这就是"逐个 insert 构造一个大 flat_map"为什么是陷阱的根,后面 03-5 笔者会专门讲怎么用批量构造绕开它。

## O(lg n) 查找:二分

flat_map 的查找——`find`、`contains`、`lower_bound`、`equal_range`——全部 `O(log n)`,靠的就是有序数组上的二分。以 `lower_bound` 为例(flat_tree.h:1027 用 `std::ranges::lower_bound`):

```cpp
// 在有序数组 [first, last) 里找第一个不小于 key 的位置
auto it = std::ranges::lower_bound(data, key, comp);
```

二分每步把搜索范围砍半,所以 `n` 个元素最多比较 `log₂(n)` 次。100 万元素大约 20 次比较,每次比较命中 cache(连续存储)只要 1 到 2 个周期,查找的总代价极小。flat_map 查找快就快在这:不光是 `O(log n)`,而且每一次比较本身都便宜。

`find`、`contains`、`lower_bound`、`equal_range` 这几个接口语义和 std::map 一致,flat_map 全从 flat_tree 继承——`find(key)` 是精确查找,等于 `lower_bound` 之后再判一次相等;`contains(key)` 就是 `find != end`;`lower_bound(key)` 给第一个 `>= key` 的位置;`equal_range(key)` 给 `[lower_bound, upper_bound)` 区间。区别只在底层把树遍历换成了二分。

## O(n) 插入:shift 的代价

flat_map 的插入(`insert`/`emplace`/`operator[]`/`insert_or_assign`)走的是同一条路(flat_tree.h:1060 `unsafe_emplace`):先用 `lower_bound` 找到插入位置,这是 `O(log n)`;然后在那位置上来一发 `vector::emplace`,把后面所有元素往后挪一格,这一下就是 `O(n)`。总复杂度被 shift 主导,落地 `O(n)`。erase 同理(flat_tree.h:914/921 `body_.erase`),删一个,后面所有元素往前挪,`O(n)`。

### 实测:shift 到底有多贵

光说 `O(n)` 不够直观,笔者跑了个实验。在 vector 头部插 10 万次(`emplace(begin)`),每次都得把后面所有元素挪一格:

```text
10 万次 vector::emplace(begin)  →  264 ms   (O(n²) 总代价)
10 万次 vector::push_back       →  0 ms      (摊还 O(1))
```

两个数量级的差距。把 flat_map 当 std::map 那样频繁在中间插,您看到的就是这条 264ms 的曲线。flat_map 的 `O(n)` 插入不是教科书上吓唬人的理论警告,是实打实会撞上去的性能墙。

## range 构造:O(N lg²N) → O(N lgN)

但 flat_map 有一条便宜的构造路径。您要是能一次性喂给它一坨数据(比如从 `vector<pair<K,V>>` move 构造),它就用不着逐个 insert——而是先把元素全部 append 进去,再一次性排序去重(`sort_and_unique`,flat_tree.h:147-149):

```text
flat_map 构造(N 个元素):
  1. append 所有元素         O(N)
  2. sort_and_unique:
       std::stable_sort      O(N log N)
       unique + erase        O(N)
  总计                       O(N log N)  (有额外内存时;否则 O(N log²N))
```

`stable_sort` 在有额外内存可用时是 `O(N log N)`——可以开一块临时缓冲做归并;内存不够时退化到 `O(N log²N)`,原地归并每层得付 `O(N log N)`。所以 flat_map 的批量构造是 `O(N log N)`,远比"逐个 insert 的 `O(N²)`"便宜。这就是"写一次"场景下 flat_map strictly better 的实现依据。

### sorted_unique:跳过排序

再往前一步,如果您能保证输入已经有序且无重复,可以用 `sorted_unique_t` 标签构造(flat_tree.h:606-646),flat_map 连 `sort_and_unique` 都不调,直接接管,构造变成 `O(N)`。这是零成本抽象的典范,笔者留到 pre-04 和 03-4 专门讲。

## 复杂度总表

flat_map 的复杂度结论汇总在下面这张表里,全部在 flat_tree.h 源码注释里有据可查:

| 操作 | 复杂度 | 备注 |
|---|---|---|
| 查找 find/contains/lower_bound/equal_range | `O(log n)` | 二分,cache 友好 |
| 单次 insert/emplace | `O(n)` | 含 shift,无摊还 |
| erase(position/range) | `O(n)` | shift |
| erase(key) | `O(n) + O(log n)` | 先找再删 |
| operator[]/insert_or_assign/try_emplace | `O(n)` | 同 insert |
| range 构造(普通) | `O(N log²N)` / `O(N log N)` | 取决于是否有额外内存 |
| range 构造(sorted_unique) | `O(N)` | 跳过 sort_and_unique |
| reserve/shrink_to_fit | `O(n)` | realloc,失效迭代器 |

横向对比 std::map(红黑树):查找 `O(log n)`,渐近跟 flat_map 平手,但常数因子吃亏;insert/erase `O(log n)`,渐近上赢 flat_map。所以渐近复杂度这条线,std::map 在插入上赢;常数因子这条线,flat_map 在查找上赢。落到大白话就是——查多写少上 flat_map,大且频繁改上 std::map。

下一篇咱们看 flat_map 的比较器:它怎么决定元素顺序,以及现代的"透明比较器"怎么省掉临时对象构造。

## 参考资源

- [cppreference: std::lower_bound(二分查找)](https://en.cppreference.com/w/cpp/algorithm/lower_bound)
- [cppreference: 复杂度(摊还分析)](https://en.cppreference.com/w/cpp/language/complexity)
- [Chromium `base/containers/flat_tree.h` —— 复杂度注释](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
