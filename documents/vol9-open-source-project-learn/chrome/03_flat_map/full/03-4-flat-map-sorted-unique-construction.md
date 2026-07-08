---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: 实现 sorted_unique 构造优化——用 tag dispatch 跳过 sort_and_unique,把批量构造从
  O(N log N) 降到 O(N),配 DCHECK 诚实契约,并讲清何时该用它
difficulty: intermediate
order: 4
platform: host
prerequisites:
- flat_map 实战（三）：查找与插入
- flat_map 前置知识（四）：tag dispatch 与 sorted_unique_t
reading_time_minutes: 10
related:
- flat_map 实战（五）：迭代器失效与批量构造
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 零开销抽象
title: "flat_map 实战（四）：sorted_unique 构造优化"
---
# flat_map 实战（四）：sorted_unique 构造优化

上一篇咱们把 flat_map 的单元素插入拆透了,每次 insert 都是 `O(n)` 的 shift。这玩意儿平时一个个插感觉不出什么,可您要是拿它去构造一个挺大的 flat_map,比如启动时加载配置表,那 `O(N²)` 的总代价能把人等死。笔者之前就在一个 10 万元素的配置上栽过,启动慢得离谱,profile 一看全耗在 shift 上。

这一篇咱们就专攻构造期怎么绕开这堵墙。先说批量构造这条路,数据先攒进 vector、最后一刀 move 进 flat_map,`O(N log N)` 一次排序收尾。然后是真正的重头戏 sorted_unique 构造,数据本来就有序的话,排序那步直接跳过,降到 `O(N)`。这条优化路子是 [pre-04 tag dispatch](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md) 的落地现场,咱们把它从头到尾走一遍。

---

## 陷阱:逐个 insert 构造是 O(N²)

最直觉的写法就是写个循环一个个 insert,看着人畜无害:

```cpp
flat_map<int, Config> m;
for (auto& [k, v] : load_data()) {
    m.insert({k, v});   // 每次都 O(n) shift
}
```

咱们把代价摊开看。第 1 次 insert 是 `O(1)`,第 2 次 `O(2)`,一路涨到第 N 次 `O(N)`,总代价 `O(1) + O(2) + ... + O(N) = O(N²)`。数据量一上去就是灾难。拿 10 万元素来说,shift 总次数大概 `10⁸`,实测能磨好几秒,这就是笔者当初踩坑的现场。

flat_map 的接口设计者显然也清楚这件事,所以他们给构造期单独留了更便宜的路。

---

## 批量构造:先填 vector 再 move,O(N log N)

绕开 `O(N²)` 的办法是批量。先把数据一股脑塞进一个 vector,再把这个 vector 整个 move 进 flat_map:

```cpp
std::vector<std::pair<int, Config>> raw;
raw.reserve(N);
for (auto& [k, v] : load_data()) raw.emplace_back(k, v);   // vector push_back,摊还 O(1)

flat_map<int, Config> m(std::move(raw));   // move 构造,内部一次排序
```

`flat_map(container_type&& items)` 这个构造(flat_tree.h:578 附近)干的事很简单:接管 vector 的存储,这是一次 `O(1)` 的 move,然后调一次 `sort_and_unique`,代价 `O(N log N)`。总构造代价就压到 `O(N log N)`。和逐个 insert 的 `O(N²)` 比一比,还是 10 万元素,`N log N ≈ 1.7×10⁶` 对上 `N² = 10¹⁰`,差着四个数量级。

这就是 flat_map 官方推荐的构造姿势,数据先在 vector 里攒好,享受 push_back 摊还的 `O(1)`,最后一刀 move 进去。flat_map.h:61-62 那段文档原话就是这么写的:"If possible, construct a flat_map in one operation by inserting into a container and moving that container into the flat_map constructor."

---

## sorted_unique:跳过排序,O(N)

走到这一步,如果您手里的数据本来就排好序了、也没有重复,那批量构造里那个 `O(N log N)` 的 `sort_and_unique` 就是白干的活。直接接管不就完了。这就是 `sorted_unique` 构造要解决的事:

```cpp
std::vector<std::pair<int, Config>> raw = load_already_sorted_data();   // 已有序
flat_map<int, Config> m(sorted_unique, std::move(raw));   // 跳过 sort_and_unique,O(N)
```

第一个参数传个 `sorted_unique` 标签,flat_map 就走那个跳过排序的构造重载(flat_tree.h:606-646)。它总共就两件事:接管 vector,`O(1)` move;然后跑一遍 `DCHECK(is_sorted_and_unique(...))` 做 debug 校验。release 编译下 DCHECK 是空,所以总代价就是接管那一刀,纯 `O(N)`。

### 5 个 sorted_unique 重载

flat_tree 为 sorted_unique 准备了 5 个重载,各种输入来源都覆盖到了:

- `flat_map(sorted_unique, InputIterator first, last, comp)`
- `flat_map(sorted_unique, from_range_t, Range&&, comp)` (C++23 ranges)
- `flat_map(sorted_unique, const container_type&, comp)`
- `flat_map(sorted_unique, container_type&&, comp)` ← 上面用的
- `flat_map(sorted_unique, initializer_list, comp)`

它们和对应的普通构造只有一个差别:不调 `sort_and_unique`。机制是 tag dispatch,咱们在 [pre-04](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md) 已经拆过了。`sorted_unique_t` 是个空的 tag 类型,编译器靠"您传不传这个 tag"在重载决议期选不同的函数,运行期一分钱开销都没有。

---

## DCHECK(is_sorted_and_unique):诚实契约

问题来了。您跟 flat_map 拍胸脯说"数据有序了",可万一其实没序呢?flat_map 在 debug 下用 `DCHECK` 抓您撒谎(flat_tree.h:612/624/633/642):

```cpp
flat_tree(sorted_unique_t, container_type&& body, const Compare& comp)
    : body_(std::move(body)), comp_(comp) {
    DCHECK(is_sorted_and_unique(body_, comp_));   // debug 校验
}
```

`is_sorted_and_unique`(flat_tree.h:55-62)的实现在 [pre-04](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md) 看过:

```cpp
template <typename Range, typename Comp>
constexpr bool is_sorted_and_unique(const Range& range, Comp comp) {
    return std::ranges::adjacent_find(range, std::not_fn(comp)) ==
           std::ranges::end(range);
}
```

它扫一遍相邻元素,确认每个都严格小于下一个,既没有相等的也没有逆序的。一遍 `O(N)`,只在 debug 下跑。您要是撒谎了,debug 测试就 abort 给您看;release 下 `DCHECK` 编译成空,一个字都不校验,完全信您。

笔者管这份约定叫诚实契约。flat_map 给您 `O(N)` 构造的优化,交换条件就是您得保证数据真的有序;debug 帮您把关这个保证,release 就放手信任。所以数据来源不靠谱的时候,比如用户输入、网络抓来的东西,就别硬上 sorted_unique,老老实实用普通批量构造让 flat_map 替您排。

---

## 何时用 sorted_unique

判断标准其实就一句话:您的数据来源能不能可信地保证有序、无重复。

能保证的情况挺好认。数据是从另一个有序容器来的,比如另一个 flat_map 导出、或者一个 `std::set`;或者是您自己刚拿 `std::sort` 加 `unique` 处理过一遍;再或者就是编译期写死的常量,像配置表那种 initializer_list,您手写的时候盯着它是有序的。这些场景 sorted_unique 都用得踏实。

反过来,数据要是从用户输入、文件、网络来的,顺序根本不可控,就别冒险。还有一类容易漏的:您吃不准有没有重复。普通构造会帮您去重,sorted_unique 不会,一旦有重复元素溜进去,flat_map 的不变量就被您亲手破坏了,后面查找行为直接变成玄学。拿不准的时候,就用普通批量构造让 flat_map 自己排序去重,代价也就是 `O(N log N)`,比逐个 insert 还是快得多。

---

## 一个最小复刻

道理讲完了,咱们自己撸一个最小版的 MiniMap,把这两条构造路径都跑一遍,您就看得更清楚:

```cpp
// Platform: host | C++ Standard: C++20
#include <algorithm>
#include <cassert>
#include <vector>

struct sorted_unique_t {};
inline constexpr sorted_unique_t sorted_unique{};

class MiniMap {
public:
    // 普通构造:排序去重
    MiniMap(std::vector<int> data) : data_(std::move(data)) {
        std::sort(data_.begin(), data_.end());
        data_.erase(std::unique(data_.begin(), data_.end()), data_.end());
    }
    // sorted_unique 构造:跳过排序,debug 校验
    MiniMap(sorted_unique_t, std::vector<int> data) : data_(std::move(data)) {
        assert(is_sorted_unique());   // debug 抓撒谎
    }
    std::size_t size() const { return data_.size(); }
private:
    bool is_sorted_unique() const {
        for (std::size_t i = 1; i < data_.size(); ++i)
            if (!(data_[i-1] < data_[i])) return false;
        return true;
    }
    std::vector<int> data_;
};

int main() {
    MiniMap a{std::vector<int>{3, 1, 2, 1}};     // 普通构造,排序去重 → 3 元素
    MiniMap b(sorted_unique, std::vector<int>{1, 2, 3, 4});  // 跳过排序 → 4 元素
    // MiniMap c(sorted_unique, std::vector<int>{1, 3, 2});  // 撒谎!debug abort
    return 0;
}
```

---

到这里,flat_map 单元素插入那堵 `O(n)` 的墙咱们算是绕开了。批量构造这条路,先填 vector 再一刀 move 进去,`O(N log N)` 收尾;数据本来有序的话,sorted_unique 标签一传,排序那步直接省掉,`O(N)` 纯接管,配 debug 下的 `DCHECK` 把关,这就是 tag dispatch 在构造期省下的真金白银。

flat_map 还剩两件事值得讲透,一是迭代器失效规则,二是更多的批量构造模式,咱们后续接着拆。

## 参考资源

- [Chromium `base/containers/flat_tree.h` —— sorted_unique 重载与 is_sorted_and_unique](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h` —— 批量构造建议](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [flat_map 前置知识（四）：tag dispatch 与 sorted_unique_t](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md)
