---
chapter: 7
cpp_standard:
- 11
- 17
- 20
- 23
description: 把 vol3 讲过的顺序容器与关联容器串成一张决策地图：按操作复杂度、内存局部性、迭代器失效规则三条线，加上一棵选择决策树，说清选错容器会踩的坑
difficulty: intermediate
order: 1
platform: host
prerequisites:
- array：编译期固定大小的聚合容器
reading_time_minutes: 11
related:
- vector 深入：三指针、扩容与迭代器失效
- deque、list 与 forward_list：vector 之外的三个选择
- map 与 set 深入
- unordered_map 与 set 深入
- span：非拥有的连续视图
tags:
- host
- cpp-modern
- intermediate
- 容器
- 内存管理
title: 容器选择指南：按操作、内存与失效规则挑对容器
---
# 容器选择指南：按操作、内存与失效规则挑对容器

## 这篇要解决什么：选错容器就是埋性能 bug

vol3 把主力容器逐个拆了一遍——`array`、`vector`、`deque`/`list`/`forward_list`、`map`/`set`、`unordered_map`/`unordered_set`、还有 `span`。每一篇都在讲「这个容器内部长什么样、为什么这么设计」，这篇反过来：站在「我现在要存一坨数据，到底该选哪个」的角度，把它们摆到同一张桌上比。容器选错很少当场崩，它只会让你的程序慢、让引用莫名其妙失效、让 hot loop 里反复扩容——这些都是最难查的性能 bug，因为代码「能跑」，只是跑得窝火。

挑容器其实就看三件事：**你要对它做什么操作（复杂度）、数据在内存里怎么摆（局部性）、改完之后手里的迭代器还能不能信（失效规则）**。这三条想清楚，剩下都是细节。我们按这三条线走一遍，最后给一棵决策树收口。

## 先分清两大阵营：顺序容器与关联容器

标准库容器先分成两大类，这个分法决定了你问的第一个问题不一样。**顺序容器**（`array`、`vector`、`deque`、`list`、`forward_list`）按「位置」存数据，元素在容器里的次序就是你放进去的次序，你关心的是「我要在第几个位置插、在第几个位置删」。**关联容器**（`map`/`set` 和它们的 `unordered` 版）按「键」存数据，元素的次序由键决定（有序）或由哈希决定（无序），你关心的是「我按什么来查」。

关联容器内部又分两小类。`map`/`set`/`multimap`/`multiset` 是**有序**的，底层是红黑树，按 key 排好序，查找是稳定的 `O(log n)`，还能按范围遍历。`unordered_map`/`unordered_set` 这一组是**无序**的，底层是哈希表，查找平均 `O(1)` 但最坏 `O(n)`（全撞同一个桶时），不能按序遍历。一句话区分：**要不要按 key 排序遍历？要，就红黑树；不要，就哈希换平均 O(1)**。这个权衡我们在 [map 与 set 深入](06-map-set-deep-dive.md) 和 [unordered_map 与 set 深入](07-unordered-map-set-deep-dive.md) 两篇里都实测过。

## 复杂度速查：按操作挑容器

把复杂度摊成一张表，挑容器时直接对照你要做的操作。注意表里说的都是「操作本身」的代价，定位（找到要操作的位置）通常要另算。

| 容器 | 随机访问 | 头部插删 | 尾部插删 | 中间插删 | 按 key 查找 |
|------|---------|---------|---------|---------|------------|
| `array` | O(1) | — | — | — | — |
| `vector` | O(1) | O(n) | 摊还 O(1) | O(n) | — |
| `deque` | O(1) | O(1) | O(1) | O(n) | — |
| `list` | O(n) | O(1) | O(1) | O(1)（已有迭代器） | — |
| `forward_list` | O(n) | O(1) | — | O(1)（已有迭代器） | — |
| `map` / `set` | — | — | — | O(log n) | O(log n) |
| `unordered_map` / `set` | — | — | — | 平均 O(1) | 平均 O(1) 最坏 O(n) |

这张表里有几个最容易误读的点，单独拎出来说。第一个是 `list` / `forward_list` 的「中间插入 O(1)」——这个 O(1) 只针对插入**动作本身**（链表改两个指针），前提是你**已经持有那个位置的迭代器**；如果你还得先从头遍历去找位置，定位那一步就是 O(n)，加起来还是 O(n)。很多人看到「list 插入 O(1)」就以为 list 适合频繁增删，其实绝大多数「频繁增删」的场景，定位成本和 cache 不友好会把 list 拖得比 vector 还慢。第二个是 `vector` 尾部那个「摊还 O(1)」——单次扩容确实 O(n)，但它分摊到 N 次 push_back 上每次还是常数，所以平均是 O(1)；只要记得 `reserve`，扩容次数能压到几乎为零。第三个是 `deque`，它头尾插删都是 O(1) 看着很美，但中间插删是 O(n)，而且代价还比 vector 更重（分段结构要搬动更多），所以 deque 是「两端频繁进出的队列」专属，别拿它当通用容器。

## 内存局部性：连续 vs 节点，性能的分水岭

复杂度表只能告诉你「渐进快慢」，但同样标着「O(1) 遍历」的两个容器，真实速度能差一个数量级——差距就在内存局部性上。存储方式决定了数据在内存里怎么摆，进而决定 CPU cache 命不命中。

顺序容器按存储方式分三档。`array`、`vector` 是**连续**内存，元素紧挨着放，遍历时一整个 cache line 一起进 L1，prefetcher 还能预取下一段。`deque` 是**分段连续**——内部是一组固定大小的块（chunk），块内连续、块间不连续，所以随机访问要算「第几块的第几个」，遍历在块内顺滑、跨块会断一下。`list` / `forward_list` 是**节点**存储，每个元素单独 new 一个节点，节点之间靠指针串起来，在内存里东一个西一个，遍历时几乎每次都要跳到一个新地址，cache 命中率极差。关联容器全是节点存储：红黑树一个节点、哈希表一个桶里挂一串节点，局部性都不如连续容器。

这个差距不是理论上的，跑一下就明白。

```cpp
#include <chrono>
#include <cstdio>
#include <list>
#include <vector>

int main()
{
    constexpr int N = 1'000'000;
    std::vector<int> v(N);
    std::list<int> l;
    for (int i = 0; i < N; ++i) {
        v[i] = i;
        l.push_back(i);
    }

    volatile long long sink = 0;

    auto t0 = std::chrono::high_resolution_clock::now();
    long long sv = 0;
    for (auto x : v) { sv += x; }
    sink += sv;
    auto t1 = std::chrono::high_resolution_clock::now();

    long long sl = 0;
    for (auto x : l) { sl += x; }
    sink += sl;
    auto t2 = std::chrono::high_resolution_clock::now();

    auto us_v = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    auto us_l = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    std::printf("vector 遍历 %lld us, list 遍历 %lld us, list 慢 %.2fx\n",
                us_v, us_l, us_v ? (double)us_l / us_v : 0.0);
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/cache_bench /tmp/cache_bench.cpp && /tmp/cache_bench
```

不想配环境？直接点开下面的在线示例跑这段 benchmark，看连续内存到底快多少：

<OnlineCompilerDemo
  title="连续 vs 节点：vector 与 list 遍历性能实测"
  source-path="code/examples/vol3/01_container_cache_benchmark.cpp"
  description="同样是 O(n) 遍历，连续内存的 vector 吃满 cache，节点式的 list 每个元素都要单独访存——实测两边耗时差几倍"
  allow-run
/>

跑下来 `vector` 遍历会比 `list` 快好几倍（具体倍数跟机器和 cache 大小有关，量级是数倍而不是百分之几）——两者遍历都是 O(n)、每次加法都是 O(1)，但 `vector` 的连续内存吃满 cache，`list` 的每个节点都要单独访存。这就是「为什么默认用 vector」的底层理由：在绝大多数「存一坨数据然后遍历」的场景里，连续内存带来的 cache 红利，远超过链表省下的那点搬移开销。**只有当你真的需要在已知位置频繁增删、且增删代价明显高于遍历代价时，list 才可能赢**——这个条件比直觉里苛刻得多。

## 迭代器失效速查：改了容器，手里的引用还能不能用

第三个维度是迭代器失效。你拿到一个迭代器或引用，然后对容器做了插入/删除，那个迭代器还能不能继续用？这直接决定你能不能「边遍历边删」「把引用存起来以后用」。下面这张表是 cppreference 上各容器「Iterator invalidation」小节的汇总，权威且值得背下来。

| 容器 | 插入（insert / push） | 删除（erase / pop） |
|------|----------------------|--------------------|
| `vector` / `string` | 发生重分配则全部失效；否则插入点之后的失效 | 擦除点及之后全部失效 |
| `deque` | **全部失效** | **全部失效** |
| `list` / `forward_list` | 不失效 | 仅被删元素失效 |
| `map` / `set` 等 | 不失效 | 仅被删元素失效 |
| `unordered_map` / `set` 等 | 触发 rehash 则失效；否则不失效 | 仅被删元素失效 |

这张表里要特别盯住 `deque` 那一行。很多人把 deque 当成「头尾能 O(1) 的 vector」来用，但 vector 在不扩容时 erase 只失效点之后的，而 **deque 任何 erase 都会让全部迭代器失效**——这是 deque 分段结构搬移块指针导致的。如果你在代码里「存了 deque 的迭代器，之后又 erase 了一下」，几乎一定踩坑。相对地，节点容器（`list`、`map`、`set` 及其 unordered 版）的最大福利就是**插入永不失效、删除只失效被删的那个**，所以它们天然支持「边遍历边按迭代器删」「长期持有指向元素的引用」。

还有个 unordered 容器专属的细节：rehash。`unordered_map` 在装填因子超过 `max_load_factor`（默认 1.0）时会 rehash（扩桶），这一下会让所有迭代器失效（但引用和指针**不**失效，这是标准明确保证的）。对策是提前 `reserve(n)` 把桶数撑够，既避免 hot loop 里反复 rehash，也避免迭代器突然失效。

## 选择决策树

把三条线拧成一棵树，从最该先问的问题往下走。

第一刀切在「大小编译期知不知道」：知道且不变，直接 `array`——零堆分配、能 constexpr、放静态区省 RAM，没有比它更便宜的。不知道、要变长，进第二刀。第二刀切在「是不是按键查找」：是，进关联容器分支——要按 key 有序遍历就用 `map`/`set`（O(log n)），只要平均 O(1) 查找就用 `unordered_map`/`unordered_set`（记得 reserve）；不是按键查找，进顺序容器分支。第三刀切在「频繁在哪插删」：频繁头尾进出，`deque`；只在尾部增长，`vector`（务必 reserve）；频繁在已知中间位置增删且不需要随机访问，`list`；以上都不沾，默认 `vector`。

```text
大小编译期已知且不变?
├─ 是 → array
└─ 否
   ├─ 按键查找?
   │  ├─ 要按 key 有序遍历 → map / set           (O(log n))
   │  └─ 只要平均 O(1) 查找   → unordered_map/set (记得 reserve)
   └─ 按位置存
      ├─ 频繁头尾进出     → deque
      ├─ 主要尾部增长     → vector (+ reserve)
      ├─ 已知位置频繁增删 → list (确认定位+cache 不是瓶颈)
      └─ 其余             → vector (默认)
```

两个补充。一是只要「借用一阵子」、不想转移所有权，用 `span`——它是「array/vector/C 数组的统一只读视图」，零拷贝传参的标配，详见 [span 深入](08-span.md)。二是 C++23 起有了新选项：想要「有序 + cache 友好」的 map，看 `flat_map`（底层是排序 vector）；想要「容量固定、绝不堆分配」的变长容器，看 C++26 的 `inplace_vector`——这俩我们放到 [新标准容器](10-new-containers-cpp23-26.md) 单独讲。

## 几个最常见的误选

把踩坑频率高的几个列一下，挑容器时先自检。第一种，**「增删多所以用 list」**——忽略了定位成本和 cache 不友好，绝大多数情况下 vector 加 erase 反而更快，list 只在你确实长期持有大量迭代器、且增删远多于遍历时才划算。第二种，**unordered 容器不 reserve**——往里塞 N 个元素却不 `reserve(N)`，中间会触发多次 rehash，每次 rehash 重哈希全部元素，hot path 上白白浪费。第三种，**vector 反复 push_back 不 reserve**——同理，扩容时整块搬移，reserve 一下能消掉绝大部分拷贝。第四种，**跨容器传引用不看失效规则**——尤其在 deque 上存了迭代器又改了容器，或者对着 vector 边遍历边 erase 不更新迭代器，这类 bug 编译器不会提醒，运行时才炸。

## 临了收几句

挑容器，先把三件事问清楚：操作复杂度、内存局部性、迭代器失效。这三条对得上，八九不离十；细节（异常安全、自定义分配、异构查找）再回到各容器的深入篇看。一个朴素但好用的默认值：**拿不准就 vector**，它连续、尾部摊还 O(1)、接口最全，是覆盖面最广的安全牌，等你量出它真的成了瓶颈再换。下一篇我们进入容器适配器——`stack`、`queue`、`priority_queue`，它们不是新容器，而是把底层容器「包」成栈/队列/堆的接口外壳。

想直接上手运行看看效果？点开下面的在线示例（能运行、也能看汇编）：

<OnlineCompilerDemo
  title="容器选择：按位置存 vs 按键查"
  source-path="code/examples/vol3/01_container_selection.cpp"
  description="顺序容器（vector/list）与关联容器（map/unordered_map）的不同操作代价，呼应选择决策树"
  allow-run
/>

## 参考资源

- [容器库总表（含迭代器失效说明）— cppreference](https://en.cppreference.com/w/cpp/container)
- [容器迭代器失效规则（按操作分）— cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
- [std::vector 的 Iterator invalidation 小节 — cppreference](https://en.cppreference.com/w/cpp/container/vector#Iterator_invalidation)
