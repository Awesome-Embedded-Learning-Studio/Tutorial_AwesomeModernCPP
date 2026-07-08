---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: 拆解 flat_map 的 sorted_unique_t 标签分发——用空 tag 类型在重载决议期挑函数,
  跳过 sort_and_unique,配 DCHECK 做防御性校验,零运行期开销
difficulty: intermediate
order: 4
platform: host
prerequisites:
- flat_map 前置知识（三）：比较器、strict_weak_order 与透明查找
reading_time_minutes: 9
related:
- flat_map 实战（四）：sorted_unique 构造优化
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 零开销抽象
title: "flat_map 前置知识（四）：tag dispatch 与 sorted_unique_t"
---
# flat_map 前置知识（四）：tag dispatch 与 sorted_unique_t

[pre-02](./pre-02-flat-map-complexity-and-amortized.md) 那篇笔者顺手埋了个钩子:flat_map 的批量构造是 `O(N log N)`,因为它拿到数据得先排一遍、再去个重(`sort_and_unique`)。笔者当时写完心里就嘀咕,这要是数据本来就是有序的,这一遍岂不是白排?Chromium 的工程师也想到了这茬,他们的答法挺干净:您构造时甩个 `sorted_unique_t` 标签进去,等于跟它说"放心,这批数据有序无重复",它就把排序那步跳了,构造直接掉到 `O(N)`。

这一篇咱们就拆这个标签背后的机制,也就是 tag dispatch(标签分发),外加 flat_map 怎么在 debug 下用 `DCHECK` 抓那些嘴上打包票、手上递乱数据的人。

## 问题:批量构造每次都要排序吗

flat_map 的普通构造——不管是传 vector、initializer_list 还是 range——默认都走 `sort_and_unique`(flat_tree.h:567/578/586/594)。它没办法,它根本不知道您给它的数据是不是排好的,只能先排再去重,保险第一。

可工程里"数据本来就排好了"的场景一抓一大把。配置是从另一个有序容器拷过来的、点云是上游 pipeline 处理过按 id 排好的、测试 fixture 是您手写的有序 list。这种时候还让它 `O(N log N)` 排一遍,纯粹白烧 CPU:

```cpp
std::vector<std::pair<int, Config>> raw = load_config();  // 已知有序
flat_map<int, Config> m(raw.begin(), raw.end());           // 还是会再排一次!
```

`raw` 明明已有序,flat_map 照样花 `O(N log N)` 再排一遍。数据集小您不在意,真上到百万量级,这个对数因子就肉疼了。

---

## tag dispatch:用类型挑函数

解决这事的套路有个名字,叫 tag dispatch(标签分发)。思路其实朴素得有点过分:定义一个空的"标签类型",构造时传不传这个标签,让编译器在重载决议期挑不同的函数。标签本身是空 struct,运行期什么都不传,自然零开销。

这招在标准库里到处都是。`std::sort` 的并行版您传个 `std::execution::par`,它就选并行算法;不传,选串行。标签自己不带任何数据,纯粹是给重载决议一个"分流信号"。iterator_category 那一套也是同源的把戏——`std::random_access_iterator_tag` 往重载里一塞,算法就走随机访问那条快速路径。

### flat_map 的 sorted_unique_t

flat_map 用的就是这一套(flat_tree.h:28-31):

```cpp
struct sorted_unique_t {
    constexpr sorted_unique_t() = default;
};

inline constexpr sorted_unique_t sorted_unique;
```

一个空 struct(只留着默认构造),外加一个 `constexpr` 实例 `sorted_unique`。您构造 flat_map 时把 `sorted_unique` 当第一个参数塞进去,重载决议就会把您引到那个"跳过排序"的构造函数:

```cpp
std::vector<std::pair<int, Config>> raw = load_config();  // 已知有序
flat_map<int, Config> m(sorted_unique, raw.begin(), raw.end());  // 跳过 sort!
```

第一个参数是标签,后面才是数据。标签本身不带负载,它存在的全部意义就是让编译器在重载决议期说一句"哦,走那条不排序的路"。

---

## 5 个 sorted_unique 重载

flat_tree 给 sorted_unique 配了 5 个构造重载(flat_tree.h:606-646),InputIterator range、`from_range_t`、`const container_type&`、`container_type&&`、`initializer_list` 五种输入形态各一个。它们和普通构造的差别只有一行:不调 `sort_and_unique`。

```cpp
// 普通构造(flat_tree.h:567 附近):排序去重
flat_tree(InputIterator first, InputIterator last, ...) {
    insert(first, last);
    sort_and_unique();   // 花钱的排序
}

// sorted_unique 构造(flat_tree.h:606 附近):跳过排序
flat_tree(sorted_unique_t, InputIterator first, InputIterator last, ...) {
    insert(first, last);
    DCHECK(is_sorted_and_unique(...));   // 只 debug 校验,不排序
}
```

两个重载参数列表的差别只有打头的那个 `sorted_unique_t`。编译器据此在重载决议期分流。`sorted_unique_t` 是空类型,实例不占空间,传它等于没传,这次"选择"在运行期一个字节都不收您。

---

## DCHECK(is_sorted_and_unique):debug 抓撒谎的人

可嘴上说"有序"的人,手上递的数据未必真有序。flat_map 不傻信您,它在 debug 构建里挂了个 `DCHECK(is_sorted_and_unique(...))`(flat_tree.h:612/624/633/642)当保险。`is_sorted_and_unique`(flat_tree.h:55-62)长这样:

```cpp
template <typename Range, typename Comp>
constexpr bool is_sorted_and_unique(const Range& range, Comp comp) {
    return std::ranges::adjacent_find(range, std::not_fn(comp)) ==
           std::ranges::end(range);
}
```

它用 `std::ranges::adjacent_find` 配 `std::not_fn(comp)` 扫一遍相邻对:任何相邻元素没满足"严格小于"的(相等或逆序),`adjacent_find` 立刻定位到那个位置,`DCHECK` 翻脸,abort 给您看。

这是一份很 Chromium 风格的契约:debug 下 flat_map 替您校验您保证的真实性,撒谎当场爆;release 下 `DCHECK` 整个编译成空,完全不校验,直接信任您。`is_sorted_and_unique` 本身是 `O(N)`(扫一遍相邻对),但这个钱只在 debug 付——release 是真正的 `O(N)` 构造,append 完直接接管,不排也不校验。

---

## 零成本:release 完全不付费

代价摊开算一笔就清楚了。debug 构建里 sorted_unique 构造是 append(`O(N)`)加上 `DCHECK(is_sorted_and_unique)` 那次 `O(N)` 校验,合起来还是 `O(N)`。release 构建里 `DCHECK` 直接消失,sorted_unique 构造就只剩 append,纯 `O(N)`。

对比一下普通构造:append(`O(N)`)+ `sort_and_unique`(`O(N log N)`,走的是 stable_sort)。大数据集上 `N log N` 比 `N` 慢一个对数因子——100 万元素就是 20 倍的差距。所以 sorted_unique 在 release 下是真·零开销抽象:您只在确信数据有序时用它,省掉那个 `log N` 因子,debug 下还白送一道校验保平安。

---

## 一个最小复刻

咱们自己撸个 tag dispatch 的最小版,亲手感受一下"用类型挑函数"到底长啥样:

```cpp
// Platform: host | C++ Standard: C++17
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

struct sorted_unique_t {};                       // 空 tag 类型
inline constexpr sorted_unique_t sorted_unique{}; // 常量实例

class MiniMap {
public:
    // 普通构造:假设数据无序,内部排序
    MiniMap(std::vector<int> data) : data_(std::move(data)) {
        std::sort(data_.begin(), data_.end());
        std::cout << "  [普通构造] 排序了\n";
    }
    // sorted_unique 构造:信任调用方,跳过排序(debug 下可加 assert 校验)
    MiniMap(sorted_unique_t, std::vector<int> data) : data_(std::move(data)) {
        assert(is_sorted_unique());   // debug 校验
        std::cout << "  [sorted_unique 构造] 跳过排序\n";
    }
private:
    bool is_sorted_unique() const {
        for (size_t i = 1; i < data_.size(); ++i)
            if (!(data_[i - 1] < data_[i])) return false;   // 必须严格升序
        return true;
    }
    std::vector<int> data_;
};

int main() {
    std::vector<int> a = {3, 1, 2};
    MiniMap m1(a);                                   // 普通构造,会排序

    std::vector<int> b = {1, 2, 3};                  // 已有序
    MiniMap m2(sorted_unique, b);                    // 跳过排序
    return 0;
}
```

跑一下您会看到两行输出:`[普通构造] 排序了` 和 `[sorted_unique 构造] 跳过排序`。tag dispatch 的全部机制就藏在这两个重载签名里——一个多接个空 tag 参数,编译器据此分流,没有任何运行期开销,也没有任何花活。

前置知识走到这儿就剩最后一块了:`[[no_unique_address]]`、空基类优化(EBO),以及 flat_map 为什么存的是 `pair<K,V>` 而不是 `pair<const K,V>`。

## 参考资源

- [cppreference: tag dispatch(标签分发)](https://en.cppreference.com/w/cpp/named_req/TagDispatch)
- [cppreference: std::sort 与 execution policy 标签](https://en.cppreference.com/w/cpp/algorithm/sort)
- [Chromium `base/containers/flat_tree.h` —— sorted_unique_t 与 DCHECK 校验](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
