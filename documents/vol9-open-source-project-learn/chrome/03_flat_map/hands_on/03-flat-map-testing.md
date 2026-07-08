---
chapter: 1
cpp_standard:
- 17
- 20
description: flat_map 的测试策略——围绕不变量设计用例,并实测 per-item 开销、查找/插入性能,
  给出 flat_map vs std::map/absl::btree_map 的选型判据
difficulty: advanced
order: 3
platform: host
prerequisites:
- flat_map 设计指南（二）：逐步实现
reading_time_minutes: 6
related:
- flat_map 设计指南（一）：动机、接口与 flat_tree 架构
- flat_map 实战（六）：测试与性能对比
tags:
- host
- cpp-modern
- advanced
- 容器
- map
- 测试
- 优化
title: "flat_map 设计指南（三）：测试策略与性能对比"
---
# flat_map 设计指南（三）：测试策略与性能对比

上一篇撸完实现,笔者心里其实没那么踏实——`flat_tree` 那一坨 `lower_bound + emplace + shift` 能编过是一回事,语义对不对是另一回事。容器这种东西最怕“看起来能跑”:您塞几个数进去遍历出来有序,绿了;可重复 key 去重、`insert_or_assign` 的覆写、`sorted_unique` 撒谎、迭代器失效这些坑,全压在边界上。这一篇咱们就把第一篇承诺的六条不变量一条条摁回测试里,再拿真实测量跟 `std::map` 和 `absl::btree_map` 比一比,看 flat_map 到底省在哪、又把代价付在了哪。套路跟 [WeakPtr 设计指南（三）](../../02_weak_ptr/hands_on/03-weak-ptr-testing.md) 一脉相承:不变量驱动,数据说话,不空口。

## 不变量 → 测试矩阵

| # | 不变量 | 断言 |
|---|---|---|
| 1 | 有序 | 遍历得到严格升序 key |
| 2 | 唯一 | 重复 key 被去重 |
| 3 | 查找语义 | find/contains/operator[]/at 对;at 越界 CHECK/assert |
| 4 | insert_or_assign/try_emplace | 已存在一个覆写一个不动 |
| 5 | sorted_unique | 跳过排序;撒谎 debug abort |
| 6 | 迭代器失效 | mutation 后旧迭代器失效(粗规则) |

## 关键用例(Catch2 风格示意)

六条不变量听着抽象,落到测试上其实就挑那些“写错了一定会爆”的边界。笔者这里挑三条最能锁语义的——构造期有序去重、`insert_or_assign` 的覆写、`sorted_unique` 撒谎 debug abort。配套工程在 `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/` 下 `19`~`22` 那几个 demo .cpp,Catch2 测试目标接入留作扩展,这里先看用例长什么样:

```cpp
TEST_CASE("flat_map sorts+uniques on construction", "[flat_map]") {
    flat_map<int,int> m{{3,30},{1,10},{3,30},{2,20}};
    std::vector<int> keys;
    for (auto& [k,v] : m) keys.push_back(k);
    REQUIRE(keys == std::vector<int>{1,2,3});   // 不变量 1+2
}

TEST_CASE("insert_or_assign overwrites existing", "[flat_map]") {
    flat_map<int,int> m{{1,10}};
    auto [it, ins] = m.insert_or_assign(1, 99);
    REQUIRE_FALSE(ins); REQUIRE(it->second == 99);   // 不变量 4
}

TEST_CASE("sorted_unique aborts on lying input", "[flat_map][.death]") {
    // 不变量 5:传未排序数据却宣誓 sorted_unique → debug abort
    // flat_map<int,int> m(sorted_unique, std::vector<std::pair<int,int>>{{3,3},{1,1}});
}
```

这三条盯的都是语义边界,不是 API 表面。构造期那条把不变量 1 和 2 一起验——丢进去的 `{3,30}` 重复了一份、顺序也是乱的,遍历出来必须正好是 `1,2,3`,差一个就是 `sort_and_unique` 写错了;`insert_or_assign` 那条更细,笔者特意拿 `ins` 的 false 跟 `it->second==99` 对照,就是怕把“插入了新的”和“覆写了旧的”混成一回事。`sorted_unique` 撒谎那条单独拎出来,因为它会 abort。

会 abort 的用例有个麻烦:直接塞进普通 TEST_CASE 里跑,整个二进制都得跟着挂。得隔离成 death test,让它在子进程里崩——这套路数跟 01-6 处理 OnceCallback 单次消费断言、跟 WeakPtr 的 CHECK-on-deref 是同一套,笔者在那两篇已经趟过一遍了。

## 性能:per-item 开销

```text
sizeof(flat_map<int,int>)  ≈ 24 字节(三指针)
sizeof(std::map<int,int>)  ≈ 48 字节(树根 + sentinel + 比较器)

100 万元素 map<int,int> 额外开销(数据 8MB 不计):
  flat_map:  ~0 额外(数据连续)
  std::map:  ~32MB(32B/元素 × 100 万 + 100 万次 malloc)
```

flat_map 零 per-item 元数据,一次连续分配就齐活;std::map 每元素背着 32B 元数据,还得一次堆分配。这正是第一篇说的“常数因子差一个数量级”的出处——渐近都是 `O(log n)` 查找,但 std::map 在咱们看不到的地方先把 32MB 元数据 + 100 万次 malloc 的账给记上了。

## 性能:查找 vs 插入

光看 `sizeof` 和分配次数还不过瘾,笔者上机实跑了一把。本机 GCC 16 -O2,配套 `20_lookup_vs_shift_perf`,10 万元素 `map<int,int>`:

```text
查找 10 万次(10 万元素):
  flat_map:  31 ms
  std::map:  34 ms     (int key + 10 万:几乎持平)

插入 1000 次到 10 万元素容器:
  flat_map:   2 ms     (每次 O(n) shift)
  std::map:   0 ms     (O(log n) 节点重连)
```

这里有个数字笔者第一次看愣了一下——查找上 flat_map 居然没把 std::map 按在地上摩擦,俩几乎持平。后来一琢磨就通了:10 万 + int key 这个量级,数据本身就塞得进 cache,std::map 的指针追逐还没开始大规模 miss,flat_map 的连续红利自然显不出来。真想看出差距,得把 N 再往上推,或者把 key 换成 `std::string` 这种重的。独立大 N 测试里 flat_map 快出几倍是常有的事,但您别拿这当教条,小 N 轻 key 下它就是优势不明显,这很正常。

插入这边画风就反过来了,而且没什么悬念。flat_map 每插一个就得 `O(n)` shift 一大片,2 ms 对 std::map 的 0 ms;N 越大这个差距只会越拉越开,是面实打实的墙。所以 flat_map 的契约写得很明白:查多写少的主场,别拿它当高频写的容器使。

## 选型判据

| 负载 | 推荐 | 理由 |
|---|---|---|
| 写一次读多次(配置表/命令分发) | flat_map | 写入一次性,查找 cache 友好 |
| 始终很小(~4 元素) | flat_map | 常数因子主导,零分配 |
| 大且频繁改 | std::map | O(n) 插入是墙 |
| 需引用/指针稳定 | std::map | flat_map 迭代器全失效 |
| 大 N + 频繁改 + 有序 | absl::btree_map | B-tree 中间解(Chromium 因代码膨胀禁用) |

这张表其实就是一句话的事:查多写少、或者容器一直很小,选 flat_map;写多、或者要引用指针稳定,选 std::map;又大又频繁改还非要有序,那就是 absl::btree_map 的中间解,但 Chromium 自己因为代码膨胀把这条路堵了。Chromium `//base/containers/README.md` 的划法,也就是把这张表落成了文字。

## vs std::flat_map(C++23)/ absl::btree_map

聊到这儿,绕不开两个亲戚。C++23 的 `std::flat_map`(P0429)跟 Chromium flat_map 同源,思路是一脉的,但标准版选了 split storage——键和值分两个数组存。Chromium 偏不 split,老老实实用单个 `vector<pair<K,V>>`。笔者理解 Chromium 的取舍:split 在“只遍历 key”或“只遍历 value”时确实更省 cache,但实现复杂度上去了,而 flat_map 的主战场是查多写少的小容器,split 的收益在这儿根本兑现不出来,不划算。

另一个是 `absl::btree_map`,256B 的 B-tree 节点里塞一把 key,介于红黑树和 sorted vector 之间,大 N + 频繁改 + 有序的场景它最合适。但 Chromium 在 `//base` 里把 btree 禁用了,理由是代码膨胀——每实例化一种 key/value 类型都得多生成一大坨模板,B-tree 节点的拆装逻辑比 sorted vector 那套 `lower_bound + shift` 重得多。这是工程权衡的典型样本:技术上有更优解,但项目级别的代价吃不消,就宁可不要。

到这儿,flat_map 组件的设计、实现、验证三篇就走完了。回头看,它跟 OnceCallback、WeakPtr 凑成了 vol9/chrome 的第三块拼图——前两块讲的是“怎么把回调管住”“怎么把生命周期管住”,这一块讲的是“怎么把数据存得既省又快”,都是 Chromium `//base` 工业级 C++ 的基本功。

## 参考资源

- [Chromium `base/containers/README.md` —— 容器选择指南](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [Catch2 文档](https://github.com/catchorg/Catch2/tree/devel/docs)
- [P0429 —— std::flat_map 提案](https://wg21.link/p0429)
- [absl::btree_map](https://abseil.io/docs/cpp/guides/btree)
- [flat_map 设计指南（一）：动机、接口与 flat_tree 架构](./01-flat-map-design.md)
