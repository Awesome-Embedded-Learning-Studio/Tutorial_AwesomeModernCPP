---
title: 为什么 optional 引用折腾了二十年
description: CppCon 2025 笔记 —— Steve Downey 讲 std::optional<T&>(P2988) 为何从 2005 拖到 2025 年 Sofia 会议才进 C++26：引用的三重身份、assign-through 与 rebind 之争、最终落地为指针
chapter: 6
order: 1
conference: cppcon
conference_year: 2025
talk_title: 'The Evolution of std::optional: From Boost to C++26'
speaker: Steve Downey
cpp_standard: [17, 23, 26]
difficulty: intermediate
platform: host
reading_time_minutes: 10
tags:
  - cpp-modern
  - host
  - intermediate
  - optional
prerequisites:
  - "optional：把『可能没有』做成类型"
related:
  - "optional：把『可能没有』做成类型"
  - "std::optional 的值语义底子"
---

# 为什么 optional 引用折腾了二十年

:::tip
这一系列笔记基于 CppCon 2025 Steve Downey 的演讲 *The Evolution of std::optional: From Boost to C++26* 做的二次发散。讲者是 Bloomberg 的 Steve Downey，也是把 `std::optional<T&>` 推进 C++26 的那篇提案（P2988）的主要作者。原讲视频可以在 CppCon 官方频道检索，国内观众找搬运的 Bilibili 版本看。
:::

`std::optional<T&>`，一个看起来"不就是能装空值的引用嘛"的东西，从 2005 年第一次被提出，到 2025 年 6 月的 Sofia 会议才终于投票通过进入 C++26。二十年。C++11 到 C++23 都够走完一整轮了。

笔者 2022 年刚碰 C++ 的时候，天真地以为标准库里缺什么，就是委员会懒得加。后来真的挖进去才明白，有些东西没加，是因为它很难加对。这一篇咱们顺着 Steve Downey 的讲法，把"一个能装空值的引用为什么这么难"这件事拆开。

## 先把环境交代清楚

后面所有能跑的代码，笔者都用同一套环境验证过：Arch Linux WSL，GCC 16.1.1。

```bash
$ g++ --version
g++ (GCC) 16.1.1 20260625
```

这里有个前提您得先记住：`optional<T&>` 是 C++26 才进标准的特性，提案号 P2988。GCC 16.1.1 在 `-std=c++26` 下已经实现了它，所以咱们能真的把代码跑起来，不是纸上谈兵。换成 `-std=c++23` 或更早，下面这行直接编不过：

```cpp
#include <optional>
int main() {
    int x = 42;
    std::optional<int&> opt = x;   // P2988, C++26 才支持
    *opt = 100;
}
```

笔者实跑了一下，`-std=c++23` 报错，报的是 `optional` 内部 `union` 存不下引用类型的那串模板实例化错误；切到 `-std=c++26`，编过，输出 `x=100`。这条分界线本身就是"C++26 才支持引用 optional"的活证据，后面咱们会反复用到。

## 引用在 C++ 里其实干了三件事

很多人对引用的理解停在"别名，给变量起个外号"。Steve Downey 把引用的职责拆成了三件，笔者觉得这个拆法清楚得很。

第一件是调用约定。写 `void foo(const std::string& s)`，这里的引用在说"别拷贝了，直接操作原来的对象"。这对运算符重载尤其要紧，总不能让 `operator+` 每次都把操作数整个拷一遍。

第二件是给一个复杂的表达式起局部别名。`obj.get_container()[index].get_sub().value()` 这种长链，写 `auto& x = obj.get_container()...`，编译器就记下"你给这玩意儿起了个名字"。它不占空间，纯粹是个别名关系。这两件事里，引用"不能重新绑定"是个好特性，你既然给它起了名字，它就一直指向那个东西。

第三件就出问题了。你可以把引用塞进 `struct` 里。

这一塞，性质就变了。引用作为成员开始占空间，通常是一个指针的大小，但它又不能重新绑定，于是编译器根本不知道"拷贝这个结构体"该怎么定义。是拷贝引用本身？做不到，引用不能重新绑定。是拷贝它引用的那个对象？那又不是 `struct` 该干的事。

咱们跑个最小的例子，把这件事看清楚：

```cpp
// ref_in_struct.cpp
#include <cstdio>
#include <type_traits>

struct HoldsRef   { int& ref; };   // 引用成员
struct HoldsValue { int val; };    // 普通值成员

int main() {
    std::printf("HoldsValue default_ctor=%d copy_assign=%d\n",
        std::is_default_constructible_v<HoldsValue>,
        std::is_copy_assignable_v<HoldsValue>);
    std::printf("HoldsRef   default_ctor=%d copy_assign=%d\n",
        std::is_default_constructible_v<HoldsRef>,
        std::is_copy_assignable_v<HoldsRef>);
}
```

编译运行，`-std=c++20` 就够：

```bash
$ g++ -std=c++20 ref_in_struct.cpp -o ref_in_struct && ./ref_in_struct
HoldsValue default_ctor=1 copy_assign=1
HoldsRef   default_ctor=0 copy_assign=0
```

全零。就因为多了一个引用成员，这个结构体的默认构造和拷贝赋值全没了。您想想，如果 `optional<T&>` 内部真的放一个引用成员，那它连默认构造都做不到，更别提赋值语义的混乱。所以"内部用指针"这个决定，不是偷懒，是不得不这么做。

## assign-through 还是 rebind

好，假设咱们真要造一个 `std::optional<T&>`，它内部有个引用成员。现在你给它赋值，到底该发生什么？

这里有两个选项，Steve Downey 把它们叫 **assign-through（穿透赋值）** 和 **rebind（重新绑定）**。

assign-through 的意思是赋值"穿透"optional，直接改被引用的对象。你的 `optional<int&>` 当前引用着变量 `x`，你给它赋一个 `y`，结果 `x` 的值变成 `y`，optional 本身还引用着 `x`。

rebind 则相反，赋值之后 optional 不再引用 `x`，转而引用 `y`。

如果您把 optional 当成"一个内部持有引用的 `struct`"，那按 `struct` 的规矩，assign-through 才说得通，毕竟你不能重新绑定一个引用成员。确实有一派人这么主张，动机完全站得住脚。

:::warning
坑在这里：一旦 optional 当前是空的（disengaged），assign-through 就讲不通了。它没有底层对象可以"穿透"，那空状态下的赋值难道偷偷切换成 rebind？这样一来，同一个赋值运算符的行为就依赖于 optional 当前的运行时状态。
:::

这就是整个争论的死结。Steve Downey 转述了另一位委员 JeanHeyd 的关键观察：**如果赋值行为依赖于 optional 当前的状态，这个类型就没办法被静态推导**。

什么叫没办法推导？你看到一行 `opt = value`，光看这行代码本身，你根本不知道它干了什么。你必须知道 `opt` 在运行时有没有值，才能确定这行是穿透还是重绑定。而 C++ 的整个类型系统、概念约束、模板元编程，都建立在"知道类型就知道行为"这个前提上。一旦行为依赖运行时状态，所有静态推理一起失效。

之前所有尝试走 assign-through 这条路的实现，最后都踩进了这个坑里。腾讯云上有一篇 Sofia 会议的快报写得很直白：C++17 周期标准化 `std::optional` 的时候，就为"要不要支持引用 optional"爆发过激烈争吵，争吵点正是"能不能用 `T*` 代替"和"`operator=` 该 assign-through 还是 rebind"，吵到有人负气去了 C 标准委员会（WG14）。这个特性就这么被搁置了下来。

## 最终：别绕了，它就是个指针

吵了二十年的结论其实很简单。`optional<T&>` 内部，就存一个指针。

不是引用，是指针。然后在这个指针上施加一堆约束，让它表现得像一个"可选的引用"。这样一来赋值就清晰了：不管 optional 当前有没有值，赋值都是重新绑定这个指针。行为不依赖状态，推导问题彻底消失。

笔者第一次听到这个结论的反应是"就这？折腾二十年得出一个'用指针'？"。但冷静下来想，这个"用指针"不是随便用的。它背后有一整套语义要定义清楚，还要跟值版本的 `optional<T>` 保持某种一致性，这些细节才是真正花时间的地方，也是后面几篇要讲的东西。

## P2988 这二十年

把时间线捋一下，您就明白这二十年花在哪了。

2005 年，optional 第一次被提出，最初的草案其实是带引用语义的。但到了 2017 年 `std::optional` 正式进 C++17 的时候，只进了值版本，引用版本因为上面那场 assign-through/rebind 的争吵被拿掉了。中间有个提案在 C++20 周期走得相当远，最后还是没被采纳。

转机出在 JeanHeyd 身上。他因为一直没能把 optional 引用推进标准，干脆做了次彻底的考古式梳理，把历史上到底讨论过什么、各方理由是什么都翻了出来。这份梳理直接催生了 Steve Downey 的 P2988，主张"别再纠结了，直接做"。当然，真做起来细节比想象的多得多。最终在 2025 年 6 月的 Sofia 会议上，P2988 投票通过，进入 C++26。

所以当您在 GCC 16.1.1 上用 `-std=c++26` 写出 `std::optional<int&>` 并且它真的编过的时候，背后是二十年的来回拉锯。

## 接下来

这一篇咱们搞清楚了 optional 引用为什么难，以及它最终落地为"一个带约束的指针"。但"内部用指针"只是起点，围绕这个指针还有一堆问题：它跟裸指针有什么区别？它跟 `optional<T*>` 有什么区别？赋值、`operator*`、`value()` 这些操作到底返回什么？

在那之前，笔者觉得有必要先把普通 `optional<T>` 的底子摸透说透。因为一旦 `T` 变成引用，"拥有所有权""值语义"这些前提全都不成立了，那会是一个完全不同的故事。[下一篇](./02-value-semantics-of-optional.md)咱们就从值版本的 `optional<T>` 讲起。
