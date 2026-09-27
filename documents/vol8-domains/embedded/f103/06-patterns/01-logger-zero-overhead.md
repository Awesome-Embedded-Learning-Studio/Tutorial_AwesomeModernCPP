---
title: "零开销日志组件完整踩坑记录：从 source_location 撞墙到反汇编验收"
description: "给 libestdx 设计跨单片机 logger 的前半程实录：三个设计决策怎么拍板、source_location 与参数包的两连撞、编译期裁剪要靠 flag 链兜底、重载集分发的两条铁律、to_chars 的三个暗坑、逐字节分支 vs 钳制一次的 M3 反汇编对比——每一条断言都有编译器输出或汇编背书"
chapter: 6
order: 1
tags:
  - stm32f1
  - intermediate
  - 零开销抽象
  - concepts
  - 模板
  - 嵌入式
  - 实战
difficulty: intermediate
platform: stm32f1
cpp_standard: [17, 20, 23]
reading_time_minutes: 25
prerequisites:
  - "起步：为什么是 C++，凭什么？"
  - "LED：地砖下面看裸寄存器，HAL 之上写现代 C++"
related:
  - "UART：中断驱动、环形缓冲、expected"
---

# 零开销日志组件完整踩坑记录：从 source_location 撞墙到反汇编验收

## 引言：为什么嵌入式值得自己写一个 logger

日志这个东西，host 上没人自己写——`spdlog` 一装什么都有了。但到了 BluePill 这种 64KB flash / 20KB RAM 的板子上，主流方案的账就很不好算：`printf` 家族把变参类型安全丢了，浮点和 locale 一不注意就拖几 KB 进固件；`std::format` 优雅是优雅，GCC 的实现拉进来的体积是数十 KB 量级，对 64KB 的片上 flash 属于"一寸山河一寸血"；各家 SDK 的日志宏倒是轻，但元数据靠 `__FILE__`/`__LINE__`，和"现代 C++ 不动辄上宏"的立场拧着。

所以我们在 [libestdx](https://github.com/Charliechen114514/libestdx) 里决定自己写一个，目标很朴素：**拼接式格式化、无宏元数据、编译期级别裁剪，每一项"零开销"的宣称都要能用反汇编或 map 文件验证**。这篇文章是前半程（词汇层 + 行组装层）的完整实录——不是"我教你这么写"，而是"我们这么写的时候撞了什么墙、怎么用编译器输出把墙拆了"。每个坑都有真实的报错或汇编，你可以拿同样的命令复现。

先把三个最重要的设计决策摆出来，它们决定了后面所有的坑长什么样：

| 维度 | 决策 | 一句话理由 |
|---|---|---|
| 格式化 | 拼接式 + `std::to_chars` | 最省 flash 的正路，无 locale、无堆、无变参 |
| 元数据 | `std::source_location`，不上宏 | C++20 有标准设施就没理由用宏 |
| 裁剪 | 级别阈值做模板参数，`if constexpr` 短路 | 低于阈值的日志**连字符串都不进固件** |

工具链是 arm-none-eabi-gcc 16.2，库本体 C++23、无异常无 RTTI 无堆。下面开始撞墙。

## 第一坑：`source_location` 放不进参数包

我们想要的调用形态长这样，`tag` 后面跟任意个可格式化的参数，同时自动捕获调用点的文件行号：

```cpp
Log::info("led", "count=", n);   // 内部自动带上 file:line
```

直觉写法一：把 `source_location` 作为带默认值的**首参**，参数包跟在后面：

```cpp
template <typename... Parts>
static void info(const std::source_location& loc = std::source_location::current(),
                 std::string_view tag = {}, Parts&&... parts);
```

真实报错（一字未改）：

```text
error: no matching function for call to 'Log<...>::info(const char [4], const char [7], int)'
note: template argument deduction/substitution failed:
note: cannot convert '"led"' (type 'const char [4]') to type 'const std::source_location&'
```

原因在于位置调用时第一个实参 `"led"` 会去绑 `loc`——默认实参只对"你没传"的情况生效，对"你传了但位置不对"无能为力。

直觉写法二：那把 tag 类型改成自定义的 `LogTag` 结构体，让它从 `string_view` 转换过来？同样撞墙：

```text
note: cannot convert '"led"' (type 'const char [4]') to type 'LogTag'
```

这次的原因更隐蔽：`const char[4] → std::string_view → LogTag` 这条链上有**两次用户定义转换**（`string_view` 的转换构造 + `LogTag` 的转换构造），而一条隐式转换序列里只允许一次。这不是库的问题，是语言规则，社区里 [cor3ntin 的经典文章](https://cor3ntin.github.io/2020/06/18/nonterminal/)把整个问题域写得很清楚：**带默认实参的 `source_location` 和非末端参数包天生不兼容**，[cppstories 也有专文讨论](https://www.cppstories.com/2021/non-terminal-variadic/)各路绕法。

解法是让 tag 类型自己吸收位置信息——**约束模板构造函数**，一次转换直达：

```cpp
struct Tag {
    std::string_view text;
    std::source_location location;

    template <TextSource S>
    Tag(S&& s, const std::source_location& l = std::source_location::current())
        : text(std::forward<S>(s)), location(l) {}
};

// Logger 成员:
template <typename... Parts>
static void info(Tag tag, Parts&&... parts);
```

原理：`"led"` 到 `Tag` 现在只有**一次**用户定义转换（约束模板构造），而这发生在日志调用点，所以构造函数默认实参里的 `current()` 捕获的正是写 `Log::info("led", ...)` 那一行的位置。实测验证一行见分晓：

```text
$ ./tag_smoke
file=smoke_tag.cpp line=9 all OK      # line 9 正是 Tag a("lit"); 所在行
```

::: tip 踩坑预警
别在这条路上浪费第二个小时：任何"把 `source_location` 默认实参和参数包排在一起"的排法都过不了编译。要么宏注入（我们不要），要么像上面这样让某个参数类型在调用点构造时把位置"捎"进来。这是本篇最值钱的一条捷径。
:::

## 第二坑：编译期裁剪不是语言保证，是 flag 链的集体功劳

"低于阈值的日志零成本"这句话，人人都说，但多数人没验证过它到底裁到了什么程度。我们把 `if constexpr` 短路翻开看：它裁掉的只是**函数体**；调用点的字符串字面量、`source_location` 对象仍然会被求值、地址仍被引用。它们最终从不从固件里消失，取决于这条链：

```text
-O3 内联 → 死代码消除 → -ffunction-sections -fdata-sections → -Wl,--gc-sections
```

这里有个特别容易漏的环节：很多项目（包括当时的 libestdx）只配了链接器的 `--gc-sections`，却没配编译期的 `-ffunction-sections -fdata-sections`——没有后者，gc-sections 只能按**对象文件**粒度回收，字符串级别的清除根本轮不到。补齐 flag 后我们做过一次硬核验证：在被裁剪级别的日志里塞一个唯一标记串——

```cpp
Log<LogLevel::Error>::info("hidden", "UNIQUE_MARKER_MUST_VANISH_7f3a", 7);
```

用 `-O3 -ffunction-sections -fdata-sections -Wl,--gc-sections` 编译后查产物：

```text
$ strings a.out | grep UNIQUE_MARKER
DCE: GOOD - string stripped          # 标记串零残留
```

而启用路径的文件名字符串照常保留——该在的在，不该在的不在，这才是"零开销"的完整含义。顺手再配一个 `-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.`，否则 `source_location` 会把构建机的绝对路径烤进固件，每条日志点位都在为 flash 付费。

::: tip 踩坑预警
调构建标志之前，**先记下各固件的 `arm-none-eabi-size` 基线**再改，改完对比。预期持平或下降；如果哪个固件变大了，你要的是那份数据，而不是一句"应该没事"。
:::

## 第三坑：分发用重载集，两条铁律是实测撞出来的

行组装器 `LineBuffer` 需要对不同类型的参数做分发：字符串直接拷、整数走 `to_chars`、`Hex` 包装走十六进制、自定义类型走 `append_to` 扩展点。我们的第一版是一大条 `if constexpr` 链——能跑，但读起来像一坨管道。换成重载集（`operator<<` 全家的形态）之后，30 秒内撞了两个坑，正好变成两条铁律。

**铁律一：所有模板重载的形参形式必须统一（一律按值）。** 第一版的 deleted 兜底用了 `const T&`，和按值的整数重载摆在一起，`append(42)` 直接歧义：

```text
error: call of overloaded 'append(int)' is ambiguous
  • candidate 1: 'void LineBuffer<N>::append(T) [with T = int]'
  • candidate 2: 'void LineBuffer<N>::append(const T&) [with T = int]' (deleted)
```

形参形式不同，部分序排不出高下，"约束更严者胜"也救不了场。统一按值后，同形参打平，约束接管裁决。

**铁律二：约束表达式里不许用 `*this`。** 想表达"这个类型能 `append_to` 我的缓冲"，直觉写法是：

```cpp
template <typename T>
    requires requires(const T& t) { t.append_to(*this); }   // ✗
```

真实报错：

```text
error: invalid use of 'this' at top level [-Wtemplate-body]
```

约束在 `this` 不可用的语境求值，得用 `std::declval<LineBuffer&>()` 替身。两条铁律都钉上之后，重载集长这样（节选）：

```cpp
void append(bool b);                       // 非模板：类型定死，无变化就不模板化
void append(std::string_view view);

template <TextSource T>                    // 字面量 / const char* / std::string
void append(T s) { append(std::string_view{s}); }

template <std::integral T>                 // char/bool 已被非模板重载拦走
void append(T v) { write_int(v, 10); }

template <typename T>                      // 自定义类型扩展点
    requires requires { std::declval<const T&>().append_to(std::declval<LineBuffer&>()); }
void append(T t) { t.append_to(*this); }

template <std::floating_point T>
void append(T) = delete;   // logger: float rejected (FP tables cost flash)

template <typename T>
void append(T) = delete;   // logger: no formatter for T; provide append_to or Hex
```

`= delete` 带注释这个姿势比 `static_assert` 拒绝**更好用**：两种拒绝各占一行、各说各的理由，编译错误直接指到对应行——`float` 的错误指着 float 那行，没实现 `append_to` 的类型指着兜底那行，读者看错误信息就知道该改哪。

还有一个"要不要模板化到底"的问题值得单独说：`append(char)`/`append(bool)` 保持**非模板**不是偷懒。模板是给"类型在变"的场合的，这两个类型定死，普通函数就是最简正确解；而且"非模板优先于模板"是重调决议里最硬的平手规则，零推理成本。反过来，如果你把它们也写成 `template<std::same_as<char> T>`，它和 `integral` 模板互不蕴含，`append('x')` 当场歧义——我们又实测了一次才敢把这句话写进注释。

## to_chars 的三个暗坑

`std::to_chars` 是嵌入式格式化的正路，但它有三个坑，一个比一个阴：

**坑一：`to_chars` 有 `char` 重载，且把字符当整数。** `to_chars('x')` 输出的是 `"120"`——字符的码点数字。如果 `append(char)` 重载被删掉，单字符参数会静默掉进整数重载，**输出悄悄变错**，这是比编译错误糟糕一个量级的失败模式。所以"我们极少用单字符"的正确落地不是删掉重载，而是定向拒绝：

```cpp
void append(char) = delete;   // 想要单字符就传 string_view("(")
```

实测 `to_chars(buf, buf + 8, 'x')` 的输出确实是 `"120"`，这不是猜的。

**坑二：`to_chars` 没有 `bool` 重载。** 不拦的话错误发生在我们实现内部的调用处，报错面目模糊。`append(bool)` 非模板重载把 `"true"/"false"` 直接输出，又快又好读。

**坑三：浮点一律拒绝。** 浮点 `to_chars` 会把 libstdc++ 的浮点查表代码拖进 flash，对资源的伤害和它带来的便利完全不成比例。嵌入式日志里的"小数"用定点数或 `Hex` 打原始位型更诚实。这条同样用 `= delete` + 理由注释钉死。

## 性能收口：逐字节分支 vs 钳制一次

行组装的拷贝循环，我们最初写的是最朴素的那种——每个字节过一次 `put()`，由它判断窗口满没满：

```cpp
for (char c : view) {          // 朴素版：每字节一次分支
    put(c);
}
```

被作者当面吐槽"性能爆炸"之后，我们让 M3 的反汇编说话。`-O3 -mcpu=cortex-m3` 下，朴素版热循环每字节约 8 条指令，而且 `pos_` 游标**每个字节都在寄存器和内存之间打一个来回**（分支让它无法跨迭代驻留寄存器）：

```text
1e: ldrb r2, [r2]          @ 取字节
20: cmp  lr, r1            @ 循环尾判断
22: str.w ip, [r0, #128]   @ pos_ 写回内存 —— 每字节一次!
26: strb r2, [r0, r3]      @ 存字节
2a: mov r3, ip
2c: mov r2, r1
2e: cmp r3, #124           @ 窗口判断 —— 又来一次
...
```

改成"钳制一次、无分支整段拷"之后，GCC 认出了拷贝模式，切成 4 字节块拷：

```text
48: ldr.w ip, [r3], #4     @ 4 字节取
4e: str.w ip, [r2], #4     @ 4 字节存
52: bne.n 48               # 每 4 字节 3 条指令,尾部最多 3 字节收尾
```

账面：每字节 ~8 条指令降到 ~0.75 条，**约 9 倍**；100 字符的拷贝从 ~800 条指令降到 ~90 条。这里要诚实补一句系统级的定量：115200 波特下每字节的线上时间约 87µs，100 字符约 8.7ms——毫秒级的线速把微秒级的拷贝优化整个盖住了。那为什么还必须改？一是这套库立的就是"零开销"的招牌，代码就得长得像零开销，反汇编是要进文章当配图的；二是 `-O0` 调试构建下朴素版是每字节一次真实函数调用，单步调试的体感差距巨大；三是将来接 RTT 或 DMA 这类没有线速掩体的 sink，这个惯用法是地基。

钳制版的核心是把"窗口还剩多少"算一次，而不是每字节问一次：

```cpp
void append(std::string_view view) {
    const std::size_t room = content_cap() - pos_;
    const std::size_t take = view.size() < room ? view.size() : room;
    char* dst = buf_.data() + pos_;
    const char* src = view.data();
    for (std::size_t i = 0; i < take; ++i) {
        dst[i] = src[i];                 // 无分支,GCC 自行切块拷
    }
    pos_ += take;
    if (take < view.size()) {
        truncated_ = true;               // 截断标记只此一处
    }
}
```

## 约束要镜像函数体的实际表达式

最后一个小而深的点。`TextSource` 概念（"这个东西能当日志文本用"）在两处被消费：`Tag` 的构造函数和 `LineBuffer` 的文本分发重载。两边函数体里的实际表达式不一样——`Tag` 是 `std::forward<S>(s)`（表达式类型 `S&&`），`append` 是 `std::string_view{s}`（`s` 是按值形参，在函数体里是**左值**）。概念如果写成 `convertible_to<S&&, string_view>`，在 `append` 那边就可能出现"约束放行、函数体编译不过"的缝隙。

我们的取舍：**一个库一个概念，取交集语义**——`TextSource = convertible_to<const S&, string_view>`，即"作为左值也能转"。它在两边的函数体上都是精确镜像，唯一被拒之门外的是只提供 `&&` 限定转换的病态类型，而那些类型本来就该走 `append_to` 扩展点。教训提炼成一句话：**约束跟着函数体的实际表达式走，概念重用成立的前提是两个消费方的表达式语义一致**——先看函数体，再定概念，就不会长出第二份变体。

## 注意事项与常见错误

| 症状 | 原因 | 解法 |
|---|---|---|
| `cannot convert '"tag"' to 'const std::source_location&'` | 默认实参首参 + 参数包 | Tag 约束模板构造吸收位置 |
| `cannot convert 'const char[N]' to 'LogTag'` | 链上两次用户定义转换 | 同上，一次转换直达 |
| 裁剪级别日志的字符串还在 `.rodata` | 缺 `-ffunction-sections -fdata-sections` | 补齐 flag 链，map 文件复查 |
| `invalid use of 'this' at top level` | 约束里写了 `*this` | `std::declval<LineBuffer&>()` |
| `call of overloaded 'append(int)' is ambiguous` | 模板重载形参形式混用 | 统一按值（铁律一） |
| `append('x')` 输出 `120` | `char` 掉进整数重载 | 定向 `= delete` 拒单字符 |
| 日志里文件名是绝对路径 | 没配 `-ffile-prefix-map` | 加 map 选项，路径变相对 |
| arm-gcc 16.2 显式实例化直接 ICE | 工具链 bug | 用 `__attribute__((noinline))` 包装函数看代码 |

## 小结

- `source_location` 与参数包天生相克，Tag 转换构造是无宏的正解，一次用户转换是语言的红线；
- "零开销"要验证到**字符串零残留**的粒度，`-O3` + 两个 sections flag + `--gc-sections` 缺一不可，改 flag 前先记 size 基线；
- 分发重载集两条铁律：模板形参一律按值、约束里用 `declval` 不用 `this`；
- `= delete` 带理由的定向拒绝，报错质量优于 `static_assert`，每种拒绝一行原因；
- `to_chars` 三坑：`char` 当整数是静默错误、`bool` 无重载、浮点拖查表直接拒；
- 拷贝循环钳制一次再整段搬，M3 上 ~9 倍指令差，反汇编是唯一可信的验收；
- 概念统一的前提是"约束镜像函数体"，两个消费方表达式语义一致才共用一个概念。

模块还在 libestdx 里持续落地（本文覆盖词汇层与行组装层，logger 核心、sink 适配与 Renode 全链路验证在后续篇），代码见 [libestdx/logger/](https://github.com/Charliechen114514/libestdx/tree/main/include/libestdx/logger)，欢迎围观施工进度。

## 参考资源

- [std::source_location - cppreference](https://en.cppreference.com/w/cpp/utility/source_location)
- [Non-terminal variadic template parameters — Corentin Jabot](https://cor3ntin.github.io/2020/06/18/nonterminal/)
- [Non-Terminal Variadic Parameters and Default Values - C++ Stories](https://www.cppstories.com/2021/non-terminal-variadic/)
- [std::to_chars - cppreference](https://en.cppreference.com/w/cpp/utility/to_chars)
- [libestdx 仓库](https://github.com/Charliechen114514/libestdx)
