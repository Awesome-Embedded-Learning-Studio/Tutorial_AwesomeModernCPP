---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: 拆解 flat_map 的两个内存优化——[[no_unique_address]]/EBO 让空比较器零空间开销,
  以及为什么 flat_map 存 pair<K,V> 而非 std::map 的 pair<const K,V>
difficulty: intermediate
order: 5
platform: host
prerequisites:
- flat_map 前置知识（三）：比较器、strict_weak_order 与透明查找
reading_time_minutes: 9
related:
- flat_map 实战（二）：flat_tree 核心骨架
- WeakPtr 前置知识（六）：TRIVIAL_ABI 与平凡可重locate
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 内存管理
- 零开销抽象
title: "flat_map 前置知识（五）：NO_UNIQUE_ADDRESS、EBO 与 pair 存储"
---
# flat_map 前置知识（五）：NO_UNIQUE_ADDRESS、EBO 与 pair 存储

flat_map 的类定义里挂着几行小注解,头一回读很容易划过去。笔者这次翻代码,特意停下来抠了一下,发现背后藏着两个内存层面的小巧思。一个让无状态比较器(比如默认的 `std::less<>`)一个字节都不占——靠的是 `[[no_unique_address]]`;另一个把存进去的元素从 `pair<const K,V>` 改成了 `pair<K,V>`,这个看起来只是少了个 const 的改动,实际上决定了 flat_map 哪些 API 能写、哪些写不了。咱们这一篇就把这两处拆开看。

## EBO:空对象本该零字节

C++ 有个让笔者第一次知道时直皱眉头的历史包袱:空对象——也就是没有任何数据成员的类——也得占至少 1 字节。规矩是这样:两个不同对象必须各占一个独立地址,而地址是按字节算的,0 字节的对象根本没法有独立地址。所以一个 `struct Empty {}` 的 `sizeof` 不是 0,是 1。

```cpp
struct Empty {};
sizeof(Empty);   // 1(不是 0)
```

这事儿单看不疼,可一旦您想拿空类型当成员使,就难受了。典型场景就是无状态比较器,比如 `std::less<>`——它里面啥也没有,纯粹是个函数调用包装。您要是把它老老实实做成成员:

```cpp
struct Holder {
    std::less<> comp;   // 空对象,但占 1 字节(+ 对齐填充)
    int data;
};
sizeof(Holder);   // 8 字节(1 字节 comp + 3 字节填充 + 4 字节 data)
```

`comp` 理论上零开销(它根本没数据),实际却占了 1 字节,还把对齐填充也带出来了——4 字节无谓的膨胀。

EBO(Empty Base Optimization)就是给"空类作基类"这个场景留的口子:只要空类坐在基类的位置上,编译器就允许它跟派生类共享地址,占用归零:

```cpp
struct Empty {};
struct Holder : Empty {   // 空类作基类
    int data;
};
sizeof(Holder);   // 4 字节(Empty 被优化掉)
```

标准库容器都靠这一招让空 allocator、空 comparator 不拖内存——把它们当基类继承,而不是塞成成员。可 EBO 有个边界:只对基类生效,对成员没辙。您把空对象写成成员,该占还得占。

---

## [[no_unique_address]]:把 EBO 推广到成员

C++20 把上面那条边界给松开了。新加的 `[[no_unique_address]]` 属性告诉编译器:这个成员不需要独占一个地址,要是它是空类型,就别给它分空间。换句话说,EBO 那套从基类挪到了成员身上:

```cpp
struct Empty {};
struct Holder {
    [[no_unique_address]] Empty comp;   // 标注后,空成员可零字节
    int data;
};
sizeof(Holder);   // 4 字节(comp 被 EBO 掉了)
```

Chromium 没让这个属性裸着用,而是包了一层宏 `NO_UNIQUE_ADDRESS`——编译器支持就展开成 `[[no_unique_address]]`,不支持就空着。这样代码不用到处写条件编译。

flat_tree 在两个位置挂了这个宏,都是放比较器的。一个在 `flat_tree` 自身的成员 `key_compare comp_`(flat_tree.h:545),默认那把 `std::less<>` 在这儿就归零;另一个在嵌套的 `value_compare` 里(flat_tree.h:129),同理。所以您写一句 `flat_map<int,int> m;`,里头的比较器是一字节都不占的——容器只为真正存数据的 `vector<pair<K,V>>` 付内存,比较器白送。

---

## GCC vs Clang:对空类型 EBO 等价正确

网上常听到一种说法,说 `[[no_unique_address]]` Clang 比 GCC 好。这话不全是错——在某些非空但可重叠的场景(比如同一个类里挂多个 NUA 成员)两者确实不一样。可落到 flat_map 真正关心的场景上,也就是空类型作成员,笔者实测下来两家的行为是一致的:GCC 16 和 Clang 22 都老老实实把它折到 0 字节。

```cpp
struct Empty {};
struct WithNUA  { [[no_unique_address]] Empty e; int i; };
struct WithoutNUA { Empty e; int i; };
// 实测 GCC 16 / Clang 22:
// sizeof(WithNUA)   = 4 字节(e 被优化掉)
// sizeof(WithoutNUA) = 8 字节(e 占 1 + 填充 3 + i 占 4)
```

所以 flat_map 的空比较器在 GCC 和 Clang 上都吃到了 EBO,那个流行说法您别被带沟里去。

不过 flat_tree.h:542-547 那段注释指向的,是 GCC 一个真实存在但跟语义完全无关的坑([crbug.com/1156268](https://crbug.com/1156268)):特定成员声明顺序下,GCC 会直接吐 ICE(编译器内部错误)——不是 EBO 折叠行为有差,是编译器自己崩了。Chromium 的 workaround 是把 `comp_` 的声明位置挪一挪。这是编译器实现的 bug,跟语言语义半毛钱关系没有,讲课时可别跟"NUA 语义差异"搅在一起讲。

---

## pair<K,V> vs pair<const K,V>:flat_map 的存储选择

第二个设计点更狠,直接把 flat_map 的 API 长相都改了。咱们看 flat_map 的模板签名(flat_map.h:193):

```cpp
template <class Key, class Mapped, class Compare = std::less<>,
          class Container = std::vector<std::pair<Key, Mapped>>>
class flat_map : ...;
```

`Container` 默认是 `std::vector<std::pair<Key, Mapped>>`——看清这个 `pair`,是 `pair<Key, Mapped>`,**不是** `pair<const Key, Mapped>`。这一笔跟 `std::map` 正好反过来,`std::map` 的元素是 `pair<const Key, Mapped>`,key 一进节点就再改不动。

为什么 flat_map 要走非 const 的路?根子在底层容器是 vector。vector 要保持有序,就绕不开 `insert`/`erase` 的 shift——`std::move_backward` 整段搬元素,这一下要求元素类型必须可移动赋值。可 `pair<const Key, Mapped>` 的 `first` 是 const,move-assign 不了:

```cpp
// 实测(C++17):
static_assert(!std::is_move_assignable_v<std::pair<const int, int>>);   // 不可移动赋值 → vector shift 做不了
static_assert( std::is_move_assignable_v<std::pair<int, int>>);        // 可移动赋值
```

`pair<const K, V>` 不可 move-assign,这就把 vector 的 shift 路给堵死了。flat_map 想活下去,只能选非 const 的 `pair<K,V>`。

这里有个容易搞混的点,笔者一开始也被带歪过。`insert_or_assign` 那段覆写写的是 `result.first->second = std::forward<M>(obj)`(flat_map.h:339),**只动 `.second`**,就算换成 `pair<const K,V>` 也能编过——const 的只是 `.first`。所以真正把 const pair 拍死的不是覆写,是 vector 的 shift:它要把整对 move-assign,而 `pair<const K,V>` 给不了。别拿覆写当理由,那不是病根。

代价这边也得说清楚。既然存的是 `pair<K,V>`,那 `first` 非 const,key 理论上能被迭代器改。您要是随手一个 `it->first = new_key`,就把有序不变量破坏了,容器还不知道。这玩意儿纯靠用户自律——跟 [WeakPtr 的序列契约](../../02_weak_ptr/full/pre-03-weak-ptr-sequence-checker-dcheck-check.md)一个性质,release 下不强制,您自己得有数。

`std::map` 这条路上就没这烦恼,`pair<const K,V>` 让 key 天生改不了。它的本钱来自节点容器模型——不用 shift、不用赋值,靠指针重连就行。两条路各取一头,谁也不白吃。

---

## 一个最小复刻:验证 EBO 与 pair 类型

```cpp
// Platform: host | C++ Standard: C++20
#include <iostream>
#include <type_traits>
#include <utility>

struct EmptyLess {
    using is_transparent = void;
    template <typename A, typename B>
    bool operator()(A&& a, B&& b) const { return a < b; }
};

struct WithNUA    { [[no_unique_address]] EmptyLess c; int i; };
struct WithoutNUA { EmptyLess c; int i; };

int main() {
    std::cout << "sizeof(WithNUA)    = " << sizeof(WithNUA)    << " (NUA 折叠空成员)\n";
    std::cout << "sizeof(WithoutNUA) = " << sizeof(WithoutNUA) << " (空成员占 1 + 填充)\n";
    std::cout << "pair<const int,int> 可 move-assign? "
              << std::is_move_assignable_v<std::pair<const int, int>> << " (false = 不可,所以 flat_map 不存它)\n";
    return 0;
}
```

实测输出(GCC 16 / Clang 22):`WithNUA=4`,`WithoutNUA=8`,`pair<const int,int> 可 move-assign? false`。两个数字 + 一个 false,flat_map 那两处设计的来由基本就落到地上了。

前置知识到这儿凑齐了(pre-00 导论 + pre-01..05 五块零件),下一站该进实战,把 flat_tree 的核心骨架搭起来。

## 参考资源

- [cppreference: [[no_unique_address]]](https://en.cppreference.com/w/cpp/language/attributes/no_unique_address)
- [cppreference: Empty Base Optimization](https://en.cppreference.com/w/cpp/language/ebo)
- [Chromium `base/containers/flat_tree.h` —— NO_UNIQUE_ADDRESS 与成员声明注释](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [crbug.com/1156268 —— GCC 成员声明顺序 ICE](https://crbug.com/1156268)
