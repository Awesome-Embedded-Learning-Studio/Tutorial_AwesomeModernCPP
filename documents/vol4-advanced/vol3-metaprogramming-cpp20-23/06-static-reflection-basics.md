---
chapter: 13
cpp_standard:
- 26
description: C++26 的 P2996 静态反射让编译期自省成为语言内建能力。讲清反射运算符、splice 重组、identifier_of 与 members_of、template for 遍历,以及枚举转字符串这个杀手级用例。含 Godbolt clang-p2996 实跑输出与本机不支持的诚实交代
difficulty: advanced
order: 6
platform: host
prerequisites:
- 编译期字符串:NTTP class type 与 fixed_string
- TMP 核心技巧:concepts 之前的世界
reading_time_minutes: 11
related:
- 编译期字符串:NTTP class type 与 fixed_string
- 模板实例化控制:extern template 与编译时间
tags:
- host
- cpp-modern
- advanced
- 编译期计算
- 模板元编程
- 类型安全
title: 静态反射基础:反射运算符与 splice 重组
---
# 静态反射基础:反射运算符与 splice 重组

上一篇讲编译期字符串时提了一句:C++26 的反射要让「枚举转字符串」这类苦活变成语言内建的能力。这一篇就来兑现这个话头。反射(reflection)指的是程序在编译期「自省」自身结构的能力,某个类有哪些成员、某个枚举有哪些值、某个函数签名长什么样。C++ 等这项能力等了二十多年,直到 P2996 提案在 2025 年 6 月的 WG21 Sofia 会议上投票进入 C++26 工作草案。这一篇讲清反射运算符、splice、几个核心 API,以及两个最直观的用例:遍历 struct 成员、枚举转字符串。

::: warning 这是 C++26,本机主流编译器还跑不了
P2996 虽然进了 C++26 草案,但实现现状很初期。截至 2026 年 7 月,GCC 和 MSVC 都还没 shipping,能跑的只有 Bloomberg 开源的 clang-p2996 实验分支和 EDG 的预览实现。本机的 GCC 16.1.1 和 clang 22.1.8 都不支持,实测 GCC 即便带 `-freflection` 标志、有空的 `<meta>` 头,见到 `^^Point` 还是会报 `expected primary-expression before '^'`;clang 则根本不认 `-freflection-latest` 这个参数。所以这一篇里所有反射代码的运行输出,都是在 Godbolt 的 `clang_bb_p2996`(Bloomberg 分支 trunk-20260701)上实跑出来的,编译参数 `-std=c++2c -freflection-latest`。您手头要验证,也请去 Godbolt 选这个编译器。
:::

## 反射运算符 ^^ 和 splice [: :]

P2996 的核心是两个新语法。第一个是反射运算符 `^^`,它作用在一个类型或值上,产出一个叫 `std::meta::info` 的编译期值,编译器拿着它就能去查这个东西的结构。

```cpp
#include <meta>

struct Point { int x; int y; };

constexpr auto refl = ^^Point;   // refl 是 std::meta::info,指向 Point 这个类型
```

`^^Point` 算出来是个编译期的 `info` 值,您可以把它当成「关于 `Point` 类型的元信息」。拿到这个 `info`,后续才能去问「你叫什么名字」「你有哪些成员」。

第二个语法是 splice,写法 `[: refl :]`,它把一个 `info` 重新粘回成原本的类型或值。`^^` 把类型变成 `info`,`[: :]` 把 `info` 变回类型,两者互为逆操作。下面讲枚举的例子会看到 `[:enumerator:]` 把一个枚举值的 `info` 粘回成枚举值本身,用来做比较。

## 核心 API:identifier_of、members_of、access_context

拿到 `info` 之后,P2996 在 `std::meta` 命名空间下提供了一组查询函数。这篇用到的几个:

- `identifier_of(info)` 返回实体名字的 `string_view`(R13 之前叫 `name_of`,后来改名)。
- `nonstatic_data_members_of(type_info, access_context)` 返回一个 `vector<info>`,装着这个类型的所有非静态数据成员。
- `enumerators_of(enum_info)` 返回枚举的所有枚举值。

`access_context::current()` 这个参数是 P2996R10 之后加的,用来告诉反射「以当前的访问权限去查成员」,毕竟私有成员不是谁都能看见的。每次调用查成员的函数都得把它传进去,啰嗦但是必要。

还有一个绕不开的零件叫 `define_static_array`。P2996 的查询函数返回的是 `constexpr std::vector`,而 `vector` 的内存在堆上,模板里「堆指针」没法当作编译期常量。`define_static_array` 把 vector 的内容物化到一块静态存储里,返回一个能在编译期遍历的 span。听起来绕,但只要您想用 `template for` 遍历查询结果,就得套这一层。

## template for:编译期遍历反射结果

反射返回的东西数量是编译期已知的(一个 struct 的成员数在编译期就定了),但传统 `for` 没法遍历「类型」。P1306 的 expansion statement(`template for`)就是为这个造的,它能对一组编译期已知的值逐个展开,每次迭代的量都是真正的编译期常量,能在类型位置用。

```cpp
template for (constexpr auto member : members) {
    std::cout << "  " << std::meta::identifier_of(member) << "\n";
}
```

注意是 `template for` 不是 `for`,而且循环变量声明成 `constexpr auto`。这趟循环在编译期完全展开,等价于把每个成员的 `cout` 语句手写一遍。

## 实战一:遍历 struct 的成员

把上面的零件拼起来,就能写出反射版「打印一个 struct 的所有成员名」。完整代码:

```cpp
#include <meta>
#include <iostream>

struct Point {
    int x;
    int y;
};

int main() {
    using namespace std::meta;
    constexpr auto refl = ^^Point;
    constexpr auto ctx = access_context::current();
    constexpr auto members = define_static_array(nonstatic_data_members_of(refl, ctx));

    std::cout << identifier_of(refl) << "\n";
    template for (constexpr auto member : members) {
        std::cout << "  " << identifier_of(member) << "\n";
    }
}
```

在 Godbolt 的 clang_bb_p2996 上跑(本机跑不了,原因见开头警告):

```text
Point
  x
  y
```

这段代码没有任何硬编码的字符串。`Point` 这个名字、`x` 和 `y` 这两个成员名,全是编译器从类型本身查出来的。给 `struct` 加一个字段、改个名,输出的内容自动跟着变,代码一行都不用改。这就是反射的价值,它消除了「类型定义」和「处理类型的代码」之间那道手动同步的缝。

## 实战二:枚举转字符串

反射最出圈的用例,是把枚举值转成它的名字。在反射之前,这件事要么手写一个 `switch` 把每个枚举值映射到字符串(C++ 代码库里遍地都是这种样板),要么靠 magic_enum 这种第三方库用 `__PRETTY_FUNCTION__` 解析偷偷实现。反射让它变成几行直给的代码:

```cpp
#include <meta>
#include <iostream>
#include <string_view>

enum class Color { Red, Green, Blue };

template <typename E>
constexpr std::string_view enum_to_string(E value) {
    using namespace std::meta;
    constexpr auto enumerators = define_static_array(enumerators_of(^^E));
    template for (constexpr auto enumerator : enumerators) {
        if (value == [:enumerator:]) {
            return identifier_of(enumerator);
        }
    }
    return "<unknown>";
}

int main() {
    std::cout << enum_to_string(Color::Red) << "\n";
    std::cout << enum_to_string(Color::Green) << "\n";
    std::cout << enum_to_string(Color::Blue) << "\n";
}
```

```text
Red
Green
Blue
```

这段值得拆一下。`enumerators_of(^^E)` 拿到枚举 `E` 所有枚举值的 `info` 列表;`template for` 逐个看,`[:enumerator:]` 把当前这个 `info` splice 回成枚举值本身,再和传进来的 `value` 比;匹配上的那个,用 `identifier_of` 取名字返回。整个过程在编译期展开,运行时只剩一串比较,没有字符串解析,也没有查表开销。

这个模板对任何枚举都能用,您新加一个枚举值,`enum_to_string` 自动支持,不用再去补 switch。对照上一篇里咱们为了把字符串编进类型费的那番周折(fixed_string、structural、CTAD),反射让「名字」从一种要费力气搬运的东西,变回编译器本来就掌握的信息,您只是开口去问。

## 还要再等等

P2996 给的能力远不止这两样。按名字查类型、给类型加注解(annotation)、自动生成序列化代码、struct 到 struct 的字段映射,这些过去要靠重型代码生成器或一摞宏的活,反射都能在语言内干掉。但前提是编译器得跟上。这一篇用的 Bloomberg clang-p2996 是「高度实验性」的分支,README 里明说不要用于任何要上生产的产物。在 GCC 和 MSVC 真正 ship 之前,反射主要是「能看到未来」的状态,值得学,因为 API 设计已经稳定(R13 投票通过),等编译器到了就能直接用;但不值得现在就往生产代码里塞。下一篇咱们回到今天就能用的东西,模板实例化怎么控制,编译时间怎么治。
