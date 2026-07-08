---
chapter: 1
cpp_standard:
- 17
- 20
description: 面向有模板与性能经验的读者,快速走读 flat_map 的设计动机、接口与 flat_tree 适配器架构
  ——本系列 full/ 的精炼设计指南版
difficulty: advanced
order: 1
platform: host
prerequisites:
- 移动语义与完美转发
- C++20 concepts 与 ranges
- flat_map 前置知识（零）：有序关联容器与 std::map 的红黑树
reading_time_minutes: 6
related:
- flat_map 设计指南（二）：逐步实现
- flat_map 设计指南（三）：测试策略与性能对比
tags:
- host
- cpp-modern
- advanced
- 容器
- map
- 优化
title: "flat_map 设计指南（一）：动机、接口与 flat_tree 架构"
---
# flat_map 设计指南（一）：动机、接口与 flat_tree 架构

> hands-on 轨,默认您已熟 vector 扩容、复杂度分析、C++20 concepts;不熟的话先过一遍 [full/ 前置知识](../full/pre-00-flat-map-ordered-assoc-container-intro.md)。

笔者前阵子翻 `std::map` 的查找路径,翻着翻着血压就上来了:每节点 32B 元数据,每插一个键一次 malloc,查找时指针一路 `node = node->left_` 追下去,每步都是数据相关的解引用,CPU 想预取都够不着,cache miss 排着队来。Chromium 在 `//base/containers` 里另起了一套叫 `flat_map`,把红黑树换成有序 vector + 二分——查多写少的主场,就这么直白。咱们这一篇先把动机、接口和 flat_tree 适配器架构想明白,实现和测试留给后两篇,不碰代码细节。

## 问题:std::map 卡在哪

flat_map 跟 std::map 的查找都是 `O(log n)`,渐近一模一样,教科书上写不出差别。差别全在常数因子。红黑树节点散落在堆上各过各的,查找每一步 `node = node->left_` 都是一次数据相关解引用,CPU 没法预取,大概率就是一次 cache miss。给您一个直观的数:100 万个 `map<int,int>`,std::map 光节点元数据就吃掉大概 32MB,外加 100 万次 malloc;flat_map 这边几乎零额外开销,数据老老实实排成一条。渐近相同,常数因子差一个数量级——这就是 flat_map 存在的全部理由。

## 接口长什么样

门面其实朴素得让人意外:

```cpp
template <class Key, class Mapped,
          class Compare = std::less<>,                              // 透明默认
          class Container = std::vector<std::pair<Key, Mapped>>>    // 非 const Key
class flat_map : public flat_tree<Key, internal::GetFirst, Compare, Container>;
```

template 参数里藏了几手笔者觉得挺巧的取舍,您一眼扫过去可能注意不到,笔者给您点出来:

| 决策 | 选择 | 理由 |
|---|---|---|
| 默认比较器 | `std::less<>`(透明) | 异构查找,`find("abc")` 不构造临时 string |
| 存储 | `pair<Key,Mapped>` 非 const | vector 要 shift/赋值,`pair<const K,V>` 不可 move-assign |
| `at()` 越界 | CHECK 崩溃(非 throw) | Chromium 风格,逻辑错误立即爆 |
| `sorted_unique` 构造 | tag dispatch 跳过 sort | 数据已有序时 O(N),零成本 |
| `extract`/`replace` | 右值限定批量重建 | 避开单元素 O(n) shift + 迭代器失效 |

挑两条说一下。`Compare` 默认是 `std::less<>` 而不是 `std::less<Key>`,这一手叫透明比较——您拿字符串键的 map 去查 `find("abc")`,不用临时构造一个 `std::string` 出来,省一次堆分配。还有 `pair<Key, Mapped>` 里 Key 不带 const,这事儿看着反直觉:既然 key 不该改,为什么不 const?因为 vector 插入删除要 shift、要 move-assign,`pair<const K, V>` 根本没法 move-assign,整个 vector 就废了。

`at()` 越界直接 CHECK 崩,不抛异常——这是 Chromium 的风格,逻辑错误就别撑着,当场爆给您看。还有一个笔者刚开始找半天的东西:批量重建怎么走?标准库的 `extract` 在 flat_map 这里是右值限定的 `extract()&&`,配合 `replace(container_type&&)`,一次性把底层容器换掉,避开单元素 O(n) shift 和迭代器失效那一坨麻烦。

## flat_tree:一份代码当两个容器用

flat_map 的实现藏着个挺优雅的分层,笔者第一次读到时愣了一下——核心其实就一个类 `flat_tree<Key, GetKeyFromValue, KeyCompare, Container>`,它是个通用的"有序数组关联容器",真正的 map 和 set 都只是它的薄壳。`flat_map` 是 `flat_tree<Key, GetFirst, ...>` 的子类,`GetFirst` 这个策略对象从 `pair<K, V>` 里把 first 抠出来当 key(flat_map.h:194-195);`flat_set` 更干脆,直接是 `flat_tree<Key, std::identity, ...>` 的别名,`std::identity` 把 value 原样当 key(flat_set.h:159-163)。

笔者觉得最漂亮的一手就在这里:一个 typename 提取器——`GetFirst` 还是 `std::identity`——就让同一份 flat_tree 既是 map 又是 set。策略对象这东西教科书讲一大圈,这里就一行代码教完。您把 flat_tree 理解了,flat_map 和 flat_set 之间剩下的差异就只剩那一行提取器;flat_set.h 全文 191 行,核心也就这一行。

## 不变量与代价

flat_tree 内部那条 `body_` 永远按 `comp_` 严格升序、且没有重复——这是整个机制的地基。维护它的活儿分两段:构造期跑一遍 `sort_and_unique`(stable_sort 排序,unique 去重,erase 截尾),插入期靠 `lower_bound` 找位置再 `emplace`。代价笔者给您列清楚,这样您心里有数:

| 操作 | 复杂度 | 机制 |
|---|---|---|
| find/contains/lower_bound | `O(log n)` | std::ranges::lower_bound 二分,cache 友好 |
| insert/emplace/erase | `O(n)` | vector shift,无摊还 |
| operator[]/insert_or_assign/try_emplace | `O(n)` | 同 insert |
| range 构造 | `O(N log²N)` / `O(N log N)` | sort_and_unique |
| sorted_unique 构造 | `O(N)` | 跳过 sort,只 DCHECK |

您看出来了吧,这套代价跟 std::map 是镜像的:查找这边赢了常数因子,插入那边因为 vector shift 赔了 `O(n)`。flat_map 压根不打算当通用 map 用,它把赌注押在"查多写少"上——您手里要是有个配置表、路由表、枚举映射这种构造完基本只读的场景,这笔交易就划算;反过来高频插入删除,老老实实回去用 std::map。它也没忘给"我数据已经有序了"开后门,`sorted_unique` 构造 tag dispatch 跳过排序,只要 DCHECK 验过就 O(N) 进场,零成本。

架构和代价到这儿算是理顺了。但纸面讲清楚是一回事,真把它一行行撸出来,有些东西纸上看不出来——`sort_and_unique` 为什么要拆成 stable_sort + unique + erase 三步、`sorted_unique` 的 DCHECK 在 release 下怎么保证不误删数据、`extract()&&` 那一手右值限定到底省在哪。下一篇咱们就把 flat_tree 的核心代码摊开来看。

## 参考资源

- [Chromium `base/containers/flat_tree.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [Chromium `base/containers/README.md` —— 容器选择指南](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [flat_map 前置知识（零）：有序关联容器与 std::map 的红黑树](../full/pre-00-flat-map-ordered-assoc-container-intro.md)
