---
title: optional 引用的浅层陷阱：const、value_or 与悬垂
description: CppCon 2025 笔记 —— optional<T&> 的浅层 const 三种位置、条件 explicit、value_or 为何总返回值、悬垂防御为何用 delete 而非 requires
chapter: 6
order: 4
conference: cppcon
conference_year: 2025
talk_title: 'The Evolution of std::optional: From Boost to C++26'
speaker: Steve Downey
cpp_standard: [17, 23, 26]
difficulty: intermediate
platform: host
reading_time_minutes: 11
tags:
  - cpp-modern
  - host
  - intermediate
  - optional
prerequisites:
  - "optional 引用是什么，以及赋值为什么一定是重绑定"
related:
  - "optional 引用是什么，以及赋值为什么一定是重绑定"
  - "optional 引用里藏着的移动语义陷阱"
---

# optional 引用的浅层陷阱：const、value_or 与悬垂

[上一篇](./03-optional-reference-and-assignment.md)咱们把 `optional<T&>` 的核心语义理顺了。这一篇换个角度，扒它在使用层面那些容易踩的角落：const 放哪儿、value_or 返回什么、从临时对象构造会怎样。每个坑单独看都不大，合在一起就是一份完整的避坑地图。

## const 是浅层的

这条笔者以前理解偏差最大。笔者以前以为 `const optional<T&>` 里包着的引用，解引用出来也该是 const 的，毕竟 C++ 里 const 不是会"传播"吗。完全不是这么回事，跑一下您就信了：

```cpp
// shallow_const.cpp
#include <iostream>
#include <optional>

int main() {
    int x = 42;

    // const 在 optional 上
    const std::optional<int&> opt = x;
    *opt = 100;                         // 编译通过！能改 x
    std::cout << "const optional<int&>: x=" << x << "\n";

    // const 在 T 上
    int y = 7;
    std::optional<const int&> opt2 = y;
    // *opt2 = 9;                       // 这行会编译错误
    std::cout << "optional<const int&>: y=" << y << "（不能通过 *opt2 改）\n";
}
```

```bash
$ g++ -std=c++26 shallow_const.cpp -o shallow_const && ./shallow_const
const optional<int&>: x=100
optional<const int&>: y=7（不能通过 *opt2 改）
```

`const optional<int&>` 里的 const 是浅层的，只约束 optional 对象本身（您不能 reset 它、不能重新赋值让它指向别的东西），完全不约束解引用出来的东西。`*opt = 100` 编译通过，x 真的被改成了 100。

回头看原理其实简单。`optional<T&>` 底层就一个指针，`const optional<T&>` 相当于 `T* const`，指针本身不能改，但通过指针去改它指向的东西天经地义。这跟 C++ 语言本身对 const 的定义是一致的，const 本来就是浅层的，只是咱们平时写 `const int&` 的时候 const 直接修饰了 int，所以看起来像"深层"。

### const 的三种位置

把三种写法摆一起，您就看出它和指针的规则一模一样：

```cpp
int value = 42;

std::optional<const int&> opt1 = value;     // 引用指向 const，不能通过它改 value
const std::optional<int&>   opt2 = value;   // optional 本身 const，不能 reset/重绑定，但能改 value
const std::optional<const int&> opt3 = value; // 两个都锁
```

这对应指针世界里 `const int*`（指向 const）和 `int* const`（指针本身 const）的区别。`optional<const int&>` 和 `const optional<int&>` 是两码事。笔者以前把 optional 引用当成"引用的包装"来理解，实际上把它当成"指针的包装"才对。

:::warning
这个坑在真实项目里能埋出 bug。你以为传了个 `const optional<T&>` 进去，接收方就不会改你原来的值了，结果人家 `*opt = ...` 直接改了。这种 bug 排查起来真让人怀疑人生。记住，`const optional<T&>` 防的是重新绑定，不防修改被引用对象。
:::

## 条件 explicit：到底要 explicit 到什么程度

这条偏库设计，但作为使用者也得知道它意味着什么。

笔者个人的习惯是构造函数能加 explicit 就加，防止隐式转换带来惊喜。但 optional 的历史包袱太重，`optional<T>` 从 `T` 的隐式构造已经存在很久，大量代码依赖，改不了。

那 `optional<T&>` 呢？从 `T&` 构造、从 `optional<U&>` 转换，到底 explicit 还是隐式？最终的设计决策是条件 explicit：根据底层 `T` 构造时的 explicit 性质来决定。`T` 能从 `U` 隐式构造，`optional<T&>` 就能从 `optional<U&>` 隐式构造；`T` 从 `U` 的构造是 explicit 的，optional 这边也 explicit。

这个策略听起来合理，实现代价却不小。Steve Downey 说他们在库设计工作中为此付出了很大代价，要确保各种转换落入正确的构造函数，而不是被其他重载截胡。对咱们使用者最直接的影响是：有些您以为能隐式转换的场景可能突然不行了，得显式写 `optional<T&>{...}`。遇到这种编译错误别懵，想想是不是底层类型的 explicit 性质在起作用。

## value_or 永远返回值

`value_or` 是 optional 最常用的方法之一，但它的返回类型在 `optional<T&>` 场景下真让人头疼。跑一下：

```cpp
// value_or_type.cpp
#include <iostream>
#include <optional>
#include <type_traits>

int main() {
    int x = 42;
    std::optional<int&> opt = x;
    auto r1 = opt.value_or(0);                  // 有值
    static_assert(std::is_same_v<decltype(r1), int>);

    std::optional<int&> empty;
    auto r2 = empty.value_or(7);                 // 空值
    static_assert(std::is_same_v<decltype(r2), int>);

    std::cout << "engaged value_or=" << r1 << " empty value_or=" << r2 << "（都是 int）\n";
}
```

```bash
$ g++ -std=c++26 value_or_type.cpp -o value_or_type && ./value_or_type
engaged value_or=42 empty value_or=7（都是 int）
```

optional 里面明明存的是引用，value_or 有值的时候为什么不把引用返回给咱们？因为这里有个根本性矛盾。optional 有值，您希望返回 `T&`；optional 没值，您希望返回默认值，而默认值是个临时对象，返回它的引用就是悬垂引用。这两个需求在同一个返回类型里没法统一。

现在的决策是 value_or 总返回值。最安全，虽然可能不是最方便，但至少不会产生悬垂引用。Steve Downey 的态度很明确：不能让所有人都满意，就做最安全的事情，以后再回来解决。

这个限制在真实场景里确实不方便。比如您想从 `optional<const Config&>` 和一个全局配置里二选一，返回一个引用，value_or 基本没法用，只能老老实实手写 if：

```cpp
const Config& get_config(std::optional<const Config&> override) {
    if (override) return *override;
    return global_config;
}
```

有几个提案（包括 Steve Downey 自己的）在尝试泛化 value_or，让它能返回 `T` 和 `U` 的公共引用类型。这个能力直到最近才能在语言层面表达，库技术还在建设中。目前咱们先用"总返回值"这个安全但有点笨的版本。

## 悬垂防御：delete 而不是 requires

这可能是整个设计里笔者最佩服的一个决策。您想想这个场景：从一个临时对象构造 `optional<T&>`，临时对象在表达式结束时销毁，optional 里却还存着指向它的引用，经典的悬垂。

```cpp
std::optional<int&> bad() {
    return std::optional<int&>(42);   // 从临时 int 构造，42 马上消亡
}
```

以前这种代码可能"碰巧能跑"，因为编译器不一定会立刻清理临时对象。上了生产开了优化，编译器激进回收临时对象，您就得到一个诡异的内存问题，复现都难。

设计原则是检查悬空属性。如果转换会产生一个临时对象，而这个临时对象会在表达式结束时消亡，就把这个重载直接 delete 掉，而不是用 requires 子句把它从重载集里排除。

这两者的区别非常关键。用 requires，编译器发现这个重载不满足约束，会继续去找其他重载，可能掉进一个您完全意想不到的构造函数里，报一堆看不懂的错。但用 `= delete`，编译器会直接告诉您：这个函数被删除了。错误提示来得更早、更清晰。

```cpp
// 设计思路（简化示意）
template<typename U>
    requires (std::is_lvalue_reference_v<U>)   // 只允许从左值构造
optional(optional<U&>);                         // requires：不满足会去找别的重载

// 对比
template<typename U>
optional(U&&) = delete;                         // delete：直接报错，不找别的
```

您写的东西是真的行不通，而不是被编译器硬塞进某个能跑通的路径。这一点对调试体验差别巨大。

特别值得一提的是 range-for 循环的修复。以前您写这种管道：

```cpp
for (auto& x : some_map | some_transform | another_transform) {
    // ...
}
```

如果管道中间产生了临时对象，而某个适配器返回了 `optional<T&>` 指向那个临时对象，临时对象可能在 for 循环的第一轮迭代之前就死了。C++23 修复了这个问题，在 range-for 循环中构造的临时对象现在能存活于整个 for 循环期间。这个修复虽然排除了少数本来安全的情况，但它禁止了多得多的危险情况，整体上是赚的。

## 接下来

浅层 const、条件 explicit、value_or、悬垂防御，这些是 `optional<T&>` 在"怎么用对它"层面的坑。下一篇咱们换个角度，看它跟移动语义交叉的地带。那是 C++ 里最容易出隐晦 bug 的地方，一个 `std::move` 放错位置，可能就"偷走了别人的猫"。
