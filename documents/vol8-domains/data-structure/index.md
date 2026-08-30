# 手搓 mini STL:容器库实战

咱们自己动手搓一个教学版 mini 容器库(`tamcpp::ministl` 命名空间):从"只管容量、不管对象"的裸缓冲,一路写到 LRU 缓存。每搓完一件,拿真实的工业实现照镜子——动态数组照 `std::vector` 与 Chromium 的 `vector_buffer`,经典链表照 libstdc++ 的 `stl_list`,哈希照 absl 的 swiss table,环形缓冲、侵入式容器、LRU 照 Chromium `base/containers`。照谁的镜子,错误处理就跟谁:std 镜的抛异常,Chromium 镜的当场崩溃。

前置要求:您读过 vol3 容器卷的概念层,模板看得懂特化与变参,最好碰过一次 placement new。配套代码在 `code/volumn_codes/vol8-labs/ministl/`,按 stage 分目录推进(当前 `stage1_rawbuf_vector/`:RawBuffer 与 Vector),每篇文章的输出都能在里面原样复现。

## 前置知识

- [pre-00 导论:为什么值得亲手搓一遍容器](pre-00-mini-stl-why-handroll.md)

## 实战篇

- [01 RawBuffer:只管容量,不管对象](01-raw-buffer.md)——内存与对象生命周期解耦,`placement new` 与显式析构成对登场
- [02 Vector,扩容与搬迁](02-vector-growth-and-relocation.md)——两个成员的极简布局、翻倍策略与摊还分析
- [03 Vector,五件套、异常安全与 concepts](03-vector-copy-move-and-concepts.md)——copy-and-swap、move_if_noexcept、真实 bug 复盘

本系列咱们按模块陆续推进,后续篇目围绕环形缓冲、链表(单向、双向、侵入式)、有序与哈希映射、堆、LRU 缓存展开,每篇落成即在此补上链接。

## 相邻内容

- [vol8 算法子域](../algorithms/index.md):排序、查找这些"算法那一半"归那边,容器的手写实现归这边
- [vol3 容器卷](../../vol3-standard-library/containers/index.md):std 容器的概念层,本系列的前置
- [flat_map 系列](../../vol9-open-source-project-learn/chrome/03_flat_map/index.md):排序 vector 容器的完整深讲,后续 FlatMap 组装篇直接站在它肩膀上
- [vol4 的 mini-STL 算法库](../../vol4-advanced/vol3-metaprogramming-cpp20-23/09-mini-stl-with-concepts.md):那边是 concepts 练手,这边是系统手搓
