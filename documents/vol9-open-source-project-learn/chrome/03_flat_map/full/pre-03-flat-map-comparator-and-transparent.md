---
chapter: 0
cpp_standard:
- 14
- 17
- 20
description: 拆解 flat_map 的比较器——strict weak order 要求、std::less vs 透明的 std::less<>,
  以及 transparent comparator 如何用 ConditionalT 编译期分流实现异构查找
difficulty: intermediate
order: 3
platform: host
prerequisites:
- flat_map 前置知识（二）：复杂度与摊还分析
reading_time_minutes: 10
related:
- flat_map 前置知识（五）：NO_UNIQUE_ADDRESS + EBO + pair 类型
- OnceCallback 前置知识（四）：Concepts 与 requires 约束
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 类型安全
title: "flat_map 前置知识（三）：比较器、strict_weak_order 与透明查找"
---
# flat_map 前置知识（三）：比较器、strict_weak_order 与透明查找

flat_map 是有序容器。但"有序"这俩字得追一句——按什么排?答这篇要拆两件事,都跟比较器有关。一件是,比较器不是随便写个 `<` 就完事的,它得满足一条叫 strict weak order 的数学契约,不然容器排序、查找都可能出错。另一件是,flat_map 默认的比较器是 `std::less<>`(透明的),而不是 `std::less<Key>`(不透明的),这个差别看着小,但在查找热路径上能差出一次 malloc/free——现代 C++ 为什么推崇 `std::less<>`,根子就在这。

## 比较器:决定顺序的函数对象

先看 flat_map 的模板签名,`flat_map<Key, Mapped, Compare = std::less<>, Container = ...>`(flat_map.h:190-193)。第三个模板参数 `Compare` 就是比较器,本质上是个函数对象,接两个参数,返回第一个是不是该排在第二个前面。

默认值是 `std::less<>`,意思是拿 `<` 比大小,`flat_map<int, std::string>` 默认就按 int 从小到大。您也可以传自己的,比如按字符串长度排:

```cpp
struct ByLength {
    bool operator()(const std::string& a, const std::string& b) const {
        return a.size() < b.size();
    }
};
flat_map<std::string, Config, ByLength> m;   // 按字符串长度排序
```

但比较器不是您想怎么写就怎么写的,它背后有一条数学契约管着。

## strict weak order:比较器的数学契约

容器要能正确排序、正确查找,您给的比较器必须满足 strict weak order(严格弱序)。这玩意儿四条性质,笔者一条条说。

非自反,`comp(a, a)` 必为 false,a 不能比自己小。反对称,`comp(a, b)` 为 true 时 `comp(b, a)` 必为 false。传递,`comp(a, b)` 且 `comp(b, c)` 成立,`comp(a, c)` 必成立。前三条其实就是 `<` 的性质,直觉上不难接受。

真正容易漏的是第四条,不可比传递。什么叫"不可比"?就是 `!comp(a,b) && !comp(b,a)`,即 a 不比 b 小、b 也不比 a 小,也就是俩玩意儿"相等"。第四条要求:如果 a 和 b 相等,b 和 c 相等,那 a 和 c 也必须相等。这条保证"相等"是个货真价实的等价关系,可传递,容器才能把元素划成等价类、一类一类排好。

笔者强调第四条,是因为它最容易在不经意间被违反。浮点比较里那个 NaN,`NaN < x` 和 `x < NaN` 都是 false,按理说"相等",可两个 NaN 之间也比不出——不可比传递直接破。再比如您写个带容差的比较器,`abs(a-b) < eps` 就算相等,eps 选不好、比较顺序不稳,"相等"这个关系就不传递了,排序结果就乱。元素可能在一次 find 里"消失",或者重复出现,这种 bug 不好查。C++20 把这条契约编进了 `std::strict_weak_order` concept,您可以拿它约束自己的比较器,编译期就能拦住。

一句话:用 `<` 比大小不用操心;自己写比较器,尤其按多字段、带容差的,心里得装着这四条。

## std::less vs std::less<>:不透明 vs 透明

到这儿进入现代 C++ 的一个重要区别。`std::less` 有两种长相。一种叫 `std::less<Key>`,C++98 就有,不透明,只吃 `Key` 这一种类型——`std::less<std::string>` 的 `operator()` 签名就是 `bool operator()(const std::string&, const std::string&)`,您传别的它不认。另一种叫 `std::less<>`,C++14 加进来的,透明,模板化的 `operator()`,啥类型都接,内部走 `<`,所以也叫 transparent comparator(透明比较器)。

这里有个细节您可能没留意:`std::map` 默认用的是 `std::less<Key>`,不透明;而 flat_map 默认用的是 `std::less<>`,透明(flat_map.h:192)。同一个标准库家族,一个保守一个激进,根子就在这。这个差别乍看无关紧要,对查找性能的影响却很实在。

## 透明查找:不构造临时对象

假设您手上一个 `flat_map<std::string, Config>`,想查 key `"timeout"`:

```cpp
flat_map<std::string, Config> m;
auto it = m.find("timeout");   // "timeout" 是 const char[8]
```

`"timeout"` 这玩意儿是 `const char[8]`,不是 `std::string`。比较器如果是 `std::less<std::string>`(不透明),那 `find` 的参数必须是 `std::string`,容器没办法,只能先拿您的 `const char[8]` 构造一个临时 `std::string`——分配堆内存、拷贝字符——再拿这个临时去二分比,比完析构掉。一次查,平白多一对 malloc/free。

如果比较器是 `std::less<>`(透明),情况就不一样了。`find` 直接拿 `const char*` 比,因为 `std::string` 和 `const char*` 都能走 `<`(或者 `std::less<void>::operator()` 那条泛型路径),压根不用构造临时 `std::string`。这就是透明查找的价值——查找一次,省一次临时构造。

对 int 这种轻 key,无所谓;对 `std::string`、自定义类型这种重 key,在热路径上反复 find,省下来的临时构造能累加成可观的数字。笔者第一次意识到这点是在 profile 一个配置热路径的时候,malloc 榜上一大片 `std::string` 临时,全来自 map.find,换成透明比较器之后那一片直接消失——印象很深。

## flat_map 怎么实现透明:KeyT 的编译期分流

那 flat_map 怎么知道比较器透不透明?靠一个编译期的类型特征,叫 `is_transparent`。透明比较器(像 `std::less<>`)内部带一个嵌套类型 `is_transparent`,就是个空的 struct 当标记;不透明的(像 `std::less<int>`)没有这个类型。flat_tree 拿 `KeyT<K>` 别名在编译期分流(flat_tree.h:109-111):

```cpp
template <typename K>
using KeyT = ConditionalT<
    requires { typename KeyCompare::is_transparent; },   // 比较器透明吗?
    K,                                                     // 透明:保留调用方传的 K
    Key>;                                                  // 不透明:强制回退到 Key
```

`ConditionalT` 跟 `std::conditional_t` 长得像,但参数之间不互相依赖,可以正常推导。逻辑一句话:比较器透明,`KeyT<K>` 就等于调用方传进来的 `K`,比如 `const char*`;比较器不透明,`KeyT<K>` 就被强制掰回 `Key`,比如 `std::string`。

于是 `find` 的签名就跟着比较器变了:

```cpp
// 透明比较器(std::less<>):可接受异构 key
template <class K = Key>
auto find(const KeyT<K>& key);   // KeyT<K> = K(透明)

// 不透明比较器(std::less<string>):只接受 Key
template <class K = Key>
auto find(const KeyT<K>& key);   // KeyT<K> = Key = string
```

调用方传 `const char*`,透明版本直接吃下去;不透明版本得把 `const char*` 隐式转成 `std::string`(构造临时)才匹配得上。这一切全在编译期搞定,运行期零开销。笔者认为这套设计最漂亮的地方就在这——同一个 `find` 签名,行为完全靠比较器的一个嵌套类型在编译期切,用户代码一行不用改。

## KeyValueCompare:异构比较的实现细节

再往下钻一层。底层怎么拿异构 key,去比一个存了 `pair<K,V>` 的元素?flat_tree 的 `KeyValueCompare`(flat_tree.h:439-462)用了两个 `extract_if_value_type` 重载来挡这件事。比较的某一侧如果是 `value_type`(就是 `pair<K,V>`),先走 `GetKeyFromValue` 把 key 抠出来再比;某一侧如果是裸 `K`(异构 key,比如 `const char*`),原样放行,直接比。

这么一来,`lower_bound(data, "timeout", comp)` 就能对一个存着 `pair<std::string, Config>` 的数组,直接拿 `const char*` 去比——既不用把 `"timeout"` 包成 `pair`,也不用把元素里整个 value 拆出来。这就是异构查找能落到二分循环里去的实现细节,看着不起眼,但它把"异构 key"和"存 value_type 的数组"这俩看起来对不上的东西,用一层重载抹平了。

## 一个最小复刻

咱们自己撸个透明比较的最小版,体会一下编译期分流:

```cpp
// Platform: host | C++ Standard: C++20
#include <compare>
#include <concepts>
#include <iostream>
#include <string>

// 透明比较器(带 is_transparent 标记)
struct TransparentLess {
    using is_transparent = void;   // 关键:标记透明
    template <typename A, typename B>
    bool operator()(A&& a, B&& b) const { return std::forward<A>(a) < std::forward<B>(b); }
};

// 不透明比较器(无 is_transparent)
struct OpaqueLess {
    bool operator()(const std::string& a, const std::string& b) const { return a < b; }
};

template <typename Comp, typename K>
constexpr bool is_transparent_v = requires { typename Comp::is_transparent; };

int main() {
    std::cout << std::boolalpha;
    std::cout << "TransparentLess 透明? " << is_transparent_v<TransparentLess, int> << "\n";  // true
    std::cout << "OpaqueLess     透明? " << is_transparent_v<OpaqueLess, int> << "\n";        // false
    return 0;
}
```

把这里的 `is_transparent_v` 套进前面那行 `ConditionalT`,就是 flat_tree 的 `KeyT` 分流。真实代码里 flat_tree.h:109-111,一字不差。

讲到这里,flat_map 的比较器这块算是拆透了。strict weak order 是排序正确的数学底座,`std::less<>` 比 `std::less<Key>` 强在能用异构 key 查找、不构造临时对象,flat_map 默认走透明路线,靠 `is_transparent` 标记加 `ConditionalT` 在编译期把分流做完,运行期一文不收。

flat_map 还藏着另一个挺巧的零成本构造——`sorted_unique_t` 标签,走 tag dispatch 跳过排序,下一篇咱们再拆。

## 参考资源

- [cppreference: std::less(含 transparent 形态)](https://en.cppreference.com/w/cpp/utility/functional/less)
- [cppreference: strict_weak_order(C++20 concept)](https://en.cppreference.com/w/cpp/concepts/strict_weak_order)
- [cppreference: is_transparent 与异构查找](https://en.cppreference.com/w/cpp/utility/functional/less_void)
- [Chromium `base/containers/flat_tree.h` —— KeyT/KeyValueCompare](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
