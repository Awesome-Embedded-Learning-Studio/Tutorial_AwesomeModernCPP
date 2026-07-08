---
chapter: 1
cpp_standard:
- 17
- 20
description: 实现 flat_tree 的查找(lower_bound O(lg n))与插入(lower_bound + emplace,
  O(n) shift),含 flat_map 的 operator[]/insert_or_assign/try_emplace,并实测 shift 代价
difficulty: intermediate
order: 3
platform: host
prerequisites:
- flat_map 实战（二）：flat_tree 核心骨架
- flat_map 前置知识（二）：复杂度与摊还分析
reading_time_minutes: 12
related:
- flat_map 实战（四）：sorted_unique 构造优化
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 优化
title: "flat_map 实战（三）：查找与插入"
---
# flat_map 实战（三）：查找与插入

[03-2](./03-2-flat-map-flattree-skeleton.md) 把骨架立起来了,这一篇咱们往里填两个真正会用到的东西:怎么查,怎么插。这两件事一个是 flat_map 的卖点,一个是它的代价,咱们拆开看。

查这件事 flat_map 干得漂亮。数据本来就连续有序,二分一上,`O(log n)`,还基本都吃在 cache 上。卖点是实打实的。但插这件事就得小心了——`O(n)` shift 不是写在文档里吓唬人的,是真会咬人。笔者会在最后跑个实验让您亲眼看看那条 shift 曲线长什么样,把抽象结论变成可感知的代价。两个操作的底层都站在 [pre-02](./pre-02-flat-map-complexity-and-amortized.md) 的复杂度分析和 [pre-01](./pre-01-flat-map-vector-internals-and-growth.md) 的 vector 行为上,前置没过的朋友回头补一下。

## 查找:lower_bound O(lg n)

flat_tree 的查找接口一堆(`find`/`contains`/`lower_bound`/`equal_range`),底层都收敛到同一件事:二分。Chromium 用 `std::ranges::lower_bound` 配一个 `KeyValueCompare` 比较器对象(flat_tree.h:1027),在有序数组上找第一个不小于 key 的位置:

```cpp
// flat_tree::find 的核心(简化;只传一个二元比较器 (value,key)->bool)
const_iterator find(const Key& key) const {
    auto it = std::lower_bound(body_.begin(), body_.end(), key,
        [&](const value_type& v, const Key& k) { return comp_(GetKeyFromValue{}(v), k); });
    // lower_bound 给的是"第一个不小于 key 的",还要确认是否真的相等
    if (it != body_.end() && !comp_(key, GetKeyFromValue{}(*it))) return it;
    return body_.end();
}
```

> 注意:`std::ranges::lower_bound(range, value, comp)` 只接受**一个**比较器。Chromium 的 `KeyValueCompare`(flat_tree.h:439-462)是一个**带两个 `operator()` 重载**的类(v<k 和 k<v),整体作为一个比较器对象传入——不是两个并列 lambda。我们的教学版用 `std::lower_bound`(迭代器对)+ 单二元 lambda,语义等价、更直观。

二分每步把范围砍半,`log₂(n)` 次比较。每次比较先用提取器取 key(对 map 是 `pair.first`,O(1)),再用 `comp_` 比。这一步便宜,而且因为数据是连续的,这些比较几乎全部命中 cache。flat_map 查找快就快在这儿:不光是 `O(log n)`,是每一次比较都便宜,两点一合起来,才有了那个对比 std::map 的优势。

剩下几个接口都好理解。`contains(key)` 就是 `find(key) != end()`;`equal_range(key)` 返回 `[lower_bound, upper_bound)` 区间;`count(key)` 对 flat_map 这种唯一 key 容器返回 0 或 1。说句题外话,Chromium 的 `find` 真要较真其实是经 `equal_range` 实现一层的,笔者这里教学版为了省一层直接 `lower_bound + 判等`,语义上完全等价。这些接口 flat_map 都从 flat_tree 原样继承,行为和 std::map 对齐。

---

## 插入:lower_bound + emplace,O(n) shift

单元素插入(`insert`/`emplace`)走这条路径(flat_tree.h:1060 `unsafe_emplace`):

```cpp
// flat_tree::insert 的核心(简化)
std::pair<iterator, bool> insert(const value_type& value) {
    const Key& key = GetKeyFromValue{}(value);
    auto it = std::lower_bound(body_.begin(), body_.end(), key,
        [&](const value_type& v, const Key& k) { return comp_(GetKeyFromValue{}(v), k); });   // 1. 找位置 O(log n)
    if (it != body_.end() && !comp_(key, GetKeyFromValue{}(*it))) {
        return {it, false};   // key 已存在,不插入(唯一 key 不变量)
    }
    auto inserted = body_.emplace(it, value);               // 2. 插入,O(n) shift
    return {inserted, true};
}
```

看代码就看明白了,就两步:先用 `lower_bound` 找到该插哪儿,再用 `vector::emplace` 在那里把元素构造进去。第二步是真正的代价所在。`vector::emplace(pos, value)` 要把 `pos` 之后的所有元素整批往后挪一格——底层是 `std::move_backward` 搬迁,再在腾出来的位置上构造新元素。挪多少由后面的元素个数决定,平均下来 `n/2`,大 O 记作 `O(n)`。

这就是 flat_map 插入时必须认下的一笔账:每次插入都要挪一半元素。渐近复杂度 `O(n)`,而且这里没有摊还一说——不是偶尔挪一次,是每次都挪,没有哪次能逃过去。

---

## flat_map 特有:operator[]、insert_or_assign、try_emplace

flat_tree 本身是通用的,flat_map 在它的基础上又叠了几个 map 特有的操作。咱们一个个看。

### operator[] 的实现(flat_map.h:313, 326)

```cpp
mapped_type& operator[](const Key& key) {
    auto it = lower_bound(key);              // 找位置
    if (it == end() || comp_(key, GetKeyFromValue{}(*it))) {
        it = unsafe_emplace(it, ...);        // 不存在 → 插入默认构造的 mapped
    }
    return it->second;
}
```

`m[key]` 干的事:查一下,key 不存在就插一个默认构造的 `mapped_type()` 进去,然后返回引用;存在就直接返回已有那个的引用。语义和 `std::map::operator[]` 完全一致。有一点笔者要单独提醒——它会动容器(可能真的插进去一个东西),所以 `const flat_map` 上用不了,编译期就会拦下来。

### insert_or_assign(flat_map.h:334-355)

```cpp
template <class M>
std::pair<iterator, bool> insert_or_assign(const Key& key, M&& obj) {
    auto result = emplace_key_args(key, std::forward<M>(obj));   // 先试插
    if (!result.second) {
        // key 已存在 → 覆写 mapped
        result.first->second = std::forward<M>(obj);             // 赋值(需要 pair<K,V> 非 const!)
    }
    return result;
}
```

`insert_or_assign(key, val)` 的行为:查到 key 不存在就插;查到存在,就**把 value 覆写掉**。返回 `{iterator, inserted_bool}`,`inserted=false` 表示这次其实是覆写。

笔者第一次读源码时,真正卡住的是后面这一句覆写——`result.first->second = forward<M>(obj)`。它依赖 `pair` 的 second 是可赋值的。这就是为什么 flat_map 内部必须存 `pair<K, V>` 而不是 `pair<const K, V>`——后者 second 不能赋值,这条路就堵死了。这个看似无关紧要的存储选择,是被 `insert_or_assign` 这个 API 倒推出的硬约束,详细的笔者放在 [pre-05](./pre-05-flat-map-enua-ebo-and-pair-storage.md)。

### try_emplace(flat_map.h:392-413)

```cpp
template <class... Args>
std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
    // 只在 key 不存在时才构造 mapped(args...)
    auto [it, inserted] = emplace_key_args(key, std::piecewise_construct,
                                           std::forward_as_tuple(key),
                                           std::forward_as_tuple(std::forward<Args>(args)...));
    return {it, inserted};
}
```

`try_emplace(key, args...)` 跟上面那位性格相反:key 不存在才用 `args...` 构造 mapped;key 已经在了,它就**完全不动**那个已有的 value。这就是它和 `insert_or_assign` 的本质区别——一个会覆写,一个无视。实现上有点讲究,用 `std::piecewise_construct + forward_as_tuple` 把 pair 的构造延迟到真正需要的那一刻,免得您传进来的 mapped 在"key 已存在"的情况下白白构造了一趟又被扔掉。

---

## erase:O(n) shift

别光盯着插入,删除也是 `O(n)` 的——这是连续存储的对称代价。`erase` 直接转给 vector(flat_tree.h:914/921 的 `body_.erase`):

```cpp
iterator erase(const_iterator pos) {
    return body_.erase(pos);   // vector::erase,把后面元素往前挪一格,O(n)
}
```

`erase(pos)` 删一个位置,`erase(first, last)` 删一段,干的都是同一件事:把后面的元素整批往前挪一格。`erase(key)` 这个重载稍微多一道手续,它得先 `lower_bound` 找到位置(`O(log n)`),再 `erase` 挪元素(`O(n)`),合起来 `O(n) + O(log n)`,大 O 还是 `O(n)`。

---

## 实测:O(n) shift 到底多贵

光说 `O(n)` 您大概会有感觉,但没痛感。咱们跑个实验,把这条 shift 曲线从抽象结论变成肉眼可见的代价。思路很简单:在 vector 头部插 10 万次(`emplace(begin)`),每次都把后面所有元素往后挪一格;然后对比尾部 `push_back`(摊还 `O(1)`):

```cpp
// Platform: host | C++ Standard: C++17
#include <chrono>
#include <iostream>
#include <vector>

int main() {
    constexpr int N = 100'000;

    // 头部插入:每次 O(n) shift
    std::vector<int> a;
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) a.emplace(a.begin(), i);
    auto t2 = std::chrono::steady_clock::now();
    std::cout << "emplace(begin) x" << N << ": "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count()
              << " ms\n";

    // 尾部插入:摊还 O(1)
    std::vector<int> b;
    auto t3 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) b.push_back(i);
    auto t4 = std::chrono::steady_clock::now();
    std::cout << "push_back      x" << N << ": "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count()
              << " ms\n";
    return 0;
}
```

本机(GCC 16,-O2)真实输出:

```text
emplace(begin) x100000: 264 ms
push_back      x100000: 0 ms
```

两个数量级的差距,实打实地摆在眼前。这就是您把 flat_map 当成 std::map 那样频繁插入会踩到的曲线——每次 insert 都在按比例付那条 `emplace(begin)` 264ms 的账。

所以为什么前几篇反复念叨"查多写少"这个使用前提?它真不是文档里的客套话,是被这条 O(n) shift 曲线逼出来的硬约束。写多了,您会亲眼看着性能曲线往那条 264ms 的轨迹靠。

---

## 串起来:一个完整的查找插入例子

```cpp
// 用 03-2 的 mini_flat_map
mini_flat_map<int, std::string> m{std::vector<std::pair<int, std::string>>{
    {1, "one"}, {3, "three"}, {5, "five"}}};

auto it = m.find(3);
if (it != m.end()) std::cout << it->second << "\n";   // three

// 插入(有序位置自动确定,O(n) shift)
// 这里用 flat_tree 的 insert(简化展示)
// m.insert({4, "four"});  // 插在 3 和 5 之间,挪动 5
```

flat_map 的零成本构造留给后续——sorted_unique 怎么跳过 sort_and_unique。

## 参考资源

- [Chromium `base/containers/flat_tree.h` —— lower_bound / unsafe_emplace / erase](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h` —— operator[]/insert_or_assign/try_emplace](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [cppreference: std::lower_bound](https://en.cppreference.com/w/cpp/algorithm/lower_bound)
- [flat_map 前置知识（二）：复杂度与摊还分析](./pre-02-flat-map-complexity-and-amortized.md)
