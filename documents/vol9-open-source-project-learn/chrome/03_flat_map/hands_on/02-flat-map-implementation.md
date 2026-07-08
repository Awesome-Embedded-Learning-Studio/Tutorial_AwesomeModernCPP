---
chapter: 1
cpp_standard:
- 17
- 20
description: 逐层实现 flat_tree/flat_map——签名、key 提取器、sort_and_unique、查找插入、
  sorted_unique 构造、flat_map 特有 API,代码密集少废话
difficulty: advanced
order: 2
platform: host
prerequisites:
- flat_map 设计指南（一）：动机、接口与 flat_tree 架构
- flat_map 前置知识（四）：tag dispatch 与 sorted_unique_t
reading_time_minutes: 13
related:
- flat_map 设计指南（三）：测试策略与性能对比
tags:
- host
- cpp-modern
- advanced
- 容器
- map
- 优化
title: "flat_map 设计指南（二）：逐步实现"
---
# flat_map 设计指南（二）：逐步实现

上一篇咱们把动机和接口聊透了,这一篇笔者想换个走法:别再纸上谈兵,直接一行行把 `flat_tree` / `flat_map` 给撸出来。咱们会一层一层往上叠——从最底下的签名和数据成员开始,一路堆到 `flat_map` 自己那几个特有 API。代码密集,解释只点到为止,细节论证您去看 [full/03-2~03-4](../full/03-2-flat-map-flattree-skeleton.md);配套工程在 `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/`(`19`~`22`),笔者写的时候全程对着它跑测试。

## 第 1 层:把骨架先立起来

笔者一开始动手,先把 `flat_tree` 的类签名和那几个数据成员摆好——地基不稳后面全是坑。

```cpp
// Platform: host | C++ Standard: C++20
#pragma once
#include <algorithm>
#include <cassert>
#include <functional>
#include <ranges>
#include <utility>
#include <vector>

namespace tamcpp::chrome::internal {

template <class Key, class GetKeyFromValue, class KeyCompare, class Container>
class flat_tree {
public:
    using key_type = Key;
    using key_compare = KeyCompare;
    using value_type = typename Container::value_type;
    using iterator = typename Container::iterator;
    using const_iterator = typename Container::const_iterator;
    using container_type = Container;
    using size_type = typename Container::size_type;

    static constexpr bool is_transparent_comparator =
        requires { typename KeyCompare::is_transparent; };

protected:
    Container body_;
    [[no_unique_address]] KeyCompare comp_;   // EBO:无状态比较器零字节

    flat_tree() = default;
    explicit flat_tree(const KeyCompare& c) : comp_(c) {}

    // 把 key 比较转成 value 比较(双边提取)
    template <typename A, typename B>
    bool less(const A& a, const B& b) const {
        GetKeyFromValue ext;
        return comp_(extract_if_value(ext, a), extract_if_value(ext, b));
    }
    template <typename Ext, typename V>
    static const auto& extract_if_value(Ext& ext, const V& v) {
        if constexpr (std::is_same_v<std::decay_t<V>, value_type>) return ext(v);
        else return v;
    }
};

}  // namespace tamcpp::chrome::internal
```

`[[no_unique_address]]` 让空比较器(默认 `std::less<>`)零字节,这点笔者第一次见是真觉得优雅——一个空对象就这么白白蒸发了。`extract_if_value` 是异构比较的命门:value 走提取器,裸 key 原样放行,所以您拿一个 `int` 去查 `pair<int,string>` 的表也不用先包一层。

## 第 2 层:构造,以及那个跑不掉的 sort_and_unique

骨架立起来之后,接下来要把"进来一堆乱序的东西,怎么变成有序去重的表"这件事解决掉。这就是 `sort_and_unique`。

```cpp
// 普通 range 构造:append + 排序去重
template <class InputIt>
flat_tree(InputIt first, InputIt last, const KeyCompare& c = KeyCompare())
    : body_(first, last), comp_(c) {
    sort_and_unique();
}
// 容器 move 构造:批量构造(推荐姿势)
flat_tree(Container&& body, const KeyCompare& c = KeyCompare())
    : body_(std::move(body)), comp_(c) {
    sort_and_unique();
}

void sort_and_unique() {
    std::stable_sort(body_.begin(), body_.end(),
                     [this](const value_type& a, const value_type& b) { return less(a, b); });
    body_.erase(std::unique(body_.begin(), body_.end(),
                            [this](const value_type& a, const value_type& b) {
                                return !less(a, b) && !less(b, a);
                            }),
                body_.end());
}
```

`stable_sort` + `unique` + `erase`,O(N log N)。这里有个笔者一开始没在意的点:为什么是 `stable_sort` 而不是 `sort`?因为相等元素的相对顺序,`stable_sort` 给您留着,`sort` 不保证——您要是后面 `replace` 进来的数据依赖这个顺序,`sort` 可能就给您打散了。`unique` 那个 lambda 用的是 `!less(a,b) && !less(b,a)` 判等,意思是"既不小于也不被小于",这正好是等价的定义,比直接 `==` 在异构比较里更稳。

## 第 3 层:sorted_unique 构造——给已经排好的数据开个后门

事情到这里就有意思了。您要是手头的数据本来就是有序去重的(比如从另一个 `flat_map` 倒出来的),再跑一遍 `sort_and_unique` 纯属浪费。Chromium 给这种场景留了个后门,就是 `sorted_unique` tag。

```cpp
struct sorted_unique_t {};
inline constexpr sorted_unique_t sorted_unique{};

template <class InputIt>
flat_tree(sorted_unique_t, InputIt first, InputIt last, const KeyCompare& c = KeyCompare())
    : body_(first, last), comp_(c) {
    assert(is_sorted_unique());   // debug 校验,不排序
}

bool is_sorted_unique() const {
    for (size_type i = 1; i < body_.size(); ++i)
        if (!less(body_[i - 1], body_[i])) return false;   // 必须严格升序
    return true;
}
```

标签让重载决议跳过 `sort_and_unique`,只 DCHECK。O(N) 拷贝构造,release 下连那个 `assert` 都没了。笔者第一次看这里的时候心里咯噔了一下——这不是把契约责任甩给您了吗?确实是。您要是拿一堆没排序的数据塞进 `sorted_unique` 构造,debug 下还能抓出来,release 下就 silent corruption 了。这是个诚实契约,您得自己知道自己在干嘛。

## 第 4 层:查找,二分的事儿

有序数组的查找没什么悬念,`std::lower_bound` 二分,O(log n)。但这里有个细节值得停下来看一眼。

```cpp
const_iterator find(const Key& key) const {
    auto it = std::lower_bound(body_.begin(), body_.end(), key,
        [this](const value_type& v, const Key& k) { return less(v, k); });
    if (it != body_.end() && !less(key, *it)) return it;
    return body_.end();
}
bool contains(const Key& key) const { return find(key) != body_.end(); }
size_type count(const Key& key) const { return contains(key) ? 1 : 0; }
```

`std::lower_bound` 二分,O(log n),只传**一个**二元比较器 `(value, key)→bool`——透明比较器时 `key` 可为异构类型,所以您拿 `std::string_view` 查 `std::string` 的表也不用先转。`find` 里那行 `!less(key, *it)` 是判等的小技巧:`lower_bound` 给您的是"第一个不小于 key 的位置",如果这个位置既不小于 key、key 也不小于它,那就是相等,返回它;否则返回 `end()`。

这里有个笔者写的时候踩过的小坑要提醒您:Chromium 的 flat_tree 用的是 `std::ranges::lower_bound(*this, key, KeyValueCompare(comp_))`。那个 `KeyValueCompare` 是一个**带两个 `operator()` 重载**的比较器类(`v<k` 和 `k<v` 各一个),不是两个并列 lambda;`ranges::lower_bound` 也只收一个比较器对象。咱们上面这种"一个 lambda + `extract_if_value`"的写法是教学简化,行为上等价,但 Chromium 那个版本更能扛异构查询的边角情况。

## 第 5 层:插入——O(n) 的那个 shift,跑不掉

插入是 flat_map 性能辩论的火药中心。咱们前面设计篇聊过,这里把它落地。

```cpp
std::pair<iterator, bool> insert(value_type v) {
    auto it = std::lower_bound(body_.begin(), body_.end(), v,
        [this](const value_type& a, const value_type& b) { return less(a, b); });
    if (it != body_.end() && !less(v, *it)) return {it, false};   // 已存在,不插
    return {body_.emplace(it, std::move(v)), true};               // O(n) shift,插入成功
}
```

`lower_bound` 找位置 O(log n),`emplace` 在那个位置 shift 后面所有元素 O(n)。这就是 flat_map 插入的代价—— vector 中间插一个,后面全得挪。唯一 key 的语义也在里面了:key 已存在就返回 `{it, false}`,不插。这一步笔者想强调一下:返回的 `bool` 一定要看,笔者自己刚写的时候偷懒没看,后面 debug 了半天才发现是重复插入被静默吞了。

## 第 6 层:extract 和 replace——批量重建的两把扳手

插入是单元素的细活,但有时候您想一次性倒一大堆进去,或者把整个容器换掉。`extract` 和 `replace` 就是干这个的。

```cpp
container_type extract() && {
    return std::exchange(body_, container_type{});   // 整体交出
}
void replace(container_type&& body) {
    body_ = std::move(body);
    assert(is_sorted_unique());   // sorted_unique 式诚实契约
}
iterator erase(const_iterator pos) { return body_.erase(pos); }   // O(n)
```

`extract` 是 `&&` 修饰的——只能用在右值上,意思是"容器把里面的东西掏空,您自己拿去用,它就剩个空壳了"。`replace` 走的是 `sorted_unique` 那条诚实契约路线:它信您传进来的数据是有序去重的,只在 debug 下 `assert` 检一下,release 直接接管。这两把扳手配合起来很有用——比如您想批量更新一个 flat_map,可以先 `extract` 出来,在外面乱序改一通,排序去重后再 `replace` 回去,比一次次 `insert` 快得多。

## 第 7 层:flat_map 自己的那几个 API

前面六层都是 `flat_tree` 的活儿,跟 map 还是 set 无关。真正属于 `flat_map` 的就下面这几个:operator[]、at、insert_or_assign。这也是 `flat_tree` / `flat_map` 分层的意义——核心引擎一套,map 在上面套薄壳。

```cpp
namespace tamcpp::chrome {

struct GetFirst {
    template <class K, class V>
    constexpr const K& operator()(const std::pair<K, V>& p) const { return p.first; }
};

template <class Key, class Mapped, class Compare = std::less<>,
          class Container = std::vector<std::pair<Key, Mapped>>>
class flat_map : public internal::flat_tree<Key, GetFirst, Compare, Container> {
    using base = internal::flat_tree<Key, GetFirst, Compare, Container>;
public:
    using mapped_type = Mapped;
    using base::base;   // 继承 flat_tree 的构造/查找/插入

    mapped_type& operator[](const Key& key) {
        auto it = std::lower_bound(this->body_.begin(), this->body_.end(), key,
            [this](const value_type& v, const Key& k) { return this->less(v, k); });
        if (it == this->body_.end() || this->less(key, *it))
            it = this->body_.emplace(it, std::piecewise_construct,
                                     std::forward_as_tuple(key),
                                     std::forward_as_tuple());   // 默认构造 mapped
        return it->second;
    }

    mapped_type& at(const Key& key) {
        auto it = this->find(key);
        assert(it != this->body_.end());   // 教学版用 assert;Chromium 用 CHECK
        return it->second;
    }

    template <class M>
    std::pair<iterator, bool> insert_or_assign(const Key& key, M&& obj) {
        auto it = std::lower_bound(this->body_.begin(), this->body_.end(), key,
            [this](const value_type& v, const Key& k) { return this->less(v, k); });
        if (it != this->body_.end() && !this->less(key, *it)) {
            it->second = std::forward<M>(obj);   // 覆写 .second(只改 mapped,不改 key)
            return {it, false};
        }
        return {this->body_.emplace(it, key, std::forward<M>(obj)), true};
    }
};

template <class Key, class Compare = std::less<>, class Container = std::vector<Key>>
using flat_set = internal::flat_tree<Key, std::identity, Compare, Container>;

}  // namespace tamcpp::chrome
```

`operator[]` 缺失就插一个默认构造的 mapped——这就是为什么 flat_map 用 `vector<pair<Key, Mapped>>` 而不是 `vector<pair<const Key, Mapped>>`,因为要能默认构造进去,`const Key` 干不了这事儿。`at` 越界走 assert,教学版图省事;Chromium 用的是 CHECK,因为 release 也得崩。`insert_or_assign` 是个有意思的 API——key 在就覆写 `.second`,不在就插,返回的 bool 告诉您到底是插了还是改了。最后 `flat_set` 那一行笔者特别喜欢:`using` 别名 + `std::identity` 提取器,零额外代码,这就是把核心下沉到 `flat_tree` 的红利。

## 跑一下,看它动不动

写完不跑一下心里没底。咱们来一段最小的——构造、查、改、再来个 set。

```cpp
#include <iostream>
int main() {
    using namespace tamcpp::chrome;
    flat_map<int, std::string> m{{3,"c"},{1,"a"},{2,"b"}};   // 构造即排序
    std::cout << m.size() << "," << m[1] << "\n";            // 3,a
    m.insert_or_assign(2, "B");                              // 覆写 2
    std::cout << m[2] << "\n";                               // B

    flat_set<int> s{{3,1,2,1}};                              // 排序去重
    std::cout << s.size() << "\n";                           // 3
    return 0;
}
```

到这儿七层全撸完了,flat_tree 那一层是真正的实现核心,`flat_map`(子类 + `GetFirst`)和 `flat_set`(别名 + `std::identity`)都是在它上面套薄壳。咱们一路上踩到的那些点——`sort_and_unique` 维护有序不变量、`lower_bound` 二分、`emplace` 那个 O(n) shift、`sorted_unique` 跳过排序、`extract`/`replace` 批量重建、`[[no_unique_address]]` 蒸发空比较器——这些就是 flat_map 全部的内功了。代码本身不算多,但每一处取舍背后都有讲头,这也是笔者写这篇的时候越写越觉得过瘾的地方。下一篇咱们把测试和性能对比补上,看看它跟 `std::map` 真刀真枪比起来到底差多少。

## 参考资源

- [Chromium `base/containers/flat_tree.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [flat_map 设计指南（三）：测试策略与性能对比](./03-flat-map-testing.md)
- [flat_map 实战（二）：flat_tree 核心骨架](../full/03-2-flat-map-flattree-skeleton.md)
