---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: 从 std::map 的红黑树实现切入,讲清每节点 malloc + cache miss 的小 N 痛点,
  以及 flat_map 用有序 vector + 二分如何换一条路
difficulty: intermediate
order: 0
platform: host
prerequisites:
- WeakPtr 前置知识（零）：弱引用与生命周期难题
reading_time_minutes: 10
related:
- flat_map 实战（一）：动机与接口设计
- flat_map 前置知识（一）：std::vector 内部表示与扩容
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 优化
title: "flat_map 前置知识（零）：有序关联容器与 std::map 的红黑树"
---
# flat_map 前置知识（零）：有序关联容器与 std::map 的红黑树

您随手写一句 `std::map<std::string, Config>` 存配置表,八成不会多想它底层长什么样——`O(log n)` 查找嘛,教科书说合理就合理。可要是这张表就十来个条目,启动时构造好、之后再也不动,您真去 profile 一把,会发现它比想象中慢不少。慢不在那个 `O(log n)`,在渐近复杂度藏不住的地方:每存一个 key-value 都得单独 `malloc` 一个节点,查找时在堆上跳来跳去,踩的几乎全是 cache miss。

Chromium 在 `//base` 里另起了一套有序关联容器——`flat_map`、`flat_set`——路子完全反过来:用一段连续的有序数组装所有元素,查找走二分。渐近复杂度还是 `O(log n)`,但数据挨在一起,CPU 一条 cache line 顺带就把十几个元素全捎进 L1 了,常数因子小一大截。代价当然有,插入删除退化成 `O(n)`(要挪数组)。咱们这一篇就把 std::map 的红黑树拆开看,讲清楚 flat_map 到底为什么要换这条路。

## 先把"关联容器"这词钉在哪儿

关联容器(associative container)跟序列容器的分界,说白了就一句:序列容器按位置访问,您要的是第 0 个、第 1 个;关联容器按键访问,您喊的是 `m.find("timeout")`,要的是 key 对应的那条 value。这玩意儿标准库给了两套——无序的 `std::unordered_map`(哈希表,平均 `O(1)` 查找)和有序的 `std::map`(红黑树,`O(log n)` 查找)。

咱们这篇只盯着有序的那一类。一来 flat_map 本身就是有序的,二来"有序"是个有分量的不变量:它能按 key 顺序遍历、能拿 `lower_bound` 框一段范围、能做前驱后继查询——这些哈希表统统做不到,无序就是无序。所以"有序关联容器到底该怎么实现"这个问题值得认真想:标准库给的答案是红黑树,Chromium 给的是有序数组。两条路都摆出来看看。

## std::map 的红黑树实现

主流三大实现(libstdc++、libc++、MSVC)的 `std::map` 底层都是红黑树,一种自平衡二叉搜索树。每个 key-value 对占一个树节点,64 位下节点长这样:

```text
struct Node {
    color      color_;       // 1 字节(红/黑,平衡用)
    Node*      left_;        // 8 字节
    Node*      right_;       // 8 字节
    Node*      parent_;      // 8 字节
    pair<K,V>  data_;        // 你的 key + value
};
```

光是指针加颜色就 25 字节起步(算上对齐填充,实际通常 32 字节),这还没算您的 key-value。换句话说,每存一个元素,除了数据本身,您还得替它另外付 32 字节的节点元数据。

查找是教科书那套二叉搜索:从根开始比 key,小往左大往右。红黑树自平衡,树高 `O(log n)`,所以查找就是 `O(log n)` 次比较。单看渐近复杂度,合理。

合理归合理,坑藏在"每次比较前先把节点搬到 cache"这一步。红黑树节点是逐个堆分配的——您 `insert` 一次,底层 `new` 一个 Node。100 万元素的 `std::map<int,int>`,光节点就 100 万次堆分配,返回的地址在堆上散得到处都是。查找的时候更扎手:从根 `node = node->left_` 这一跳,要去解引用一个之前根本没碰过的地址。CPU 流水线没法预取它(目标地址得等上一次加载完才知道),L1/L2 里也没有——这一跳就是一次 cache miss,几十上百个周期就出去了。`O(log n)` 次比较,每次都可能踩一次 miss,这才是 `std::map::find` 真实付出的代价。

## 真正的病根:常数因子

咱们在这儿停一下,把事情抠清楚,因为这是整个 flat_map 故事的根。

`std::map::find` 是 `O(log n)`,`flat_map::find` 也是 `O(log n)`,渐近复杂度一模一样。可"渐近一样"从来不等于"一样快"——大 O 记号故意把常数因子抹掉了,而常数因子是由"每次比较实际花多少"决定的。

对 std::map,每次比较前都得先把节点从内存搬进 cache。节点散在堆上,每跳一次大概率就是一次 miss。比较这个动作本身(比如两个 int 比大小)1 个周期就完事,可等节点从内存那头过来要 100+ 个周期——比较的"成本"几乎全耗在等 cache miss 上,真比大小的那 1 个周期可以忽略。

flat_map 走另一头:所有元素挨在一起。CPU 从内存捞数据按 cache line 捞(x86 一条 64 字节),您访问 `data[0]`,隔壁的 `data[1]`、`data[2]`…一票全免费跟着进 L1。二分查找虽然跳着访问(`mid = n/2`),但总有一段连续区域是热的,每次比较基本都吃 cache,1 个周期了事。

所以同样是 `O(log n)`,在"小到中等数据量"这个档,flat_map 的常数因子能比 std::map 小一个数量级。Chromium 造这个轮子,赢的不是渐近复杂度,是常数因子。

## 换条路:有序数组 + 二分

flat_map 的核心心思一句话讲完:别用树,用一段有序的连续数组,查找用二分。

```text
flat_map<int,std::string>:
  data_:  [ (1,"a") | (3,"c") | (7,"g") | (9,"i") | ... ]   ← 一段连续的有序 vector
                    查找用 std::lower_bound(二分,O(log n))
```

查找走 `std::lower_bound`,在有序数组上二分,`O(log n)`——渐近跟 std::map 一样,但因为数据连续,cache 友好得多。代价在插入和删除:在中间塞一个,后面整段得往后挪一格,`O(n)`;对应 std::map 的 `O(log n)` 插入。存储这边就一个 vector,0 额外节点元数据,一次连续分配。这就是 flat_map 的全部骨架,把"红黑树 vs 有序数组"那组经典权衡原原本本摆台面上:树拿空间局部性换 `O(log n)` 插入,数组拿插入复杂度换空间局部性。

那数组什么时候赢?查多写少的时候。

最典型的就是配置表、查表、命令分发表这类:启动时构造一次,往后基本只查,极少再插再删。对这种"写一次读多次"的负载,flat_map 那个 `O(n)` 插入只在构造期发生一回(还能批量优化成 `O(N log N)` 一次排序到位,见 03-4),之后全是 `O(log n)` 的 cache 友好查找。std::map 呢,每次查找都得付那个 cache miss 的常数因子。两边写入都是一次性的、不分高下,可读取 flat_map 快得多——这块场景里它几乎是白赚。

反过来,要是您的集合又大又频繁变动(比如一个不停增删的索引),flat_map 每次 `O(n)` 插入就开始疼了,那是 std::map 的主场。Chromium 自己的容器选择指南就划得这么直白:写一次读多次用 flat_map,写多次且量大用 std::map。

## Chromium 的取舍,标准库的跟进

flat_map 不是 Chromium 凭空想出来的。sorted-vector map 历史不短——Alexandrescu 早在 2001 年《Modern C++ Design》就给过 `Loki::AssociationVector`,Boost.Container 也长期挂着 `boost::flat_map`。Chromium 2017 年把这套搬进 `//base`,顺手做了 Chromium 风格的特化(`DCHECK`/`CHECK` 校验、`raw_ptr_exclusion`、默认透明比较器)。

有个细节笔者觉得值得拎出来说。Chromium 的 flat_map 把 key-value 挤在一个数组里(`vector<pair<K,V>>`);而 C++23 入库的 `std::flat_map`(P0429 提案)走的是键值分离(split storage)——keys 和 values 各开一个连续数组。分离的好处是只遍历键时 cache 更密,value 不来插一脚;代价是实现复杂,得维护两套容器同步。Chromium 选了不分离的简单路子——"看起来更优"的 split 方案被工业界主力搁下了,实现复杂度换回来的那点收益不划算。这分歧咱们留到 03-6 性能对比再细抠。

底座这层就到这儿。flat_map 默认拿 vector 存数据,所以下一步得先把 `std::vector` 的三指针、扩容、迭代器失效摸透——那是理解 flat_map 行为的前提。

## 参考资源

- [Chromium `base/containers/flat_map.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [Chromium `base/containers/README.md` —— 容器选择指南](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [cppreference: std::map(红黑树实现说明)](https://en.cppreference.com/w/cpp/container/map)
- [P0429 —— std::flat_map 提案(C++23)](https://wg21.link/p0429)
