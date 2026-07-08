---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: 讲清 flat_map 比 std::map 严格的迭代器失效规则(保守的 Assume every operation
  invalidates),以及 extract/replace 的批量重建模式
difficulty: intermediate
order: 5
platform: host
prerequisites:
- flat_map 实战（四）：sorted_unique 构造优化
- flat_map 前置知识（一）：std::vector 内部表示与扩容
reading_time_minutes: 10
related:
- flat_map 实战（六）：测试与性能对比
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 内存管理
title: "flat_map 实战（五）：迭代器失效与批量构造"
---
# flat_map 实战（五）：迭代器失效与批量构造

笔者第一次把 `std::map` 换成 `flat_map` 的时候,栽在一个特别朴素的地方:拿着一个迭代器,中间动了下容器,回头再用,悬垂了。这种事在 `std::map` 上根本不会发生,节点容器各管各的,插一个节点不会动到别人。可 `flat_map` 底层是 `vector`,一扩容整个搬,一删除后面全 shift,迭代器说没就没。

笔者当时第一反应是去翻文档,想搞清楚到底哪种操作失效、哪种不失效。结果 Chromium 给了个让笔者愣了一下的回答:它压根没给您列精细规则,而是甩过来一句横话,"假设每个操作都失效所有迭代器"。这篇咱们就拆这条规则为啥要这么写,顺便把 `flat_map` 配套的批量重建 API(`extract`/`replace`)讲透,让您在需要大改容器的时候别再一个一个 insert。

## std::map vs flat_map:迭代器稳定性

`std::map` 是节点容器,每个元素单独堆分配,树节点靠指针串起来。插入、删除一个节点,别的节点纹丝不动,只动指针。所以指向它们的迭代器、指针、引用都活着:

```cpp
std::map<int, Config> m = /* ... */;
auto it = m.find(3);
m[99] = load(99);   // 插入新元素,it 仍然有效(节点没动)
it->second;          // OK
```

引用稳定是 `std::map` 的一个实打实的好处:您可以持有一个元素的引用,容器在背后增删,引用不失效。

`flat_map` 正好反过来。底层是连续 `vector`,插入可能触发扩容(整块搬迁),删除会把后面的元素往前 shift。这些操作都会让指向元素的迭代器、指针、引用变成野的:

```cpp
flat_map<int, Config> m = /* ... */;
auto it = m.find(3);
m[99] = load(99);   // 可能扩容 → it 失效!
it->second;          // UB!可能悬垂
```

cache 友好是用语义代价换来的:连续存储换来性能,代价是引用稳定性没了。这笔账您心里得有数。

## flat_tree 的保守规则

那 `flat_map` 的失效规则到底是个啥样?要按 `vector` 的行为精确推,那可细了:`reserve` 只在 `n > capacity` 时失效、`insert` 只失效从插入点起、`push_back` 不扩容时不失效……这套规则对调用方简直是个记忆力考试,您得每次 insert 前先掂量一下"这次会不会扩容"。

flat_tree 压根没给您出这张卷子。它对所有 mutation 一刀切,统统标成失效(flat_tree.h:151/217/231/273/306/319/374),源码注释原文就一句:

> Assume that every operation invalidates iterators and references.
> (假设每个操作都失效所有迭代器和引用)

覆盖的操作:`reserve`、`shrink_to_fit`、`insert`、`erase`、`swap`、move 构造、move 赋值、`extract`、`replace`、`clear`。一句话——改了就当全失效。

笔者第一次读到这条规则,第一反应是"这也太浪费了吧"。明明 `push_back` 不扩容时迭代器明明有效,凭什么说它失效?后来在代码评审里被一段类似的代码坑过一次,才反应过来:精细规则才是真正的陷阱。调用方记不住,容易误判,以为不失效其实失效了,直接 UB。粗规则没人会记错,"改了就别再用旧迭代器",永远安全。这种故意的安全冗余,其实是工程上很划算的取舍,宁可让您"白白"丢掉一个还能用的迭代器,也别让您赌错一次。

Chromium 在源码注释里直接把 UB 的反例贴了出来(flat_map.h:57-60),就一行:

```cpp
container["new element"] = it.second;   // UB:operator[] 可能触发扩容,it 失效
```

这种"边遍历边改"的代码,在 `flat_map` 里是直接未定义行为。flat_tree 把规则说粗,就是为了让您根本不去想"这次改会不会扩容"这种问题,直接假设失效,从根上掐掉这类 bug。

落到实操上,两条规矩笔者建议您刻进肌肉记忆。一是跨 mutation 别持有迭代器、指针、引用,拿到 `find` 的结果,用完这一把就丢,下次操作重新 `find`,别想着"反正这个迭代器我等会儿还要用"。二是如果您真有"持有稳定引用"的需求,比如回调里要长期握着一个元素的指针,那就别用 `flat_map`,老老实实上 `std::map`,节点容器才有引用稳定性,这种场景 `flat_map` 给不了您。

---

## 批量构造模式(再强调)

[03-4](./03-4-flat-map-sorted-unique-construction.md) 讲过批量构造,这里咱们换个角度,从"躲开迭代器失效"再看一遍。您要是想往 `flat_map` 塞一批元素,千万别边遍历边 insert,每次 insert 都失效所有迭代器不说,复杂度还飙到 O(N²),两头挨打。正确的姿势是先攒到一个 `vector` 里,再一次性 move 进去:

```cpp
// 1. 攒到一个 vector(push_back 摊还 O(1),无迭代器失效问题——只持有 vector,不持有 flat_map 迭代器)
std::vector<std::pair<int, Config>> batch;
batch.reserve(N);
for (...) batch.emplace_back(k, v);

// 2. 一次性 move 进 flat_map(批量构造,O(N log N) 排序一次)
flat_map<int, Config> m(std::move(batch));
```

`push_back` 在 `vector` 上是摊还 O(1),而且您手里握的是 `vector` 不是 `flat_map` 迭代器,失效规则那套烦人的事根本碰不到您。如果是对已有的 `flat_map` 做大批量更新,那就得用下面这对 API,把内容 extract 出来、改完再 replace 回去。

---

## extract() 与 replace():批量重建

flat_tree 给了两个 API,撑起一种"把数据拿出来大改、再交回"的批量重建模式。笔者第一次见这对组合的时候,觉得设计得挺巧,它把 `flat_map` 的有序约束整个卸下来,让您在裸 `vector` 上撒欢,改痛快了再交回去。

### extract()&&(flat_tree.h:894)

```cpp
container_type extract() && {
    return std::exchange(body_, container_type{});   // 把内部 vector 整个交出来,body_ 清空
}
```

`extract()` 是右值限定的,只能对将死的 `flat_map`(rvalue)调。它把底层 `vector` 整个 `std::exchange` 出来给您,原 `flat_map` 变空。您拿到这个 `vector` 之后,想怎么折腾都行,`push_back`、`sort`、`unique`、改元素,`vector` 上这些操作没有 `flat_map` 那套有序约束,自由得多。改完再 `replace` 回去就行。

### replace(container_type&&)(flat_tree.h:899-905)

```cpp
void replace(container_type&& body) {
    DCHECK(is_sorted_and_unique(body, comp_));   // 校验新数据有序无重复
    body_ = std::move(body);                      // 接管
}
```

`replace(body)` 是 `extract` 的逆操作,把一个新的 `vector` 交还给 `flat_map` 接管。它会先跑一遍 `DCHECK(is_sorted_and_unique)` 校验新数据有序无重复(契约跟 sorted_unique 构造一样),通过才接管。这么一进一出,您就能"拿出 vector → 自由改 → 排序去重 → 交回",全程绕开 `flat_map` 单元素操作那 O(n) 的 shift 代价和迭代器失效。

### 典型批量重建流程

```cpp
flat_map<int, Config> m = /* ... */;

// 1. extract 出 vector(对 rvalue 调)
std::vector<std::pair<int, Config>> raw = std::move(m).extract();

// 2. 在 vector 上自由批量改(没有有序约束,没有 shift 代价)
for (...) raw.emplace_back(k, v);

// 3. 排序去重
std::sort(raw.begin(), raw.end(), by_key);
raw.erase(std::unique(raw.begin(), raw.end(), equiv), raw.end());

// 4. replace 回 flat_map(sorted_unique 式校验)
m.replace(std::move(raw));
```

这个路子适合那种"要对 `flat_map` 做大量结构性修改"的场景,比逐个 `m.insert`/`m.erase`(每次 O(n) shift 外加失效一堆迭代器)高效太多。有一点笔者得提醒您:`replace` 要求新数据有序无重复,这事儿得您自己保证,它只做 debug 下的 DCHECK 校验,release 里不会帮您兜。契约跟 sorted_unique 一样,是诚实契约,您得诚实。

剩下的事就是把 `flat_map` 跟 `std::map`、`absl::btree_map` 摆一起实测量一量了,这是下一篇的内容。

## 参考资源

- [Chromium `base/containers/flat_tree.h` —— 迭代器失效注释 + extract/replace](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h` —— UB 示例](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [flat_map 前置知识（一）：std::vector 内部表示与扩容](./pre-01-flat-map-vector-internals-and-growth.md)
- [flat_map 实战（四）：sorted_unique 构造优化](./03-4-flat-map-sorted-unique-construction.md)
