---
chapter: 1
cpp_standard:
- 17
- 20
description: 实现 flat_tree 核心骨架——sorted vector 适配器、key 提取器策略、有序不变量、
  value_compare 嵌套结构,以及 flat_map/flat_set 怎么继承它
difficulty: intermediate
order: 2
platform: host
prerequisites:
- flat_map 实战（一）：动机与接口设计
- flat_map 前置知识（一）：std::vector 内部表示与扩容
- flat_map 前置知识（五）：NO_UNIQUE_ADDRESS、EBO 与 pair 存储
reading_time_minutes: 12
related:
- flat_map 实战（三）：查找与插入
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 内存管理
title: "flat_map 实战（二）：flat_tree 核心骨架"
---
# flat_map 实战（二）：flat_tree 核心骨架

上一篇咱们把 flat_map 的目标 API 拍了下来,顺嘴提了句这玩意儿的核心其实就一个类:`flat_tree`。这一篇笔者想带您把这个"有序数组关联容器适配器"的骨架亲手搭一遍。搭完您会撞见一个挺有意思的数:Chromium 的 flat_set.h 满打满算 191 行。set 这么省事,是因为它能复用的东西全被 flat_tree 吃进去了,自己那边几乎不用再写代码。

flat_tree 之所以能一份骨架同时伺候 map 和 set,靠的是三招配合。第一招是个泛型的 key 提取器策略(`GetKeyFromValue`),同一个 value 它能取出 key 也能原样返回,这决定了容器戴着 map 还是 set 的帽子。第二招是有序不变量,每次 mutation 完它自己悄悄把顺序补回来。第三招是个嵌套的 `value_compare`,把对 value 的比较翻译回对 key 的比较。咱们一个一个拆。

## flat_tree 模板签名

`flat_tree` 的模板签名(flat_tree.h:104-105)长这样:

```cpp
template <class Key, class GetKeyFromValue, class KeyCompare, class Container>
class flat_tree {
protected:
    Container body_;                                  // 底层有序容器(默认 vector)
    [[no_unique_address]] KeyCompare comp_;           // key 比较器(EBO 零开销)
    // ...
};
```

四个模板参数,笔者挨个说一下。`Key` 是 key 类型,没悬念。`GetKeyFromValue` 是这套设计的秘密武器,它是个函数对象,实现 `const Key& operator()(const Value&)`——给定一个 value(对 map 是 `pair<K,V>`,对 set 是 `K` 本身),吐回它的 key。同一个 flat_tree 能当 map 也能当 set,差别全落在这个 typename 上,后面咱们会看具体写法。`KeyCompare` 是 key 比较器,默认 `std::less<>`。`Container` 是底层序列容器,默认 `std::vector`,map 用 `vector<pair<K,V>>`、set 用 `vector<K>`。

数据成员就俩:`body_` 是底层容器,`comp_` 是比较器。`comp_` 上挂了 `[[no_unique_address]]`,目的是让空比较器吃零字节,这块的来龙去脉笔者在 [pre-05](./pre-05-flat-map-enua-ebo-and-pair-storage.md) 里讲过,这里不重复。

---

## key 提取器:GetFirst vs std::identity

key 提取器是 map/set 共用一份代码的关键。咱们直接看两个具体实现。

flat_map 这边用的是 `GetFirst`(flat_map.h:24-29):

```cpp
struct GetFirst {
    template <class Key, class Mapped>
    constexpr const Key& operator()(const std::pair<Key, Mapped>& p) const {
        return p.first;   // value 是 pair,取 first 当 key
    }
};
```

flat_set 这边更省事,直接拿标准库自带的 `std::identity`,原样返回:

```cpp
// flat_set.h:163 等价于:
using flat_set = flat_tree<Key, std::identity, Compare, std::vector<Key>>;
// std::identity 的 operator()(const T&) 返回 T 本身——value 就是 key
```

flat_tree 内部要比较两个 value 时,先调提取器取出各自的 key,再用 `comp_` 比 key。所以同一份 flat_tree 代码,`GetFirst` 让它把 `pair<K,V>` 当 map 处理,`std::identity` 让它把 `K` 当 set 处理。笔者第一次读到这儿愣了一下——map 和 set 的全部实现分野,就落在这一个 typename 上。

---

## value_compare:把 value 比较转成 key 比较

flat_tree 还对外甩了一个嵌套的 `value_compare`(flat_tree.h:122-130)。它的用处是让外部能按 value 比较,比如您想拿 `std::sort` 直接排 value 数组时,手头得有个比 value 的函子才行:

```cpp
struct value_compare {
    constexpr bool operator()(const value_type& left, const value_type& right) const {
        GetKeyFromValue extractor;
        return comp(extractor(left), extractor(right));   // 取出各自 key 再比
    }
    [[no_unique_address]] key_compare comp;   // 同样 EBO
};
```

它干的活儿就是"双边提取 key 再交给 `comp`"。对 map 来说是比两个 pair 的 first;对 set 来说是比两个 key 本身(因为提取器是 identity,原样透传)。这个嵌套结构让 flat_tree 能在 value 层面提供比较接口,底层还复用同一份 key 比较器,不用为 value 另写一套。

---

## 有序不变量:每次 mutation 后保持有序 + 唯一

flat_tree 护着的核心不变量只有一条:`body_` 永远按 `comp_` 严格升序,且无重复。这条不变量在两个地方被维护,构造期一把排好,插入期逐个守好。

### 构造期:sort_and_unique

普通构造(传无序数据)调 `sort_and_unique`(flat_tree.h:147-149,实现在 567/578/586/594):

```cpp
void sort_and_unique() {
    std::stable_sort(body_.begin(), body_.end(), value_comp());   // 排序(O(N log N))
    auto it = std::ranges::unique(body_, equiv);                  // 去重(equiv = !comp && !comp)
    body_.erase(it.end(), body_.end());                           // 删掉重复尾巴
}
```

`stable_sort` 按 `value_comp` 排序,然后 `unique` 把等价元素挪到末尾,再 `erase` 砍掉尾巴。笔者提一句这里为什么用 stable_sort 而不是 sort:等价元素(同一个 key 的多个 value)如果原来有先后,stable_sort 能保住它们的相对顺序——虽然 flat_map 去完重只留一个,但保序语义在某些边界情况(比如 value 带状态时)更稳。构造完事儿之后,`body_` 就是有序无重复的干净状态。

### 单点插入:lower_bound + insert

运行期插单个元素时(flat_tree.h:1060 `unsafe_emplace`),先 `lower_bound` 找位置(保持有序),再 `insert`。lower_bound 找的是"第一个不小于 key 的位置",插在那儿天然保持有序;要是 key 已经在,`lower_bound` 会指向那个相等元素,`unique` 语义就要求拒绝插入,避免重复。这套查找加插入的具体写法和那个让人肉疼的 shift 代价,咱们留到 03-3 详讲。

---

## 构造:普通 vs sorted_unique

flat_tree 的构造分两族,这个分法背后藏着性能取舍,笔者拆给您看。

普通构造这边,您传无序数据进来,它内部老老实实调 `sort_and_unique`:

```cpp
flat_tree(InputIterator first, InputIterator last, const Compare& comp) {
    body_.insert(body_.end(), first, last);
    sort_and_unique();   // 排序去重
}
```

sorted_unique 构造这边,您拿 `sorted_unique_t` 标签拍胸脯保证数据已经有序无重复,它就跳过排序,只做个 DCHECK:

```cpp
flat_tree(sorted_unique_t, InputIterator first, InputIterator last, const Compare& comp) {
    body_.insert(body_.end(), first, last);
    DCHECK(is_sorted_and_unique(body_, comp));   // 只 debug 校验,不排序
}
```

两族的差别就落在一个 `sort_and_unique` 调用上:要么真排,要么信您。后者在您确定数据源已经有序时(比如从另一个有序容器搬过来)能省一次 O(N log N),是 flat_tree 给性能敏感场景留的逃生口。tag dispatch 这个机制本身的来龙去脉,笔者放在 [pre-04](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md) 里讲了。

---

## flat_map / flat_set 怎么继承 flat_tree

有了 flat_tree 这层骨架,flat_map 和 flat_set 上头几乎不用再写东西。

flat_map(flat_map.h:194-195)走的是继承,把它需要的那一份 key 提取器填进去:

```cpp
template <class Key, class Mapped, class Compare = std::less<>,
          class Container = std::vector<std::pair<Key, Mapped>>>
class flat_map : public flat_tree<Key, internal::GetFirst, Compare, Container> {
    // 继承 flat_tree 的所有通用操作(find/insert/erase/lower_bound...)
    // 自己只加 map 特有的:operator[]、at、insert_or_assign、try_emplace
};
```

flat_set(flat_set.h:159-163)更干脆,连类都懒得定义,直接别名:

```cpp
template <class Key, class Compare = std::less<>,
          class Container = std::vector<Key>>
using flat_set = flat_tree<Key, std::identity, Compare, Container>;
// 完全没有自己的代码——set 就是"key=value"的 flat_tree
```

flat_map 加的那几个 map 特有操作(`operator[]`/`at`/`insert_or_assign`/`try_emplace`),笔者留到 03-3 跟查找插入一起讲。flat_set 这边因为 key 就是 value,真没什么可加的,一个 `using` 就交代完了。您现在回头看 flat_set.h 那 191 行,大概就明白这抽象的杠杆有多大。

---

## 最小 flat_tree 复刻

光看 Chromium 的代码不过瘾,咱们自己动手搓一个最小版,亲手体会一下"key 提取器 + 有序不变量"这两招是怎么咬合的:

```cpp
// Platform: host | C++ Standard: C++20
#include <algorithm>
#include <functional>
#include <iostream>
#include <utility>
#include <vector>

namespace tamcpp::chrome::internal {

template <class Key, class GetKeyFromValue, class KeyCompare, class Container>
class flat_tree {
public:
    using value_type = typename Container::value_type;
    using iterator = typename Container::iterator;
    using const_iterator = typename Container::const_iterator;

    // 普通构造:无序数据,内部排序去重
    flat_tree(Container data, KeyCompare comp = KeyCompare())
        : body_(std::move(data)), comp_(comp) {
        sort_and_unique();
    }

    // 查找:O(log n) 二分
    const_iterator find(const Key& key) const {
        auto it = std::ranges::lower_bound(
            body_, key,
            [&](const value_type& v, const Key& k) { return comp_(GetKeyFromValue{}(v), k); },
            [&](const Key& k, const value_type& v) { return comp_(k, GetKeyFromValue{}(v)); });
        if (it != body_.end() && !comp_(key, GetKeyFromValue{}(*it))) return it;
        return body_.end();
    }

    std::size_t size() const { return body_.size(); }
    const value_type& front() const { return body_.front(); }

private:
    void sort_and_unique() {
        GetKeyFromValue ext;
        std::stable_sort(body_.begin(), body_.end(),
                         [&](const value_type& a, const value_type& b) {
                             return comp_(ext(a), ext(b));
                         });
        body_.erase(std::unique(body_.begin(), body_.end(),
                                [&](const value_type& a, const value_type& b) {
                                    auto ka = ext(a), kb = ext(b);
                                    return !comp_(ka, kb) && !comp_(kb, ka);
                                }),
                    body_.end());
    }

    Container body_;
    [[no_unique_address]] KeyCompare comp_;
};

}  // namespace tamcpp::chrome::internal
```

这个最小版抓住了两件事:构造时排序去重,以及查找时二分。key 提取器策略那条线,在 `find` 和 `sort_and_unique` 里都能看到它把 value 翻译成 key 的动作。下一步咱们往里加插入和删除,那个 shift 的代价才是 flat_map 真正的软肋。

---

## 用 flat_tree 拼出 map 和 set

```cpp
// map:存 pair<K,V>,用 GetFirst 提 key
struct GetFirst {
    template <class K, class V>
    constexpr const K& operator()(const std::pair<K, V>& p) const { return p.first; }
};

template <class K, class V>
using mini_flat_map = internal::flat_tree<K, GetFirst, std::less<>,
                                          std::vector<std::pair<K, V>>>;

// set:存 K,用 std::identity 提 key
template <class K>
using mini_flat_set = internal::flat_tree<K, std::identity, std::less<>, std::vector<K>>;

int main() {
    mini_flat_map<int, std::string> m{std::vector<std::pair<int, std::string>>{
        {2, "b"}, {1, "a"}, {3, "c"}}};
    std::cout << m.size() << " elements, front key=" << m.front().first << "\n";   // 3, 1(已排序)

    mini_flat_set<int> s{std::vector<int>{3, 1, 2, 1}};   // 重复的 1 会被去重
    std::cout << s.size() << " elements\n";                                        // 3
    return 0;
}
```

跑一下您会看到 `3 elements, front key=1`(排序生效)和 `3 elements`(去重生效)。一份 flat_tree,戴 `GetFirst` 的帽子就是 map,换 `std::identity` 就是 set。

---

骨架搭到这儿算是立住了。`flat_tree<Key, GetKeyFromValue, KeyCompare, Container>` 这套签名是有序数组关联容器的通用底座,Key 提取器策略决定它戴 map 还是 set 的帽子,有序不变量靠构造期的 `sort_and_unique` 和插入期的 `lower_bound + insert` 两道关守着,`value_compare` 把 value 比较翻译回 key 比较。flat_map 在这之上加几个 map 特有操作,flat_set 干脆一个 `using` 交代完——flat_set.h 那 191 行就是这么来的。

接下来咱们把 flat_tree 的查找与插入真正写出来,`O(log n)` 二分好办,`O(n)` 的 shift 才是笔者想实测给您看的——那个代价到底有多肉疼。

## 参考资源

- [Chromium `base/containers/flat_tree.h` —— flat_tree 类与 value_compare](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h` —— GetFirst + flat_map 子类](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [Chromium `base/containers/flat_set.h` —— flat_set 别名](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_set.h)
- [flat_map 实战（一）：动机与接口设计](./03-1-flat-map-motivation-and-api-design.md)
