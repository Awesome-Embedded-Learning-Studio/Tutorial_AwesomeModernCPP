---
chapter: 13
cpp_standard:
- 20
description: 把字符串当模板参数,在 C++17 之前难到几乎没人做。讲清 const char* 作 NTTP 的坑、C++20 P0732 的 structural type、fixed_string 惯法,以及编译期哈希这条不碰 NTTP 的备选路子
difficulty: intermediate
order: 5
platform: host
prerequisites:
- TMP 核心技巧:concepts 之前的世界
- Concepts:把模板约束写进签名
reading_time_minutes: 14
related:
- TMP 核心技巧:concepts 之前的世界
- 静态反射基础:反射运算符与 splice 重组
tags:
- host
- cpp-modern
- intermediate
- 编译期计算
- 模板元编程
- 类型安全
title: 编译期字符串:NTTP class type 与 fixed_string
---
# 编译期字符串:NTTP class type 与 fixed_string

上一篇结尾留了个话头:把字符串当模板参数。这件事听着简单,`template <"hello">` 嘛,但在 C++17 及以前它难到几乎没人愿意做。C++20 的 P0732 提案把这扇门彻底推开,让 class type 能当非类型模板参数(NTTP)用,`fixed_string` 这个经典惯用法就是直接的受益者。这一篇咱们从「为什么以前难」讲到「现在怎么顺手」,顺带说清 structural type 这个新概念到底在约束什么。

## 先说麻烦:C++17 之前 const char* 作 NTTP 的难处

在 C++20 之前,NTTP 只能是整数、枚举、指针、引用这几类,字符串想挤进来只能走 `const char*`。但 `const char*` 作模板参数天生不好用,先看最致命的一刀:字符串字面量**根本不能直接写进模板参数列表**。

```cpp
template <const char* Name>
struct Bad {};
Bad<"hello"> b;   // 字面量作 NTTP,编译失败
```

```text
error: '"hello"' is not a valid template argument for type 'const char*'
       because string literals can never be used in this context
```

GCC 的原话点得很透,`string literals can never be used in this context`。原因是模板参数要求有链接性(linkage),好让两个翻译单元里同一个模板实例能合并;字符串字面量没有链接性,编译器直接拒收。要让 `const char*` 作 NTTP,您得先把字符串存进一个有外部链接的 `constexpr` 变量里:

```cpp
constexpr const char kRed[] = "red";   // 有 linkage 的对象
template <const char* Name>
struct Tagged { static constexpr const char* name = Name; };

Tagged<kRed> t;   // 这次能编过
```

能编是能编,但这又引出第二层麻烦:`Tagged<kRed>` 里的参数是 `kRed` 这个**对象的地址**,不是 `"red"` 这串字符的内容。两个翻译单元里如果各定义了一个 `kRed`,它们地址不同,`Tagged<kRed>` 会实例化出两个不同类型。这跟模板「相同参数等于相同类型」的直觉是冲突的。再叠加一层维护成本:每个想用作模板参数的字符串都得先在外面声明一个变量,代码里到处是这种为编译器服务的样板变量。这几条加起来,就是 C++20 之前「字符串当模板参数」基本没人用的原因。

## C++20 的解药:P0732 与 structural type

P0732(Louis Dionne 提出,进 C++20)给了一条新路:让 class type 也能作 NTTP。但有个前提,这个类得是所谓的 **structural type**(结构化类型)。structural 的要求概括成两句:它得是个 literal class type(能在编译期完成构造和析构),而且**所有基类和所有非静态数据成员都必须是 public**。后一条是写代码时最容易踩的线,后面会专门演示。它的本意是让编译器能「按数据成员的值」判断两个模板参数是否等价,而不用去调用用户写的 `operator==`——在模板等价性这种编译期机制里调用户代码,会惹出一堆麻烦。

`fixed_string` 就是顺着这条规则设计出来的:把一串字符包进一个只有 public `char` 数组成员的结构体,它就满足 structural,能作 NTTP 了。下面是一个最小可用的实现:

```cpp
template <std::size_t N>
struct FixedString {
    char value[N] = {};

    // 字面量 "abc" 的类型是 const char[4](含 \0),正好匹配 const char(&)[N]
    constexpr FixedString(const char (&str)[N]) {
        for (std::size_t i = 0; i < N; ++i) value[i] = str[i];
    }

    constexpr bool operator==(const FixedString& other) const {
        for (std::size_t i = 0; i < N; ++i) {
            if (value[i] != other.value[i]) return false;
        }
        return true;
    }

    constexpr const char* c_str() const { return value; }
};

// CTAD 推导指引:让字面量 "hello" 推导出 FixedString<6>(6 含末尾 \0)
template <std::size_t N>
FixedString(const char (&)[N]) -> FixedString<N>;
```

这里有个细节值得停下来看清。`"hello"` 这种字符串字面量,类型是 `const char[6]`,5 个字符加一个结尾 `\0`。所以 CTAD 推导出的 `N` 是 6,`value[6]` 里正好存得下完整字符串和 `\0`。构造函数收的是 `const char(&str)[N]`(数组的引用),这样 `N` 能从字面量的大小推出来,不用手写。

::: warning operator== 不参与模板等价性判断
咱们给 `FixedString` 写的 `operator==`,只在「运行时比较两个对象」或 `if constexpr` 手动比较时被调用,**和模板参数的等价性判断毫无关系**。编译器判断 `Named<"abc">` 和另一个 `Named<"abc">` 是不是同一类型,依据的是 structural 规则,逐字节比 `value` 数组的值,不调用任何用户代码。换句话说,您把 `operator==` 整个删掉,`Named<"abc">` 和 `Named<"abc">` 仍然是同一类型,`Named<"abc">` 和 `Named<"abd">` 仍然是两个不同类型。想偏了这一点,读别人用 NTTP class type 的代码会处处拧巴。
:::

## fixed_string 实战:字符串当模板参数

设计好了,就能把 `FixedString` 直接塞进 `template <...>` 里:

```cpp
template <FixedString S>
struct Named {
    static constexpr auto name = S;
};

template <FixedString S>
void greet() {
    std::cout << "hello, " << S.c_str() << "\n";
}
```

跑一下:

<OnlineCompilerDemo allow-run
  title="fixed_string 当 NTTP,字符串编进类型"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/nttp_fixed_string.cpp"
  description="FixedString 是 structural 类型,能直接塞进 template 参数,字符串由此编进类型本身。"
/>

运行结果:

```text
world
hello, templates
编译期字符串比较断言通过
```

`Named<"world">{}` 实例化出一个类型,它的 `name` 成员在编译期就存着 `"world"`;`greet<"templates">()` 同理,字符串 `"templates"` 被编进了类型本身。这件事在 C++17 里几乎做不到,现在一行模板参数就解决了。

再看编译期的字符串比较。`FixedString{"abc"} == FixedString{"abc"}` 在 `static_assert` 里成立,说明比较完全在编译期完成,结果是个常量布尔值。这种能力在「用字符串做编译期分派」的场景里很实在,比如根据一个配置字符串选不同的实现,过去得用枚举绕一圈,现在直接拿字符串字面量说话。

## structural 的限制:为什么不能随便拿个类当 NTTP

P0732 开了门,但只开了 structural 类这扇。咱们试一个故意的反例,把一个有 private 数据成员的类塞进 NTTP:

```cpp
class Secret {
    int x;   // private(默认 class 成员是 private)
public:
    constexpr Secret(int v) : x(v) {}
};

template <Secret S>
struct Bad {};
Bad<Secret{1}> bad;
```

```text
error: 'Secret' is not a valid type for a template non-type parameter
       because it is not structural
note: 'Secret::x' is not public
```

报错把规则说得很直白:`not structural`,原因是 `Secret::x` 不是 public。编译器要能「逐成员看值」来判断两个 NTTP 是否等价,成员如果是 private 的,编译器没法这么直接地访问它们,还会牵涉访问权限的语义,索性一刀切要求全 public。所以您想把一个自定义类作 NTTP,第一步就是检查它的数据成员是不是都暴露在外。带 `std::string` 成员的类也别指望:`std::string` 自己有 private 数据,不满足 structural,连带整个类也用不了。

## 编译期哈希:不一定非得 NTTP

讲到这里要补一句,并不是所有「编译期处理字符串」的需求都需要把字符串编进类型。很多场景用一个 `constexpr` 函数就够,比如编译期算个字符串哈希:

```cpp
constexpr std::uint64_t fnv1a_64(std::string_view s) {
    std::uint64_t hash = 14695981039346656037ULL;  // FNV offset basis
    for (char c : s) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL;  // FNV prime
    }
    return hash;
}
```

<OnlineCompilerDemo allow-run
  title="编译期 FNV-1a 字符串哈希"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/compile_time_hash.cpp"
  description="纯 constexpr 函数,不碰 NTTP,哈希值在编译期算定,可塞进 static_assert。"
/>

运行结果:

```text
fnv1a_64("hello") = 11831194018420276491
fnv1a_64("world") = 5717881983045765875
编译期哈希断言通过
```

`fnv1a_64("hello")` 在编译期就算定,可以塞进 `static_assert`、可以当 `switch` 的 `case`(哈希值是常量)、可以做编译期的字符串匹配。这套思路不碰 NTTP,通用性反而更好,`std::string_view` 能接任何字符串来源。所以一个朴素的决策:您需要把字符串编进类型、参与重载或当类型标签时,上 `fixed_string` NTTP;只是想在编译期对一串字符算个结果时,`constexpr` 函数就够,别动用 NTTP。

下一篇咱们往 C++26 看一眼:静态反射(P2996)要让「枚举转字符串、按名字查类型」这类过去得靠 `fixed_string` 加一堆模板苦活才能办的事,变成语言内建的能力。
