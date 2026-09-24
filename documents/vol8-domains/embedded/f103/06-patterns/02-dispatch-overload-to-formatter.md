---
title: "从重载集到 Formatter 特化：一次嵌入式日志分发机制的选型讨论"
description: "记录 libestdx logger 行组装器分发机制的三段演化：成员重载集（两条实测铁律）只是中间形态，CPO/tag_invoke 漂亮但劝退，最终落在 std::formatter 形状的 Formatter<T> 特化——三种形态的代码、真实报错与取舍理由全部留档，顺带拆掉 LineBuffer 与格式化的耦合"
chapter: 6
order: 2
tags:
  - stm32f1
  - intermediate
  - concepts
  - 模板
  - 零开销抽象
  - 嵌入式
  - 实战
difficulty: intermediate
platform: stm32f1
cpp_standard: [20, 23]
reading_time_minutes: 18
prerequisites:
  - "零开销日志组件完整踩坑记录：从 source_location 撞墙到反汇编验收"
related:
  - "UART：中断驱动、环形缓冲、expected"
---

# 从重载集到 Formatter 特化：一次嵌入式日志分发机制的选型讨论

## 引言：这篇文章记录一个"中间形态"

上一篇我们给 libestdx 写日志组件的行组装器 `LineBuffer` 时，类型分发的最终形态是 `Formatter<T>` 特化。但它是从两个前身演化来的：一个被吐槽长得像管道的 `if constexpr` 链，和一个相当能打的**成员重载集**版本。中间还认真讨论过 CPO / `tag_invoke` 这条 ranges 同款路线，最后劝退。

这篇把三段演化完整留档——不是"正确的答案是什么"，而是"每个形态强在哪、死在哪、为什么对嵌入式这个特定场景，最后一票投给了 `std::formatter` 的形状"。三种形态都是正经设计，很多知名库就停在中间形态上；讨论的价值在于看清各自的账。

先说清我们要分发什么。日志调用长这样：

```cpp
Log::info("led", "count=", n, " hex=", log::Hex(n), " ok=", true);
```

行组装器要回答一个问题：**每个实参的类型，怎么变成文本字节**。字符串直接拷，整数走 `to_chars`，`Hex` 包装走十六进制，用户类型自己说了算，单字符和浮点要定向拒绝。这就是"分发机制"的全部题目。

## 形态零：if constexpr 链，以及它为什么先死

最直觉的写法是一个巨大的单入口模板，内部 `if constexpr` 逐类判断：

```cpp
template <typename V>
void append(V&& value) {
    using U = std::remove_cvref_t<V>;
    if constexpr (std::same_as<U, char>) { /* ... */ }
    else if constexpr (std::same_as<U, bool>) { /* ... */ }
    else if constexpr (std::convertible_to<const U&, std::string_view>) { /* ... */ }
    else if constexpr (requires { typename U::is_log_hex; }) { /* ... */ }
    // ...六七个分支
}
```

它能跑，零运行时开销，但可读性像一坨管道——分支顺序是隐式语义（`string_view` 必须在 `convertible_to` 前面否则无限递归），读者要在脑内模拟一遍分发才能确认哪条路径生效。被吐槽之后我们立刻换了形态一。现在回头看，链的真正问题是**它把"分发顺序"这个本该由语言规则管的东西，变成了代码排版的约定**。

## 形态一：成员重载集——中间形态，比看上去难伺候

把链摊平成重载集，每个类型一个 `append` 重载，这是 `operator<<` 全家的经典形态，读起来顺眼得多：

```cpp
struct LineBuffer {
    void append(char) = delete;                        // 定向拒绝
    void append(bool b);
    void append(std::string_view view);
    void append(std::string_view s, std::size_t width);

    template <TextSource T>          void append(T s);   // 字面量/string
    template <IsHex H>               void append(H h);
    template <std::integral T>       void append(T v);

    template <typename T>
        requires requires { std::declval<const T&>().append_to(std::declval<LineBuffer&>()); }
    void append(T t) { t.append_to(*this); }            // 用户类型扩展点

    template <std::floating_point T> void append(T) = delete;  // float 拒绝
    template <typename T>            void append(T) = delete;  // 兜底拒绝
};
```

这个版本我们真的写出来、跑绿了全部冒烟门。但它带着两条**必须人工看守的铁律**，而且都是实测撞出来的，不是理论推演：

**铁律一：模板重载的形参形式必须统一（一律按值）。** 我们的兜底拒绝最初写成 `const T&`，和按值的整数重载摆在一起，`append(42)` 直接歧义：

```text
error: call of overloaded 'append(int)' is ambiguous
  • candidate 1: 'void LineBuffer<N>::append(T) [with T = int]'
  • candidate 2: 'void LineBuffer<N>::append(const T&) [with T = int]' (deleted)
```

形参形式一混，部分序排不出高下，"约束更严者胜"也救不了场。

**铁律二：成员函数约束里不许用 `*this`。** 想表达"这个类型能 `append_to` 我的缓冲"，直觉写 `requires requires(const T& t) { t.append_to(*this); }`，报错：

```text
error: invalid use of 'this' at top level [-Wtemplate-body]
```

约束在 `this` 不可用的语境求值，只能请 `std::declval<LineBuffer&>()` 当替身。

这两条规矩写进注释就能守住，但它们暴露了一个更深的问题：**`LineBuffer` 这个类干着两份工**——字节窗口管理（游标、钳制、截断标记、行尾）和类型→文本的格式化策略（重载集、`to_chars`、拒绝清单）。两份工缝在一个类里，重载集作为成员存在，就是铁律必须存在的原因；扩展点拿到的是具体的 `LineBuffer&`，于是约束必须知道这个具体类型，`declval` 舞步挥之不去。

## 形态二的候选：CPO / tag_invoke，漂亮但劝退

讨论里出现过一条很现代的路线——定制点对象（CPO）加 `tag_invoke` 协议，`std::ranges` 和 `std::execution` 的同款：

```cpp
inline constexpr struct append_value_t {
    template <typename Buffer, typename T>
    void operator()(Buffer& buffer, const T& value) const
        noexcept(noexcept(tag_invoke(*this, buffer, value)))
    {
        tag_invoke(*this, buffer, value);
    }
} append_value;

// 用户类型:hidden friend 特化 tag_invoke
struct Foo {
    int x;
    friend void tag_invoke(append_value_t, auto& buffer, const Foo& foo) {
        buffer.append(foo.x);
    }
};
```

客观说，这套东西解决的是真问题：统一协议、不占全局函数名、约束可探测（`is_tag_invocable`）。但它对**嵌入式读者**的认知要价是三件套齐上：CPO 函数对象、hidden friend、ADL 两阶段查找——一个写单片机固件的工程师看到 `tag_invoke(*this, buffer, value)` 的第一反应大概率不是"哦定制点"，而是"这是啥"。而且社区里对它的批评是现成的：[Barry Revzin 的经典批评](https://brevzin.github.io/2020/11/30/tag-invoke/)指出 `tag_invoke` 自己就成了全局保留标识符（讽刺的是这恰是它想解决的问题）、错误信息更糟、定制在调用点不可见；连最重度使用它的 std::execution 社区都在 [P2300 的后续讨论](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3281r0.html)里寻求成员定制点来避开这层复杂度。协议层再优雅，"读者一眼看懂"这一票在教程库里的权重就是更高。**劝退，记录在案。**

## 形态三：Formatter<T> 特化，最终形态

最后落点是大家早就认识的形状——`std::formatter` 的翻版。库只提供一个主模板，内置类型用（受约束的）特化实现，用户类型照着特化：

```cpp
// 主模板:未特化 = 不支持,文案指路
template <typename T>
struct Formatter {
    static void format(auto&, const T&) {
        static_assert(!sizeof(T),
                      "logger: no formatter for T; specialize logger::Formatter<T> or wrap in Hex");
    }
};

template <>           struct Formatter<bool>  { /* "true"/"false" */ };
template <TextSource T> struct Formatter<T>   { /* 转 string_view */ };
template <IsHex T>    struct Formatter<T>     { /* "0x" + to_chars 16 */ };
template <typename T>
    requires std::integral<T> && (!std::same_as<T, char>) && (!std::same_as<T, bool>)
struct Formatter<T>   { /* to_chars 10 */ };
```

用户类型扩展点变成一个特化，一眼就懂：

```cpp
template <>
struct estdx::logger::Formatter<Point> {
    static void format(auto& out, const Point& p) {
        out.append("("); out.append(p.x); out.append(","); out.append(p.y); out.append(")");
    }
};
```

与此同时 `LineBuffer` 拆成纯字节层（钳制拷贝、`pad`、给 `to_chars` 的 `window()`/`commit()`、`finish`），两层的唯一接缝是一行转发：

```cpp
template <typename T>
void append(const T& value) {
    Formatter<std::remove_cvref_t<T>>::format(*this, value);   // Formatter 前置声明即可
}
```

这个形态把形态一的账一次性清了：

- **两条铁律消失**。没有重载集就没有重载决议的坑，分发变成偏特化排序，而特化集合按构造互斥（整数特化显式排除 `char`/`bool`），不存在"需要人守的规矩"；
- **`declval` 舞步消失**。`format(auto& out, ...)` 是自由取用的函数形参，不再是成员约束里的 `*this`；
- **拒绝质量保住了**。三个负例各指各的特化行，真实报错如下：

```text
format.hpp:76  error: static assertion failed:
               logger: single chars are not in the logging vocabulary; pass a string_view
format.hpp:86  error: static assertion failed:
               logger: float rejected (FP formatting tables cost flash); use Hex or fixed-point
format.hpp:35  error: static assertion failed:
               logger: no formatter for T; specialize logger::Formatter<T> or wrap in Hex
```

- **职责边界干净**：`LineBuffer` 只回答"怎么安全地把字节塞进固定窗口"，`Formatter<T>` 只回答"一个类型如何变成日志文本"。想换格式化策略（比如将来加一个二进制通道的格式化器）不用碰缓冲半行。

当然它也有代价，如实记录：特化比"写个成员函数"的仪式感重一点；分发从重载决议变成模板实例化，报错栈更深一层（但文案兜底反而更可控）；以及两个语言细节必须踩对——

**细节一：拒绝用 `static_assert` 时条件必须依赖模板形参。** `static_assert(false)` 写在函数模板里会在任何包含处无条件炸；要写成 `static_assert(!sizeof(T), ...)`（`T` 是模板参数，依赖了才推迟到实例化）。显式特化 `Formatter<char>` 里 `char` 已定死，就借用 `format` 的 `auto& out` 形参：`static_assert(sizeof(out) == 0, ...)`。

**细节二：requires 子句里的 `!concept<T>` 不是初等表达式。** 受约束偏特化写到 `requires std::integral<T> && !std::same_as<T, char>` 时，clangd 立刻标红：

```text
Parentheses are required around this expression in a requires clause
```

正确写法是给非初等的操作数加括号：`std::integral<T> && (!std::same_as<T, char>)`。GCC 在这里比标准宽容，不修也能编，但可移植代码得按语法来。

## 三种形态的对账单

| | if constexpr 链 | 成员重载集 | CPO / tag_invoke | Formatter 特化 |
|---|---|---|---|---|
| 分散度 | 全在一处（也全是它的错） | 每类型一个成员 | hidden friend + CPO | 每类型一个特化 |
| 需要人守的规矩 | 分支顺序 | 两条铁律（形参统一/不用 this） | 协议约定 + ADL 心智 | 特化集合互斥（构造即正确） |
| 拒绝的报错 | static_assert 文案 | `= delete` 逐条理由 | 薄弱（社区公认痛点） | static_assert 逐条理由 |
| 读者门槛 | 低（就是长） | 中（懂重载决议） | 高（CPO+ADL+两阶段） | 低（`std::formatter` 熟脸） |
| 与缓冲的耦合 | 全耦 | 扩展点钉死具体类型 | 解耦 | 一行转发的缝 |

我们的场景权重是"教程库 + 嵌入式读者 + 拒绝报错要指路"，最后一票投给 Formatter 特化。如果你的库是泛型框架、读者是库作者，CPO 的统一协议就可能是值得的；如果你的类型集合封闭且小，重载集甚至链都完全够用——形态没有对错，只有对不上账。

## 小结

- 中间形态（成员重载集）不是失败品，它跑绿了全部门；逼我们搬家的是它暴露的**职责耦合**——缓冲管理和格式化策略缝在一个类里，铁律与 `declval` 都是这根缝的并发症；
- CPO / `tag_invoke` 是为"泛型框架的统一协议"生的，嵌入式教程库里"读者一眼看懂"权重更高，社区对它的错误信息与不可见性批评也确有其事；
- `Formatter<T>` 特化借了 `std::formatter` 的熟脸：特化集合按构造互斥、拒绝文案各归各位、`LineBuffer` 回到纯字节层，一行前置声明转发的缝；
- 两个语言细节值得记住：`static_assert` 拒绝必须让条件依赖模板形参；requires 子句给 `!concept<T>` 加括号。

代码在 [libestdx/logger/](https://github.com/Charliechen114514/libestdx/tree/main/include/libestdx/logger)（`buffer.hpp` 字节层 + `format.hpp` Formatter 层），演化过程中的中间版本在这篇文章里永久留档。

## 参考资源

- [Why tag_invoke is not the solution I want — Barry Revzin](https://brevzin.github.io/2020/11/30/tag-invoke/)
- [P3281: Member customization points (std::execution 社区对 tag_invoke 复杂度的回应)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3281r0.html)
- [Customization point object - cppreference](https://en.cppreference.com/w/cpp/named_req/CustomizationPointObject)
- [std::formatter - cppreference](https://en.cppreference.com/w/cpp/utility/format/formatter)
- [libestdx 仓库](https://github.com/Charliechen114514/libestdx)
