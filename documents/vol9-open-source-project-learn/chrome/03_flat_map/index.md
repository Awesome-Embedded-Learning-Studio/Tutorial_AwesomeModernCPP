# flat_map：从 Chromium 学到的有序容器设计

本目录拆 Chromium 的 `flat_map` / `flat_tree`,讲透"用有序 vector 实现关联容器"这套工业级设计:为什么小数据量下数组打败树、查多写少的主场、sorted_unique 的零成本构造、透明比较器与 EBO。它是 [OnceCallback](../01_once_callback/) 与 [WeakPtr](../02_weak_ptr/) 的姊妹篇,补 vol9/chrome 的容器与性能这维。

## 完整教程（full/）

前置知识(6 篇):

- [有序关联容器入门](./full/pre-00-flat-map-ordered-assoc-container-intro.md)
- [vector 内部与扩容](./full/pre-01-flat-map-vector-internals-and-growth.md)
- [复杂度与摊还](./full/pre-02-flat-map-complexity-and-amortized.md)
- [比较器与 transparent](./full/pre-03-flat-map-comparator-and-transparent.md)
- [tag dispatch 与 sorted_unique](./full/pre-04-flat-map-tag-dispatch-and-sorted-unique.md)
- [NUA EBO 与 pair 存储](./full/pre-05-flat-map-enua-ebo-and-pair-storage.md)

动手实践(6 篇):

- [动机与 API 设计](./full/03-1-flat-map-motivation-and-api-design.md)
- [flat_tree 骨架](./full/03-2-flat-map-flattree-skeleton.md)
- [查找与插入](./full/03-3-flat-map-lookup-and-insert.md)
- [sorted_unique 构造](./full/03-4-flat-map-sorted-unique-construction.md)
- [迭代器失效与批量构建](./full/03-5-flat-map-iterator-invalidation-and-bulk-build.md)
- [测试与性能](./full/03-6-flat-map-testing-and-perf.md)

## 进阶设计指南（hands_on/）

面向有模板与性能经验的读者:

- [设计](./hands_on/01-flat-map-design.md)
- [实现](./hands_on/02-flat-map-implementation.md)
- [测试与性能](./hands_on/03-flat-map-testing.md)
