---
title: optional 引用里藏着的移动语义陷阱
description: CppCon 2025 笔记 —— optional<T&> 与移动语义交叉处的"猫被偷走"bug：operator* 对右值 optional 的返回类型、*std::move(opt) 为何危险、std::move 能不写就不写
chapter: 6
order: 5
conference: cppcon
conference_year: 2025
talk_title: 'The Evolution of std::optional: From Boost to C++26'
speaker: Steve Downey
cpp_standard: [17, 23, 26]
difficulty: intermediate
platform: host
reading_time_minutes: 9
tags:
  - cpp-modern
  - host
  - intermediate
  - optional
prerequisites:
  - "optional 引用的浅层陷阱：const、value_or 与悬垂"
related:
  - "optional 引用的浅层陷阱：const、value_or 与悬垂"
  - "标准化真相：The Beman Project 与一份能跑的参考实现"
---

# optional 引用里藏着的移动语义陷阱

[上一篇](./04-shallow-traps-const-value-or-dangling.md)讲的是 `optional<T&>` 使用层面的坑。这一篇换个维度，看它跟移动语义交叉的地带。这是 C++ 里最容易出隐晦 bug 的地方，一个 `std::move` 放错位置，可能就"偷走了别人的猫"。

## 先承认一种最危险的"能跑通"

说实话，笔者以前有种很糟糕的编码习惯：只要代码跑出预期结果，就觉得大功告成，直接提交。后来在折腾一个安装脚本时栽过大跟头。那脚本看起来跑通了，输出全对，笔者觉得搞定就扔一边。过几天换了个环境跑，直接炸了。后来有大佬帮着看，才发现那脚本本来就不该跑通，它只是碰巧跑通。

这是最糟糕的一种"能跑通"，因为它给您虚假的安全感，让您以为这部分已经理解透了，实际上底层逻辑全歪。

这种"碰巧能跑通"的坑，在 C++ 的模板和移动语义里太多了。这一篇讲的，就是 optional 引用和移动语义交叉处一个能产生千分之一概率诡异崩溃的 bug。

## "猫被偷走"是怎么回事

Steve Downey 举了个特别生动的例子。假设有只猫叫 Finn，笔者创建了一个指向 Finn 的引用，包装进 optional。这时候如果把那个 optional 引用移动赋值给另一个 optional，在某些实现里会发生一件离谱的事：Boost.Optional 会把那只猫本身"偷走"，而不是简单拷贝引用。

您可能懵了，引用怎么能被"偷走"？引用又不拥有对象。问题出在 `operator*` 的返回值类别和移动语义的交互上。

咱们先看一个关键事实：`operator*` 对右值 optional 的返回类型，在 `T` 和 `T&` 两种特化下是不一样的。跑一下：

```cpp
// move_category.cpp
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

struct Cat {
    std::string name;
    Cat(std::string n) : name(std::move(n)) {}
};

int main() {
    std::optional<Cat> ov = Cat{"Finn"};
    using Rval = decltype(*std::move(ov));          // 值版本，右值 optional
    std::cout << "*std::move(optional<Cat>)  是 Cat&& ? "
              << std::is_same_v<Rval, Cat&&> << "\n";

    Cat c{"Loki"};
    std::optional<Cat&> or_ = c;
    using Rref = decltype(*std::move(or_));          // 引用版本，右值 optional
    std::cout << "*std::move(optional<Cat&>) 是 Cat&  ? "
              << std::is_same_v<Rref, Cat&> << "\n";
}
```

```bash
$ g++ -std=c++26 move_category.cpp -o move_category && ./move_category
*std::move(optional<Cat>)  是 Cat&& ? 1
*std::move(optional<Cat&>) 是 Cat&  ? 1
```

读这两行输出。对一个右值的 `optional<Cat>`（值版本），`*std::move(opt)` 的类型是 `Cat&&`，因为 optional 马上要销毁，里面的 Cat 也可以被移动出来，这是合理的优化。但对一个右值的 `optional<Cat&>`（引用版本），`*std::move(opt)` 的类型还是 `Cat&`，没变成 `Cat&&`。

## 底层到底在发生什么

这个区别就是"猫被偷走"的核心。

对一个右值的 `optional<T>`，`operator*` 返回 `T&&` 是对的。optional 即将销毁，里面的 `T` 也能被移动，您写 `some_type dest = *std::move(opt)` 是把 `T` 移动出来，没问题。

但对 `optional<T&>`，optional 即将销毁，不代表它引用的那个对象即将销毁。Finn 还活得好好的，它只是被一个即将销毁的 optional 引用着。如果实现里 `operator*` 对右值 `optional<T&>` 也返回 `T&&`（这正是 Boost.Optional 当年的 bug），您写 `*std::move(opt)` 就是在移动 Finn 本身。猫被偷走了。

P2988 修复了这一点：`optional<T&>` 的 `operator*` 永远返回 `T&`，不管 optional 是不是右值。因为咱们在模拟引用语义，而引用的值类别跟"持有这个引用的容器"的值类别无关，一个引用绑定到哪个对象，通过它访问到的值类别就取决于那个对象本身。上面实测的 GCC 16.1.1 实现是对的，`*std::move(optional<Cat&>)` 是 `Cat&` 不是 `Cat&&`。

## 笔者给自己定的一条规矩

看到这里，笔者给自己定了一条规矩，也分享给您。

不要试图去推断您能对什么进行移动。

什么意思？别想"我知道这个函数返回什么，我可以把 `std::move` 包在外面，没问题"。您今天可能是对的，明天别人改了那个函数的返回类型，或者模板实例化成另一个特化，您的 `std::move` 就可能从无害变成偷走别人的猫。

正确的做法是：只对您确定有权移动的对象执行移动，并且把 `std::move` 写在那个对象本身上，而不是写在包含它的容器外面。

```cpp
// 危险写法：您不知道 *rhs 解引用出来到底该不该被移动
some_type dest = *std::move(rhs);

// 安全写法：先解引用拿到引用，再 move 这个引用
some_type dest = std::move(*rhs);
```

这两行区别微妙，语义完全不同。`*std::move(rhs)` 是先把 optional 变成右值再解引用，解引用的结果类型取决于 optional 的 `operator*` 对右值怎么定义，这正是前面说的不可预测的部分。`std::move(*rhs)` 是先解引用拿到引用，再把这个引用变成右值，语义清晰：我要移动的就是那个被引用的对象。

更进一步，您能写出的最好的 `std::move`，是那些根本不需要写的 `std::move`。比如返回局部变量：

```cpp
Cat make_cat() {
    Cat c{"Finn"};
    return c;                       // 正确，NRVO 或隐式移动
    // return std::move(c);         // 多余，甚至可能阻止 NRVO
}
```

编译器自己知道 `c` 是局部的、马上要销毁的，它会自动处理。您手写 `std::move` 反而可能把 NRVO 搞没，因为 `std::move(c)` 返回的是右值引用，而 NRVO 要求返回的是命名的局部变量本身。写 `std::move` 本质上是在向编译器解释一些事情，而这总是有风险的，因为您不一定比编译器聪明。状态好的时候您可能是，到了周五下午笔者就不是了。

## optional 引用到底要解决什么问题

聊了半天 bug，退一步看，`optional<T&>` 到底用来干什么？

最典型的用例是：查找某个东西，而"没找到"不算异常。

笔者以前写代码，从 map 里查东西，找不到就抛异常或者返回 end 迭代器让调用方处理。但说实话，很多时候"没找到"是个再正常不过的结果，根本不值得用异常来表达。异常的开销大，语义也不对，"key 不存在"不是程序出错，它就是一个可能的查询结果。

有了 `optional<T&>`，咱们就能给 map 写一个返回 optional 引用的 getter，找到就拿到引用直接改，没找到就是空。现在标准库还没把这个接口直接做进关联容器（那是 P3091 的事），您可以先用 [上一篇](./03-optional-reference-and-assignment.md)里的 `reference_wrapper` 包一层过渡。

## 接下来

移动语义和引用的交叉地带，核心就一条：optional 即将销毁，不代表被引用对象即将销毁；所以别用 `*std::move(opt)`，用 `std::move(*opt)`，最好的 move 是不写 move。下一篇咱们跳出 optional 本身的细节，看这场演讲里最让笔者兴奋的部分：标准化到底是怎么进行的，以及 The Beman Project 在里面扮演的角色。
