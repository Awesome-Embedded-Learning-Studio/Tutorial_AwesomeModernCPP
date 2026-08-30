---
title: "mini STL 导论:为什么值得亲手搓一遍容器"
description: "从『数据结构用 C 做最合适』这句老话聊起,说清这个独立小项目为什么用模板手搓:工业实现怎么挑来当参照(std 实现、libstdc++、absl、Chromium base 各照各的),十三件手搓件的路线图与边界:红黑树和小对象 vector 为什么刻意不搓。"
chapter: 0
order: 0
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
  - 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 9
cpp_standard: [17, 20, 23]
prerequisites:
  - "容器选择指南:按操作、内存与失效规则挑对容器"
  - "vector 深入:三指针、扩容与迭代器失效"
related:
  - "mini STL 实战(一):RawBuffer,只管容量,不管对象"
  - "综合项目:concepts 约束的 mini-STL 算法库"
---

# mini STL 导论:为什么值得亲手搓一遍容器

咱们天天在写 `std::vector`,但如果把它的实现从编译器里删掉,您能凭空写出一份来吗?Chromium 的工程师 2017 年真的动手写过一遍:`base/containers/vector_buffer.h` 的版权年份就是 2017,179 行,一个"只管容量、不管对象"的裸缓冲,今天仍然是 `base::circular_deque` 的地基。一个浏览器内核团队,放着标准库不用,自己把容器地基重新写了一遍,理由写在文件开头:"Unlike `std::vector`, VectorBuffer never constructs or destructs its arguments"。标准库的 vector 不肯把"内存"和"对象生命周期"这两件事拆开卖,而他们需要拆开。

这个系列带您把这件事做一遍。它是个独立的小项目:咱们从零攒一个自己的 mini 容器库(`tamcpp::ministl` 命名空间),连代码工程带测试都是自己的;搓完一件,就拿真实的工业实现照一次镜子:动态数组照 `std::vector` 的内部实现,经典链表照 libstdc++ 的 `stl_list`,哈希照 absl 的 swiss table,环形缓冲和 LRU 照 Chromium base。哪件照哪面,按"这件容器最值得学的点在哪"来挑。

## 用 C 搓,还是用模板搓

这个系列的提案里有一句话:"数据结构这种东西,用 C 做其实最合适。"这话有道理。用 C 写一个动态数组,`malloc` 一块裸内存,`void*` 一转,元素一个一个塞进去,没有构造函数、没有析构函数、没有类型系统跟您讨论"移动语义"——数据结构的骨架(容量、增长、搬迁、索引)会以最赤裸的方式摊在您面前。大学的数据结构课也都这么教。

但 C 的直觉在"装什么类型"这个问题上会开始痛。想让数组装 `int` 也行、装 `struct Task*` 也行,C 只有两条路:宏,或者 `void*` 加一堆强制转换。前者把类型安全丢给预处理器的字符串替换,后者把类型安全丢给调用方的自觉。两条路走到底,您会发现自己在用 C 模拟一个残缺的模板系统。

模板就是干这个的。`template <typename T> class Vector` 一行,类型参数化交给编译期,而且是在类型检查之下。元素忘了析构,在 C 里是内存错误,在 C++ 里可以是一个编译期分派(`if constexpr`)加一个静态断言的事。所以这个系列的答案落在两头。骨架用 C 的直觉搓:咱们会经常看内存布局和真实输出,像写 C 一样抠细节。接口和类型安全,交给模板。提案人自己也说这是"用模板的好时机":concepts、`if constexpr`、模板参数注入这些技术,在没有真实容器要写的场合学,总有点纸上谈兵。

## 照的是什么镜子

主角始终是咱们自己的 mini 库,工业实现都是参照物。`std::vector`、`std::list` 的内部结构是常驻的一面:标准承诺了什么、实现在本机又是怎么做的,libstdc++ 的头文件就躺在那里;哈希那篇的参照是 absl 的 swiss table 设计文档,哈希表演化到今天的样子它写得最清楚;环形缓冲、侵入式容器、LRU 则照 Chromium 的 `base/containers/`。这个目录的 README 第一句自我介绍是"some stdlib-like containers",三十来个头文件里真正自成体系的就这么几件:

| 容器                    | 一句话身份                    | 独门理由                                  |
| ----------------------- | ----------------------------- | ----------------------------------------- |
| `flat_map` / `flat_set` | 排序 vector 版关联容器        | 小数据量下零堆分配、缓存友好(vol9 已深讲) |
| `circular_deque`        | 环形缓冲版双端队列            | 替代实现差异巨大、浪费内存的 `std::deque` |
| `small_map`             | 小走数组、大走真 map 的混合体 | 浏览器里 map 的尺寸众数只有 4             |
| `intrusive_heap`        | 带句柄的堆                    | 支撑浏览器任务调度器的延迟队列            |
| `LinkedList`            | 侵入式双向链表                | 插入零分配、已知节点 O(1) 删除            |
| `LRUCache`              | list 加 map 的组合件          | 2026 年的新代码,concepts 现役             |
| `RingBuffer`            | 定容环形窗口                  | 只留最近 N 个采样,写过单片机的朋友应该很眼熟    |

"没有"也是参照,咱们看这个目录里缺什么:没有 `small_vector`(内联存储的向量,Chromium 用 absl 的 `InlinedVector`,自己不造),没有自研哈希表(通用 map 直接推荐 `absl::flat_hash_map`),也没有经典链表(需要稳定迭代器的场合,官方建议直接用 `std::list`,自家只造侵入式变体)。为什么有的造、有的不造,每条都是真实的工程取舍,收官篇会把这些选择一条条讲清楚。

::: warning
提醒一句预期:本系列不做源码解读,手搓件平均一百来行,砍掉工业版本里的安全宏、性能注记和兼容层,只留数据结构的骨头。对照源码时,行号都以本地快照为准,您手里若是别的版本,行号对不上是正常的。
:::

## 方法论:手搓、照镜子、做取舍

每一件容器的走法都一样。咱们先不看源码,把最小可用版本写出来,跑通测试——写错了才记得住,这是动手学项目反复验证过的老理。写完打开对应的工业实现,逐个岔路口对照:容量怎么表示?扩容选几倍?迭代器带不带父指针?同一道题人家写了几百行,差异本身就是教材。

最后把"咱们的教学版为什么跟人家不一样"写成明白话。比如咱们库的下标检查走崩溃式的 `Check`(跟着 Chromium),`std::vector::at` 走抛异常(调用方可以接住恢复),两边都有道理,道理讲出来才值钱。

十三件手搓件加一篇收官,咱们按依赖来排:常见的基础几件在前,带工业背景的几件放在后面。地基是裸缓冲加动态数组 `Vector`。接着进环形:定容的 `RingBuffer`、动态的 `CircularDeque`,顺带写一页 stack/queue 适配器。然后是链表的三种形态:最简单的单向 `ForwardList`、容器自管节点的经典双向 `List`、Chromium 那种 `LinkedList` 侵入式变体。三种看完,"节点到底长在谁身上"这个问题就透了。再往上是关联结构:组装一个 `FlatMap`,手搓开地址 `HashMap`,还有混合策略的 `SmallMap`。收尾是堆(`IntrusiveHeap`)和组合毕业(`LRUCache`)。每一层的知识点都被下一层复用,地基篇的搬迁手法会在环形扩容里原样再用一次。

## 刻意不搓什么

红黑树不搓。`std::map` 的底层就是它,vol3 的概念层已经把节点布局讲透;这里不碰它的原因很直白:它难在十几种旋转与重着色的组合爆炸,教学回报配不上这个难度。有个流传很广的手写 STL 教程,作者写红黑树那一课时自述"边缘情况多到写出无数 bug"。写完的收获主要是对红黑树的敬畏。有序结构这一格,咱们用排序 vector 顶上,查找语义一样,心智负担小一个量级。

`small_vector`(把少量元素直接存在对象体内、不上堆的那种 vector)咱们也不搓,收官篇当讨论题:Chromium 自己都没造这件,理由本身就值得一段。

`std::string`、`std::span`、分配器策略,vol3 与 vol4 都有专文,咱们不重复。

最后消个歧:vol4 元编程子卷有一篇"mini-STL 算法库",搓的是 `transform`、`accumulate` 这几个算法;咱们这边搓的全是容器,两边凑一起才是完整的 mini STL。同一个卷里的算法子域,讲排序查找那一套,和这边也不抢戏。

## 从哪开始

第一件是 `RawBuffer`,一个只管容量、不管对象的缓冲,咱们库所有容器的共同起点,Chromium 那边也是拿它当地基。配套代码在 `code/volumn_codes/vol8-labs/ministl/`,按 stage 分目录生长:现在是 `stage1_rawbuf_vector/`,以后每开一件新容器就新开一个 stage 目录,评审的人一个 stage 一个 stage 地看。那一篇里有个笔者第一次踩就愣住的坑,UBSan 抓的,真跑给各位看。

## 参考资源

- Chromium `base/containers/` 目录与 README(容器选型官方指南)
- `base/containers/vector_buffer.h`(2017,本系列的第一面镜子)
- 本机 libstdc++ `bits/stl_list.h`、`bits/forward_list.h`(链表篇的镜子);[absl swiss table 设计文档](https://abseil.io/about/design/swisstables)(哈希篇的镜子)
- [stl1weekend(小彭老师的手写 STL 系列)](https://github.com/parallel101/stl1weekend)("自己动手实现 STL"路线的同道,红黑树血泪出处)
- [综合项目:concepts 约束的 mini-STL 算法库](../../vol4-advanced/vol3-metaprogramming-cpp20-23/09-mini-stl-with-concepts.md)(vol4,算法那一半)
- [容器选择指南](../../vol3-standard-library/containers/01-container-selection-guide.md)(vol3,概念层入口)
