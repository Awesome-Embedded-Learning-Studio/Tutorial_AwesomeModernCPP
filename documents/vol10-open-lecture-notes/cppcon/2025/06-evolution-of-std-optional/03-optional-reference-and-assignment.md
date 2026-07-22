---
title: optional 引用是什么，以及赋值为什么一定是重绑定
description: CppCon 2025 笔记 —— optional<T&> 的非拥有定位、map 查找痛点、赋值为何永远是重新绑定、vector<bool> 幽灵，以及 make_optional 与 CTAD 在引用上的真实行为
chapter: 6
order: 3
conference: cppcon
conference_year: 2025
talk_title: 'The Evolution of std::optional: From Boost to C++26'
speaker: Steve Downey
cpp_standard: [17, 23, 26]
difficulty: intermediate
platform: host
reading_time_minutes: 16
tags:
  - cpp-modern
  - host
  - intermediate
  - optional
prerequisites:
  - "std::optional 的值语义底子"
related:
  - "std::optional 的值语义底子"
  - "optional 引用的浅层陷阱"
---

# optional 引用是什么，以及赋值为什么一定是重绑定

[上一篇](./02-value-semantics-of-optional.md)把值版本的底子摸透了。这一篇正式进 `optional<T&>` 的核心。一旦 `T` 变成引用，"拥有所有权""值语义"这些前提全部不成立，咱们得换一套思路来理解它。

## 它不是"装着引用的盒子"

很多人，包括以前的笔者，以为 optional 引用大概就是个不能为 null 的指针加个 `has_value` 判断。这个理解太浅。

`optional<T&>` 的核心定位是非拥有类型。注意"非拥有"这三个字，optional 内部不持有任何实际对象，它只是指向别处已经存在的某个东西。这听起来像指针，但关键在于它同时兼具引用语义和值语义。指针本身是很好的值，您可以拷贝它、比较它，它有自己的标识（地址），同时指针又能被解引用来操作指向的东西，这是引用语义的一面。`optional<T&>` 要的就是这种双重性格。

它还天然带一个空状态，而且这个空状态不需要额外空间来存标志位。底层就是用空指针实现的，零开销。笔者以前一直以为 optional 存引用要多花一个 bool 的空间，实际根本不需要，空指针本身就是最好的"无值"表示。

## map 查找：最能说明问题的痛点

理论说再多不如看实际痛点。您写 C++ 肯定遇过这个场景：从 map 里查找一个东西，找到了要修改它。

```cpp
std::map<std::string, int> enemy_hp{{"goblin", 30}, {"dragon", 500}};

auto it = enemy_hp.find("dragon");
if (it != enemy_hp.end()) {
    it->second -= 50;   // 想改 value，得写 .second
}
```

这种代码笔者写了几百遍，每次都觉得 `.second` 是多余的噪音。map 迭代器解引用出来是 `pair<const Key, Value>&`，你只想要 value，却被迫和 key 绑在一起处理。

等 `optional<T&>` 进了标准，查找函数可以直接返回 `optional<int&>`，找到就改，没找到就是空，没有 `.second`。现在标准库还没给 map 加这个接口（那是 P3091 的事，稍后讲），但咱们能自己用 `reference_wrapper` 包一层模拟，先体会一下效果：

```cpp
// refwrap_lookup.cpp
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>

template<typename Map>
auto try_get(Map& m, const typename Map::key_type& k)
    -> std::optional<std::reference_wrapper<typename Map::mapped_type>>
{
    auto it = m.find(k);
    if (it != m.end()) return std::ref(it->second);
    return std::nullopt;
}

int main() {
    std::unordered_map<std::string, int> scores{{"Alice", 95}, {"Bob", 87}};

    if (auto r = try_get(scores, "Alice")) {
        r->get() += 5;                       // 拿到引用，改的是 map 里的值
        std::cout << "Alice=" << scores["Alice"] << "\n";
    }
    if (auto r = try_get(scores, "Charlie")) {
        std::cout << "Charlie=" << r->get() << "\n";
    } else {
        std::cout << "Charlie not found, no exception\n";
    }
}
```

```bash
$ g++ -std=c++17 refwrap_lookup.cpp -o refwrap_lookup && ./refwrap_lookup
Alice=100
Charlie not found, no exception
```

Alice 的分数从 95 改成了 100，Charlie 没找到但没抛异常。语义上这就是"一个可能不存在的引用"，只是 `reference_wrapper` 用起来别扭，拿到手还得 `.get()` 一次。等 C++26 的 `optional<T&>`，直接写成返回 `optional<int&>`，干净多了。这个给关联容器加返回 optional 引用查找接口的提案是 P3091（Pablo Halpern 提出），它没赶上 C++26 的进度，推到了 C++29。原因听起来有点好笑：同时推进 C++26 和 C++29 两套标准，会让处理标准文档的人太困惑。C++ 标准本身是个三千多页的 LaTeX 文档，技术上能用 git 分支管，但大家真不想这么干。

## 当函数参数：一份隐式契约

`optional<T&>` 做函数参数也有意思。笔者以前觉得传指针和传引用在表达意图上差不多，仔细想想指针的语义其实太模糊。

```cpp
void process(Logger* logger);                     // 这个函数会不会 delete 它？会不会存下来以后用？调用者不知道
void process(std::optional<Logger&> logger);      // 意图清晰得多
```

把 logger 描述成 `optional<Logger&>`，等于告诉接收它的函数：我不会拥有它（不会 delete），也不会在函数返回后还拿着它的引用，你只要保证调用期间它活着就行。这虽然不是形式化的契约，但在 C++ 能做到的范围内，已经算清晰的意图表达了。optional 参数还天然支持"不传"，不传 logger，函数里 `if (logger)` 判断一下跳过日志逻辑就行。Steve Downey 说这算个极简的依赖注入框架，笔者觉得这个说法挺准。

## 赋值：重新绑定，不是值拷贝

到这里都是好消息。接下来是 `optional<T&>` 最容易让人栽跟头的地方：赋值到底干了什么。先把场景摆出来：

```cpp
struct Cat { std::string name; Cat(std::string n) : name(std::move(n)) {} };

Cat finn{"Finn"};
Cat loki{"Loki"};

std::optional<Cat&> a;            // 空
std::optional<Cat&> b = loki;     // 已经绑定 loki

a = finn;     // ?
b = finn;     // ?  b 已经绑着 loki，这是改 loki 的名字，还是让 b 改绑 finn？
```

您可能觉得赋值嘛有什么好问。但 `b` 已经绑定到 loki 了，现在把 finn 赋给它，到底是把 loki 的名字改成 "Finn"，还是让 b 改去引用 finn？按 `optional<T>` 的赋值是值拷贝来类推，应该是前者。但这个理解是错的。跑一下：

```cpp
// rebind.cpp
#include <iostream>
#include <optional>
#include <string>
#include <utility>

struct Cat { std::string name; Cat(std::string n) : name(std::move(n)) {} };

int main() {
    Cat finn{"Finn"}, loki{"Loki"};
    std::optional<Cat&> a;            // 空
    std::optional<Cat&> b = loki;     // 绑定 loki

    a = finn;     // 重绑定 a -> finn
    b = finn;     // 重绑定 b -> finn（不是改 loki）

    std::cout << "a has_value=" << a.has_value() << " a->name=" << a->name << "\n";
    std::cout << "b->name=" << b->name << " loki.name=" << loki.name << "\n";

    int p = 1, q = 2;
    std::optional<int&> oa = p, ob = q;
    std::swap(oa, ob);
    std::cout << "swap 后 *oa=" << *oa << " *ob=" << *ob
              << " (p=" << p << " q=" << q << " 不变)\n";
}
```

```bash
$ g++ -std=c++26 rebind.cpp -o rebind && ./rebind
a has_value=1 a->name=Finn
b->name=Finn loki.name=Loki
swap 后 *oa=2 *ob=1 (p=1 q=2 不变)
```

读输出。给空的 a 赋 finn，a 重绑定到 finn；给已经绑着 loki 的 b 赋 finn，b 也重绑定到 finn，而 loki 的名字还是 Loki，没被动。赋值改的是 optional 引用谁，不是被引用对象的内容。swap 同理，交换的是两个指针，oa 和 ob 互换了绑定目标，p 和 q 自己的值没变。

这跟指针的赋值是一回事。你给一个指针赋值，改的是指针的指向，不是指向对象的内容。`optional<T&>` 内部就是个指针，所以赋值就是重绑定。

### 为什么不"有值就拷、没值就绑"

笔者一开始想过一个看起来更聪明的方案：optional 已经 engaged 就做值拷贝，disengaged 就做绑定。仔细一想这是噩梦。因为这样一来，同一个赋值运算符的行为就取决于 optional 当前的运行时状态。您读代码看到 `opt = value`，根本不知道它干了什么，得往回追溯 opt 此刻有没有值。这完全破坏了可推导性。

[上一篇](./01-why-optional-reference-took-20-years.md)讲过 JeanHeyd 的那个关键观察：赋值行为一旦依赖状态，类型就没法静态推导。所有走过这条路的实现最后都踩进了坑里。始终重绑定这条规则，不管 optional 之前什么状态，赋值之后就绑定到您给的那个东西上。简单、一致、可预测。

### vector\<bool> 的幽灵

但这里有个严肃的反对意见，笔者也纠结过。`optional<int>` 赋值是值拷贝，`optional<int&>` 赋值是重绑定。同一个模板，不同特化，行为不一致。这不是另一个 `vector<bool>` 吗？

`vector<bool>` 是 C++ 标准库里最臭名昭著的设计之一。它做了特化，使得 `vector<bool>` 和 `vector<其他任何类型>` 行为神奇地不同，不存真正的 bool 而做位压缩，导致你没法取单个元素的地址，迭代器类型也不一样。而且进了标准就删不掉，只能一直背着这个历史包袱。

但后来笔者想通了。C++ 的引用从第一天起就不是泛型的。引用不是对象，它没有自己的地址，不能有"引用的引用"，不能有"引用的数组"。引用从一开始就是值语义世界里的特殊存在。你硬要把引用塞进一个为值语义设计的模板里，还指望它行为完全一致，本身就不现实。

咱们不是在"往 optional 里放一个 T&"，咱们想要的是"引用语义的 optional"，只是 C++ 的引用语义必须用不同的方式来实现。这不是在制造不一致，是 C++ 引用本身的特性决定的。

## make_optional 和 CTAD 在引用上挖的坑

赋值语义搞清楚了，还有两个特别容易踩的坑：`make_optional` 和 CTAD。先说结论：`std::make_optional` 永远返回 `optional<T>`，即使您传进去的是一个引用。

```cpp
// ctad_truth.cpp
#include <iostream>
#include <optional>
#include <type_traits>

int main() {
    int x = 42;
    auto o1 = std::make_optional(x);     // 一定 optional<int>
    std::optional<int&> o2 = x;           // 显式写才是 optional<int&>
    std::optional o3{x};                  // CTAD 到底推成什么？

    x = 99;
    std::cout << "make_optional 随 x 变？ " << (*o1 == 99) << " (0=拷贝)\n";
    std::cout << "optional<int&> 随 x 变？ " << (*o2 == 99) << " (1=引用)\n";

    if constexpr (std::is_same_v<decltype(o3), std::optional<int&>>)
        std::cout << "CTAD o3 -> optional<int&>\n";
    else if constexpr (std::is_same_v<decltype(o3), std::optional<int>>)
        std::cout << "CTAD o3 -> optional<int>（退化为值，不是引用）\n";
}
```

```bash
$ g++ -std=c++26 ctad_truth.cpp -o ctad_truth && ./ctad_truth
make_optional 随 x 变？ 0 (0=拷贝)
optional<int&> 随 x 变？ 1 (1=引用)
CTAD o3 -> optional<int>（退化为值，不是引用）
```

三个事实一次跑明白。`make_optional(x)` 得到 `optional<int>`，x 改成 99 之后里面还是 42 那份拷贝。显式写 `std::optional<int&>` 才是引用语义，跟着 x 变。最值得注意的是第三行：CTAD `std::optional o3{x}` 推导出来的是 `optional<int>`，不是 `optional<int&>`。

:::warning
网上一些早期材料说 `std::optional o{x}` 的 CTAD 能推导出引用版本，那是旧提案的设想。笔者实测下来，最终落地的 P2988 没这么干，CTAD 仍然退化为值。想要引用语义，老老实实写全 `std::optional<int&> o{x}`。别拿 CTAD 碰运气，碰出来的是拷贝。
:::

`make_optional` 退化成值这件事不能改。太多现有代码依赖 `make_optional` 总是返回 `optional<T>` 这个事实，改了就是破坏性变更。从直觉上说，`make_optional` 就像在"制造一个 optional 值"，您不会期望它给您一个引用语义的东西。这一点它跟函数返回值的行为一致，您从函数里返回一个 `T&`，用 `auto` 接，得到的是 `T` 不是 `T&`。

## 接下来

`optional<T&>` 的核心咱们摸清了一半：非拥有、赋值重绑定、make_optional 和 CTAD 不给引用。但围绕这个"内部指针"还有一堆角落：const 放在哪儿语义完全不同，`value_or` 永远返回值，从临时对象构造会被直接 delete 掉。[下一篇](./04-shallow-traps-const-value-or-dangling.md)咱们把这些浅层陷阱一个个扒开。
