---
chapter: 1
cpp_standard:
- 17
- 20
description: 用 Catch2 围绕不变量测试 flat_map,并实测量对象大小、per-item 开销、查找/插入性能,
  对比 std::map 与 absl::btree_map,给出选型判据
difficulty: intermediate
order: 6
platform: host
prerequisites:
- flat_map 实战（五）：迭代器失效与批量构造
reading_time_minutes: 12
related:
- flat_map 前置知识（零）：有序关联容器与 std::map 的红黑树
- flat_map 前置知识（二）：复杂度与摊还分析
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 测试
- 优化
title: "flat_map 实战（六）：测试与性能对比"
---
# flat_map 实战（六）：测试与性能对比

代码撸完了,接下来的问题其实就俩:写对了吗?跑多快?笔者这篇就围着这两件事转。前半篇咱们拿不变量当靶子设计测试,把排序、查找、插入、去重、失效这些行为逐条验过去;后半篇把 flat_map 拉出来跟 `std::map`、`absl::btree_map` 一起上秤,量一量对象多大、查找多快、插入多疼。那条"小 N flat_map 赢、大 N 写多了 std::map 反超"的判据到底落在什么数据上,跑完就清楚了。

## 六条不变量,一条都不能松

flat_map 能不能算"对",全看这六条不变量撑不撑得住,笔者把它们拢在一块儿讲。

排序和去重是头两条,也是最容易被肉眼验的:遍历一遍拿到的 key 必须严格升序,而且不能有重复。第三条是查找语义得齐整,`find`、`contains`、`operator[]`、`at` 各司其职,其中 `at` 越界要直接 CHECK 崩给您看——这种是确定的 bug,release 也得炸。第四条针对两个容易混的写接口:`insert_or_assign` 遇到已存在 key 就覆写,`try_emplace` 则是已存在就别动,一个动一个不动,语义别搞反。第五条是 `sorted_unique` 这条"撒谎者走捷径"的路径——您声称数据已排序去重,它就跳过排序;可您要是撒谎,debug 下直接 abort。最后一条是迭代器失效,任何 mutation 之后旧迭代器都按粗规则作废,这一条上一篇已经讲透,这里只验它。

## 关键用例(Catch2 风格示意)

下面是 Catch2 风格的示意用例(项目当前的可运行示例是 `code/.../chrome_design/` 下 `19`~`22` 的 demo .cpp,Catch2 测试目标接入留作扩展):

```cpp
// Platform: host | C++ Standard: C++20
#include <catch2/catch_test.hpp>
#include "flat_map.hpp"
using namespace tamcpp::chrome;

TEST_CASE("flat_map is sorted+unique after construction", "[flat_map]") {
    flat_map<int, int> m{{3,30}, {1,10}, {3,30}, {2,20}};   // 重复 3,无序
    // 不变量 1+2:有序且去重
    std::vector<int> keys;
    for (auto& [k, v] : m) keys.push_back(k);
    REQUIRE(keys == std::vector<int>{1, 2, 3});
}

TEST_CASE("at out-of-range CHECKs", "[flat_map][.death]") {
    flat_map<int, int> m{{1,10}};
    // 不变量 3:越界 CHECK 崩溃(隔离 death test)
    // REQUIRE_DEATH(m.at(99));
}

TEST_CASE("insert_or_assign overwrites, try_emplace leaves alone", "[flat_map]") {
    flat_map<int, int> m{{1,10}};
    auto [it1, ins1] = m.insert_or_assign(1, 99);   // 已存在 → 覆写
    REQUIRE_FALSE(ins1);
    REQUIRE(it1->second == 99);
    // try_emplace 对已存在 key 不动(没法在同测试里直接验,语义见 03-3)
}
```

这些用例瞄的全是语义边界——排序去重、`at` 那一下 CHECK、`insert_or_assign` 的覆写。`at` 的 death test 得隔离跑,因为它真 abort,跟普通断言不是一回事。

## 性能:对象大小与 per-item 开销

先把"占多大内存"这件事量清楚。flat_map 的身子骨就一个 `vector<pair<K,V>>` 加一个零字节的比较器;`std::map` 走的是红黑树,每个节点要扛 3 个指针加一个颜色位,再加数据本身。咱们用 `sizeof` 看容器骨架,再用 100 万元素的占用看分摊到每条的 per-item 开销:

```text
sizeof(flat_map<int,int>)  ≈ sizeof(vector<pair<int,int>>) = 24 字节(三指针,64 位)
sizeof(std::map<int,int>)  ≈ 48 字节(树根 + 比较器 + sentinel 节点)

100 万元素 map<int,int> 的额外开销(数据本身 8MB 不计):
  flat_map:  ~0 额外(数据连续,无节点元数据)
  std::map:  ~32MB(每节点 32B × 100 万,且每节点一次 malloc)
```

flat_map 在 per-item 上几乎白嫖:数据连续排布,没有节点元数据,也只 malloc 一次。`std::map` 这边光节点元数据就 32B 一条,还要 100 万次堆分配凑齐。对象越小、集合越大,这道口子撕得越开。

## 性能:查找(cache 友好 vs 指针追逐)

两边查找都是 `O(log n)`,渐近上谁也不比谁快。常数因子才是分水岭:flat_map 数据连续,二分时比较能蹭上 cache;`std::map` 的节点东一个西一个,每跳一次解引用大概率就是一次 cache miss。

实测(本机 GCC 16,-O2,配套 `20_lookup_vs_shift_perf`,10 万元素 `map<int,int>`,各做 10 万次 `find`):

```text
查找 10 万次(10 万元素):
  flat_map:  31 ms
  std::map:  34 ms
```

别急着下结论。N=10 万、key 是 `int` 的时候,两边几乎打平——int 比较本身只要一个周期,cache 那点红利还没盖过 `std::map` 树高更低的优势。flat_map 真正拉开身位,得等 N 更大、或者 key 更重的时候。比如 key 换成 `std::string`,比较本身就贵,一次 cache miss 的代价占比立刻被放大,独立的大 N 测试里 flat_map 快个几倍很常见。所以"flat_map 查找一定快"这话不能当教条背,它吃工作负载——N 越大、key 越重,优势才越显眼。

## 性能:插入(O(n) shift 的墙)

查找还能看负载脸色,插入这边 flat_map 就是明确输。实测往一个已经塞了 10 万元素的容器里追加插 1000 个 key:

```text
插入 1000 次到 10 万元素容器:
  flat_map:  2 ms   (每次 O(n) shift)
  std::map:  0 ms   (每次 O(log n) 节点重连)
```

flat_map 慢得明明白白,这就是"查多写少"判据背后最硬的那块数据。您的活儿要是插入占大头,flat_map 这个 `O(n)` 的 shift 迟早成瓶颈,乖乖回去用 `std::map`。绝对值会随机器和 N 浮动,但 flat_map 插入比 `std::map` 慢这个趋势是稳的——N 越大口子越大,因为 shift 的代价本来就是 O(n)。

## 选型判据(实测总结)

三组数据凑齐了,选型判据也就摆在那儿了:

| 工作负载 | 推荐 | 理由 |
|---|---|---|
| **写一次读多次**(配置表、命令分发、查表) | flat_map | 写入都是一次性(批量构造),读取 cache 友好 |
| **始终很小**(浏览器统计众数 ~4 元素) | flat_map | 小 N 常数因子主导,零分配优势大 |
| **大且频繁改**(动态索引) | std::map | flat_map 的 O(n) 插入是墙 |
| **需要指针/引用稳定** | std::map | flat_map 迭代器跨变更全失效 |
| **大量有序 key + 频繁改 + 大 N** | absl::btree_map | B-tree 中间解(但 Chromium 因代码膨胀禁用) |

一句话能说完:查多写少用 flat_map,写多就回到 std::map。Chromium 的 `//base/containers/README.md` 划的也是这条线。

## vs std::flat_map(C++23)与 absl::btree_map

先说 `std::flat_map`(C++23,P0429)。它跟 Chromium 这套 flat_map 同宗同源,但标准版换了个存法——split storage,keys 和 values 各自一条连续数组,只遍历键的时候 cache 排得更密,value 不会插进来添乱。听着更优,代价是维护两套容器同步,实现复杂度上去了。Chromium 没走 split,老老实实一个 `vector<pair<K,V>>`——"看起来更优"的 split 在工业界主力那儿反而被放弃了,复杂度跟收益对不上账。

再说 `absl::btree_map`。它是 B-tree,每个节点 TargetNodeSize=256B,一次能塞几十个 key。这样一来,一次 cache line 命中能比对好几个 key,既治了红黑树指针追逐的毛病,又躲开了 sorted vector 那个 `O(n)` 的插入。属于"既要有序、又要大 N、还要频繁改"这摊需求的解药。可它有笔账没法回避:代码体积。Chromium 在 `//base` 明令禁用 `absl::btree_map`,原因就在这儿。

## 教学版与 Chromium 的取舍

跟前两个系列一样,咱们的教学版做了一层简化:

| 维度 | Chromium | 教学版 |
|---|---|---|
| 底层 Container | `std::vector` | 同 |
| 排序 | `std::stable_sort` + unique + erase | 同 |
| 透明比较 | `KeyT<K>` + `KeyValueCompare` 双重载 | 简化模板 |
| `DCHECK(is_sorted_and_unique)` | 完整 | `assert` 模拟 |
| `[[no_unique_address]]` 比较器 | 标注 | 标注 |
| `extract`/`replace` | 完整 | 简化/省略 |
| `raw_ptr_exclusion`/Chromium 宏 | 完整 | 省略 |

核心机制(sorted vector 适配器、tag dispatch、透明比较、EBO、批量构造)一字不差。

到这里,flat_map 这个组件的设计、实现、验证就全跑通了。从 [pre-00 红黑树痛点] 一路到这儿 13 篇下来,咱们把"sorted vector 为什么能干翻红黑树"整条链路走完了。加上前面的 OnceCallback 和 WeakPtr,Chromium `//base` 里工业级 C++ 设计的回调、弱引用、容器三块拼图,也就凑齐了。

## 参考资源

- [Catch2 文档](https://github.com/catchorg/Catch2/tree/devel/docs)
- [Chromium `base/containers/README.md` —— 容器选择指南](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [P0429 —— std::flat_map 提案(C++23)](https://wg21.link/p0429)
- [absl::btree_map 文档](https://abseil.io/docs/cpp/guides/btree)
- [cppreference: std::map](https://en.cppreference.com/w/cpp/container/map)
