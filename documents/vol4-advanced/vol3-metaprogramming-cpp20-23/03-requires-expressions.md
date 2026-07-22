---
chapter: 13
cpp_standard:
- 20
description: requires 这个词在 C++20 里既是子句又是表达式,最容易混。把 requires 表达式拆透:四种成分(简单/类型/复合/嵌套)、怎么用它定义 concept,以及「不求值」和「对具体类型硬错误」这两个坑
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 使用 Concepts 约束模板:subsumption 与重载
- Concepts:把模板约束写进签名
reading_time_minutes: 13
related:
- Concepts:把模板约束写进签名
- 使用 Concepts 约束模板:subsumption 与重载
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- concepts
- 类型安全
title: Requires 表达式深度解析:四种成分
---
# Requires 表达式深度解析:四种成分

前两篇里 `requires` 这个词反复出现,有时候是子句,有时候是表达式,长得一样干的却不是一回事。这一篇把它彻底拆开:什么是 requires 表达式、它有哪几种成分、怎么用它当场描述「类型要提供哪些操作」,以及两个最容易让人困惑的坑。读完这一篇,上面所有 `requires(T t){ ... }` 的写法您都能看明白来历。

## 两种 requires:子句与表达式

先把同名的两种东西放一张表对照,这是后面所有内容的基础。

| | requires 子句(requires-clause) | requires 表达式(requires-expression) |
|---|---|---|
| **是什么** | 给模板加约束的语法位置 | 一个能在编译期算出 bool 的表达式 |
| **长什么样** | `requires Numeric<T>` | `requires(T t) { t.size(); }` |
| **值** | 不是值,是约束声明 | 一个 bool(true / false) |
| **出现位置** | 模板参数列表之后 | 几乎任何需要 bool 的地方:子句里、concept 定义里、`static_assert` 里 |

上一篇讲的 subsumption、重载分派,主角是**子句**。这一篇的主角是**表达式**。两者常配合使用:子句里塞一个表达式,就是 `requires requires(T t){ t+t; }` 那种连写两个 `requires` 的样子——外层是子句,内层是表达式。

## requires 表达式的四种成分

一个 requires 表达式 `requires(参数) { ... }` 的大括号里,可以写四种不同的「要求」。咱们定义一个 `Container` 概念,把四种成分一次用全:

```cpp
#include <concepts>
#include <vector>

template <typename T>
concept Container = requires(T t) {
    // ① 简单要求(simple requirement):表达式必须合法,能编译通过即可
    t.begin();
    t.end();

    // ② 类型要求(type requirement):必须有这个内嵌类型
    typename T::value_type;

    // ③ 复合要求(compound requirement):表达式合法,且返回值满足某个约束
    { t.size() } -> std::convertible_to<std::size_t>;

    // ④ 嵌套要求(nested requirement):再嵌一条编译期 bool 判断
    requires std::integral<typename T::value_type>;
};

static_assert(Container<std::vector<int>>);   // vector<int> 四条全满足
static_assert(!Container<int>);               // int 没有 begin/end,第一条就挂
```

这两个 `static_assert` 是编译期断言,代码能编过就说明 `vector<int>` 满足 `Container`、`int` 不满足,不需要运行。

四种成分各有用处。简单要求最常用,`t.begin();` 只是在问「`T` 类型的对象能不能调 `begin()`」,能编译就算过。类型要求 `typename T::value_type;` 检查内嵌类型存不存在,容器、迭代器的 trait 检查里频繁出现。复合要求 `{ t.size() } -> std::convertible_to<std::size_t>;` 把「表达式合法」和「返回类型满足约束」绑在一起,比先检查能不能调、再用 `decltype` 查返回类型分两步写更紧凑,复合要求还能加 `noexcept`:`{ t.size() } noexcept -> std::convertible_to<std::size_t>;`,要求这个调用还不抛异常。嵌套要求 `requires std::integral<...>;` 让您在表达式内部再塞一条 concept 判断,适合「主要求满足后,还要额外满足某条」的场景。

顺带提一个容易看走眼的点:`Container` 的第④条要求 `value_type` 是整数类型,而 `std::integral<char>` 是 **true**(char 属于整数类型族,和上一篇 `integral<bool>` 是 true 一个道理)。所以 `Container<std::string>` 实际上是满足的——string 的 `value_type` 是 char,过得了第④条。您要是只想收 `value_type` 恰好是 `int` 的容器,得把第④条换成 `std::same_as<typename T::value_type, int>`。

## requires 表达式是个 bool:不一定非要起 concept 名

requires 表达式算出来是个 bool,所以它不只能住在 concept 定义里,任何要编译期判断的地方都能直接用。

```cpp
#include <string>

// 直接塞进 static_assert,不用先定义 concept
static_assert(requires(std::string s) { s.size(); });   // string 有 size()

// 直接用在 if constexpr 里做编译期分支
template <typename T>
void process(T t) {
    if constexpr (requires(T x) { x.empty(); }) {
        std::cout << "has empty()\n";
    } else {
        std::cout << "no empty()\n";
    }
}
```

```bash
$ g++ -std=c++20 -Wall -Wextra four_requirements2.cpp -o fr2 && ./fr2
has empty()      // std::vector<int> 有 empty()
no empty()       // int 没有
```

临时检查一下「这个类型有没有某个操作」,用内联 requires 表达式最省事。但要注意一个取舍:内联表达式没有名字,它不形成可复用的原子约束。上一篇讲 subsumption 时说过,重载分派靠命名的 concept 建立蕴含关系。如果您想让两个重载靠约束分派,得用命名的 concept(`concept C = requires(...){...}`),内联表达式做不到 subsumption。所以「只在某一处用一次」的检查用内联表达式,「要参与重载或反复复用」的要求提成 concept。

## 坑一:requires 表达式不求值

这是最反直觉的一个坑。requires 表达式里写的那些调用,只是**检查能不能编译**,根本不会真正执行。咱们直接跑一个看证据。

```cpp
#include <iostream>

int counter = 0;
int increment() {
    ++counter;
    std::cout << "[副作用] increment 被调用了\n";
    return 1;
}

template <typename T>
concept MentionsIncrement = requires(T t) {
    increment();   // 只检查「这个调用合不合法」,不求值、不执行
};

int main() {
    static_assert(MentionsIncrement<int>);   // 满足:increment() 调用合法
    std::cout << "concept 求值完毕,counter = " << counter << "\n";
    increment();                              // 这里才真正调用
    std::cout << "真正调用后,counter = " << counter << "\n";
}
```

```bash
$ g++ -std=c++20 -Wall -Wextra unevaluated.cpp -o ue && ./ue
concept 求值完毕,counter = 0
[副作用] increment 被调用了
真正调用后,counter = 1
```

看清楚 `counter = 0` 那行。`MentionsIncrement<int>` 求值时,requires 表达式里的 `increment()` 调用**完全没有执行**——counter 还是 0,副作用那行也没打印。直到 `main` 里真正写了一句 `increment()`,counter 才变成 1。

requires 表达式和 `decltype`、`sizeof` 一样属于**不求值上下文(unevaluated context)**。编译器只关心里面的表达式「类型上合不合法」,不会生成调用代码,更不会触发任何副作用。这一点初学者特别容易栽跟头:在 requires 表达式里写了个「看起来在初始化」「看起来在计算」的式子,以为它跑了,其实什么都没发生。判断类型能力用 requires 表达式,真正要让代码跑起来还得在普通函数体里写。

## 坑二:对具体类型直接写,会硬错误

第二个坑更隐蔽,是上一个的延伸。咱们想测「string 没有某个方法」,直觉写法是在 requires 表达式里用 string 当参数:

```cpp
// 直觉写法:拿具体类型 string 直接测负例
static_assert(!requires(std::string s) { s.nope(); });   // string 没有 nope
```

```text
four_requirements2.cpp:17:44: error: 'std::string' has no member named 'nope'
```

报的是硬错误,不是优雅的 false。为什么?因为 requires 表达式对**具体类型**是「立即求值」的——编译器看到 `std::string s` 这个具体类型,直接去 string 里找 `nope`,找不到就硬报错,不走 SFINAE 那套「替换失败就返回 false」的机制。换个更刺眼的例子,`requires(int x) { x.foo(); }` 会报 `request for member 'foo' in 'x', which is of non-class type 'int'`,因为 `int` 这种基本类型根本不能写 `.foo()` 语法,解析阶段就过不去。

解决办法是让 requires 表达式处在**模板上下文**里,最常见的做法是把它包进一个 concept:

```cpp
template <typename T> concept HasSize = requires(T t) { t.size(); };
template <typename T> concept HasNope = requires(T t) { t.nope(); };

static_assert(HasSize<std::string>);    // string 有 size -> true
static_assert(!HasNope<std::string>);   // string 没 nope -> false,这次优雅了
static_assert(!HasSize<int>);           // int 没 size -> false
```

```bash
$ g++ -std=c++20 -Wall -Wextra neg_via_concept.cpp -o nvc && echo "全部断言通过"
全部断言通过
```

包进 concept 之后,T 是模板参数,requires 表达式求值时走的是 SFINAE 友好路径:找不到成员就算 false,不硬报错。所以写测试断言时,负例尽量用命名的 concept 包装,别拿具体类型直接往 requires 表达式里塞。

::: warning 两个坑是一回事的两面
不求值和具体类型硬错误,根子都在 requires 表达式的求值时机上。requires 表达式对模板参数是「延迟、SFINAE 友好」的,所以不执行(不求值)、失败返回 false;对具体类型是「立即」的,所以同样不执行,但失败直接硬错误。记住一条:requires 表达式只检查「能不能编译」,从不执行;要让它优雅地返回 false,把它放在模板上下文(通常就是包成 concept)里。
:::

三种东西——四种成分、不求值、模板上下文——凑齐,requires 表达式这个 C++20 里最容易混的词就算讲透了。下一篇咱们从另一个方向看模板的编译期能力:在 concepts 出现之前,模板元编程(TMP)是怎么用特化和 SFINAE 做编译期计算和类型推导的,以及现在怎么把这些老技巧往 concepts 上迁。
