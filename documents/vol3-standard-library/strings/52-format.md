---
chapter: 7
cpp_standard:
- 20
- 23
description: 讲透 std::format——Python f-string 风格的编译期类型安全格式化，格式串语法、format_string 的编译期校验为什么比 printf 安全、format_to 写缓冲、与 printf/iostream 的性能与类型安全双维度实测对比，以及 C++23 的 print 与运行期 width/precision 参数
difficulty: intermediate
order: 52
platform: host
prerequisites:
- string 深入：SSO、COW 与 resize_and_overwrite
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
reading_time_minutes: 16
related:
- print 与 println：直接消费 format 的快捷输出
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'format：C++20 的类型安全格式化'
---

# format：C++20 的类型安全格式化

把一个 `int`、一个字符串、一个浮点数拼成一行可读文本，是每个 C++ 程序都要干的事。这件事标准库给过两条路，但两条都让人不痛快：`printf` 快但类型不安全，`iostream` 安全但慢且啰嗦。`std::format`（C++20）就是来填这个缺口的——它用 Python f-string 风格的占位符语法，把类型检查挪到编译期，既保留了 `printf` 的格式串表达力，又不掉进运行期未定义行为的坑。

这一篇我们把 `std::format` 拆开讲透：格式串到底怎么写、它凭什么能在编译期就把错误类型挡下来、怎么往缓冲里写、跟 `printf` 和 `iostream` 比到底快多少慢多少，最后看看 C++23 又补了哪些东西。C++23 的 `std::print` / `std::println` 本身是另一个故事，我们留到下一篇单独讲，这里只把它当成 `std::format` 的直接消费者顺带提一下。

## 先看痛点：printf 和 iostream 各差在哪里

用 `printf` 拼一行日志，几乎是所有项目的肌肉记忆，但它有两个老毛病。

第一个是类型不安全。`printf` 的格式串（`%d` / `%s` / `%f`）和后面的实参之间，编译器**不强制对齐**——你写 `%s` 却传一个 `int`，照样编过。我们试一段：

```cpp
// Standard: C++20
#include <cstdio>

int main() {
    // %s 期望 char*，却传了 int —— 编译通过，运行期 UB
    std::printf("value = %s\n", 42);
    return 0;
}
```

用 GCC 16.1.1 开 `-Wall -Wextra` 编，它**只给一个 warning**（`format '%s' expects ... but argument has type 'int'`），不是 error。然后真跑起来：

```text
$ ./printf_ub
Segmentation fault (core dumped)   # exit code 139 = SIGSEGV
```

`42` 被当成指针去解引用，直接段错误。这就是运行期 UB——编译器帮你看了一眼、提醒了一下，但你不理它，它也照编不误。

而且这个 `-Wformat` 提醒**只对字符串字面量生效**。一旦格式串是运行期拼出来的，编译器连看都看不到，warning 直接消失：

```cpp
// Standard: C++20
#include <cstdio>
#include <string>

int main(int argc, char**) {
    std::string fmt = (argc > 0) ? "value = %s\n" : "value = %d\n";
    std::printf(fmt.c_str(), 42);   // 同样是 UB，这次连 warning 都没有
    return 0;
}
```

`-Wall -Wextra` 下这段**一片干净**，没有任何提示。项目里只要有一处日志把用户输入拼进格式串，类型检查就彻底失守。

`iostream` 倒是类型安全，但代价是又啰嗦又慢。拼刚才那句 `"id=1 name=alice score=3.14"`：

```cpp
std::ostringstream oss;
oss << "id=" << 1 << " name=" << "alice" << " score=" << 3.14;
std::string s = oss.str();
```

每个值都要单独 `<<` 一次、运算符重载层层跳、`ostringstream` 内部还要维护格式化状态——这套机制的代价，我们后面用真实 benchmark 量一下，先记着它「公认慢」。

`std::format` 的立意就是把这两边的优点捏一块：像 `printf` 那样用紧凑的格式串表达输出意图，但把「占位符和实参类型对不对」的检查挪到编译期，编不过就别想跑。

## 上手：格式串长什么样

先跑一个最小的，感受一下语法：

```cpp
// Standard: C++20
#include <format>
#include <iostream>

int main() {
    std::cout << std::format("Hello {} {} {}\n", "world", 42, 3.14);
    return 0;
}
```

```text
Hello world 42 3.14
```

`{}` 就是一个占位符，按顺序吃后面的参数。不用关心类型，`std::format` 在编译期已经知道每个参数是什么，到运行期直接按正确的方式格式化。

### 位置参数：同一个参数用多次

`{}` 默认按顺序吃参数。想打乱顺序、或者同一个参数用多次，就给占位符带编号——`{0}` 是第一个参数、`{1}` 是第二个：

```cpp
std::cout << std::format("{1} before {0}\n", "B", "A");
```

```text
A before B
```

位置参数最实在的用处是国际化场景——不同语言的语序不一样，「{0} 的 {1}」和「{1} of {0}」可能要复用同一组参数，翻译时只动格式串、不动调用代码就行。一旦用了位置参数，同一个串里**所有**占位符都得带编号，不能位置和自动混着写。

### 格式说明：`{:` 之后的那一坨

真正让 `std::format` 接近 `printf` 表达力的，是 `{:` 后面可以带的**格式说明**。完整语法是 `{:fill align width .prec type}`，看着吓人，拆开一层层看就清楚了。

**对齐与填充**：`<` 左对齐、`>` 右对齐、`^` 居中，`{}` 之前还可以带一个填充字符。配宽度用：

```cpp
std::cout << std::format("[{:>10}]\n", "right");
std::cout << std::format("[{:<10}]\n", "left");
std::cout << std::format("[{:^10}]\n", "center");
std::cout << std::format("[{:*^10}]\n", "x");
```

```text
[     right]
[left      ]
[  center  ]
[****x*****]
```

**精度与类型**：浮点保留几位小数用 `.N`，整数的进制用类型字符 `b`/`o`/`x`：

```cpp
std::cout << std::format("{:.3f}\n", 3.14159);   // 浮点保留 3 位
std::cout << std::format("{:b}\n", 42);          // 二进制
std::cout << std::format("{:#x}\n", 255);        // 带 0x 前缀的十六进制
std::cout << std::format("{:#o}\n", 8);          // 带 0 前缀的八进制
std::cout << std::format("{:c}\n", 65);          // 当字符输出
```

```text
3.142
101010
0xff
010
A
```

这些说明还能组合，组合顺序得照着 `fill align width .prec type` 来。下面两个组合很常用：左对齐用 `-` 填充、带符号的零填充：

```cpp
std::cout << std::format("[{:-<8}]\n", 42);    // 左对齐，填 '-'
std::cout << std::format("[{:+08}]\n", 42);    // 强制正号 + 零填充
```

```text
[42------]
[+0000042]
```

格式说明还有不少边角（`{:.5}` 作用在字符串上会截断长度、`{:e}` 科学计数法等等），我们不需要背全表——核心记住 `fill align width .prec type` 这个骨架，剩下的查 cppreference 就行。关键是理解：**所有这些都和参数类型绑定，类型不对会在编译期直接挡掉**。我们这就来看这是怎么做到的。

## 编译期类型检查：format_string 是怎么挡错的

这是 `std::format` 区别于 `printf` 最核心的一块。回到开头那个 `%s` 配 `int` 的例子，换成 `std::format`：

```cpp
// Standard: C++20
#include <format>
#include <iostream>

int main() {
    std::cout << std::format("{:d}", "not a number");
    return 0;
}
```

这次 GCC 16.1.1 **编译直接报错**，不是 warning：

```text
t2_compile.cpp:7:30: error: call to consteval function
  'std::basic_format_string<char, const char (&)[13]>("{:d}")'
  is not a constant expression
...
format:1609:48: error: call to non-'constexpr' function
  'void std::__format::__failed_to_parse_format_spec()'
```

报错信息长得吓人，但意思很明确：格式说明 `d`（整数）配不上参数类型 `const char[13]`（字符串），格式串解析失败，**编译就过不了**。

参数个数不对同理。两个占位符只给一个参数：

```cpp
std::cout << std::format("{} {}", 1);   // 2 个占位符，1 个参数
```

```text
t3_args.cpp:4:30: error: call to consteval function
  'std::basic_format_string<char, int>("{} {}")' is not a constant expression
format:322:56: error: call to non-'constexpr' function
  'void std::__format::__invalid_arg_id_in_format_string()'
```

同样编译失败。这块的机制值得拆开看一眼，因为它解释了「为什么非要是字面量」。

### format_string：一个 consteval 的看门人

`std::format` 的第一个参数类型不是 `const char*`，而是 `std::format_string<Args...>`。这个类型有个关键设计：它的构造函数是 `consteval` 的——也就是说，**构造它这件事本身必须能在编译期完成**。

```cpp
// 大致是这个意思（简化伪码，不是标准库真身）
template <typename... Args>
struct basic_format_string {
    const char* str;

    // consteval 构造函数：编译期就跑格式串解析
    template <typename T>
    consteval basic_format_string(const T& s) : str{s} {
        // 编译期扫描格式串，对每个占位符校验：
        //  - 参数下标越界？报错
        //  - 格式说明对这个参数类型合法吗？不合法报错
        constant_expression_check(s, std::make_format_checker<Args...>());
    }
};
```

构造函数里那个「扫描 + 校验」的过程，正是把格式串和实参类型逐一比对的检查器。因为整个构造是 `consteval` 的，它只能在编译期常量上下文里发生——而字符串字面量恰好是编译期常量。于是把一个「格式串和实参类型不匹配」的运行期 bug，硬生生挡成了编译期 error。

::: warning 格式串必须是字面量
`format_string` 的 `consteval` 构造决定了：格式串**必须是编译期常量**。下面这种写法编不过，因为 `runtime_fmt` 不是常量：

```cpp
std::string runtime_fmt = read_from_config();
std::format(runtime_fmt, 42);   // error: 不是常量表达式
```

真正的运行期格式串要走另一条路（下面的 `std::vformat`），那条路**没有编译期校验**，自己保证类型对得上。这其实是有意的取舍：标准库把「快、安全」的常用路径做成编译期校验，把「我确实需要运行期串、风险自担」的逃生通道单独留出来，不让前者被后者拖累。
:::

### 运行期格式串：vformat 那条逃生通道

当你真的从配置文件、用户输入里读来一个格式串时，`std::format` 用不了，得走 `std::vformat`。它跳过编译期校验，运行期解析：

```cpp
// Standard: C++20
#include <format>
#include <iostream>
#include <string>

int main() {
    std::string runtime_fmt = "x={}, y={}";
    int a = 1, b = 2;
    // vformat：运行期格式串 + make_format_args 打包的参数；没有编译期校验
    std::string s = std::vformat(runtime_fmt, std::make_format_args(a, b));
    std::cout << s << '\n';
    return 0;
}
```

```text
x=1, y=2
```

注意 `make_format_args` 在 C++20 里要传**左值**（`a`、`b`，不能直接写 `1`、`2`），这是标准里一个为人诟病的坑——LWG 3631 已经在 C++23 里把它改成 `const&`，允许传右值了。我们在本机 GCC 16.1.1（libstdc++）实测，C++23 模式下传右值**仍然编不过**，说明这个缺陷修订在当前 libstdc++ 还没落地。所以暂时还是老老实实传左值最稳，别被老资料里「C++23 可以传右值了」带跑。

`vformat` 这条路是你写自己的国际化日志框架、`fmt::runtime` 风格接口时才会碰的。日常 99% 的场景，用字面量格式串走 `std::format` 就够了，类型安全是白送的。

## format_to：往缓冲里直接写

`std::format` 每次返回一个 `std::string`，意味着一次堆分配。如果你想往已有的缓冲里写、避免分配，用 `std::format_to`——它把结果写到一个输出迭代器，更像 `printf` 的 `snprintf` 那种「写到这块内存」的用法。

最自然的搭配是上一篇讲过的 `std::back_inserter`，往 `std::string` 追加：

```cpp
// Standard: C++20
#include <format>
#include <iostream>
#include <string>
#include <iterator>

int main() {
    std::string buf;
    std::format_to(std::back_inserter(buf), "a={} ", 1);
    std::format_to(std::back_inserter(buf), "b={} ", 2);
    std::cout << "buf = [" << buf << "]\n";
    return 0;
}
```

```text
buf = [a=1 b=2 ]
```

这正是「适配器 + 算法」协作的又一个例子：`format_to` 只认输出迭代器接口，`back_inserter` 把「赋值」翻译成 `push_back`，两者一咬合，写 `string` 就跟写流一样顺。

如果目标是固定大小的 `char` 数组（嵌入式里常见，想避免任何堆分配），直接把数组首地址当迭代器传进去。但数组不会自动扩容，写超了就越界——这时候用 `std::format_to_n`，它额外接受一个最大字符数，保证不越界，还能告诉你是否被截断：

```cpp
// Standard: C++20
#include <format>
#include <iostream>

int main() {
    char cbuf[8];
    auto res = std::format_to_n(cbuf, sizeof(cbuf) - 1, "long number {}", 123456789);
    *res.out = '\0';   // res.out 指向写入的末尾，手动补 '\0'
    std::cout << "cbuf = [" << cbuf << "]\n";
    std::cout << "total size = " << res.size
              << ", truncated = " << std::boolalpha
              << (res.size > static_cast<int>(sizeof(cbuf)) - 1) << '\n';
    return 0;
}
```

```text
cbuf = [long nu]
total size = 21, truncated = true
```

`res.size` 是**完整**格式化后的长度（21），`res.out` 是实际写进缓冲的末尾位置。两者一比就知道有没有被截断——这里 21 远大于缓冲容量 7，结果被截成了 `long nu`。做固定缓冲的日志、协议帧拼接时，这个 `format_to_n_result` 是你判断「这条日志放不放得下」的依据。

顺带一提，如果你只想知道格式化后多长、不实际写，有 `std::formatted_size`：

```cpp
std::cout << std::formatted_size("{}-{}\n", 100, 200);   // 7
```

预分配缓冲时用它算一次容量、再 `format_to` 写进去，能避免 `std::string` 内部的二次扩容。

## 实测：format 对比 printf 对比 iostream

口说无凭，我们真跑一遍。同一行日志（`id=N name=alice score=3.14`）循环 100 万次，分别用 `printf` / `std::format` / `std::format_to`（写定长 `char` 缓冲）/ `iostream`（`ostringstream`），量总耗时。完整 benchmark 在 `/tmp/fmt/bench.cpp`，用 `g++ -std=c++23 -O2`（本机 GCC 16.1.1）编。

```text
--- run 1 ---
printf    : 129121 us   (0.13 us/iter)
format    : 164247 us   (0.16 us/iter)
format_to : 154787 us   (0.15 us/iter)
iostream  : 304199 us   (0.30 us/iter)
(sink=0)
--- run 2 ---
printf    : 135027 us   (0.14 us/iter)
format    : 180146 us   (0.18 us/iter)
format_to : 152233 us   (0.15 us/iter)
iostream  : 442312 us   (0.44 us/iter)
(sink=0)
```

几个稳健的结论（绝对微秒值会随机器抖动，只看数量级和相对关系）：

- **`printf` 最快**，因为它的格式串解析是手写状态机、参数走 varargs，开销最小，但代价就是前面说的类型不安全。
- **`std::format` / `format_to` 紧跟其后**，大约是 `printf` 的 1.1–1.4 倍。`format_to` 往 `char` 缓冲写、不分配堆，比返回 `std::string` 的 `std::format` 还略快一点。在「类型安全 + 接近 printf 性能」这个维度上，`std::format` 是有明显优势的。
- **`iostream` 明显最慢**，大约是 `printf` 的 2–3 倍，而且抖动大（`ostringstream` 反复构造析构、`<<` 运算符层层跳、维护格式化状态都拖后腿）。日志热路径上用 `ostringstream` 拼串，是真亏。

所以结论很清楚：**要安全又不想慢，用 `std::format`**。只有在已经被类型检查卡死、又极端在意那点性能的角落（比如超高频的紧凑日志），`printf` 才有保留价值，而且那种场景通常更该考虑直接把日志砍掉。`iostream` 拼格式化串，在性能敏感的地方该淘汰了。

## C++23 给 format 补了什么

C++23 围绕格式化做了两件值得一提的事，都让 `std::format` 更顺手。

### 第一件：运行期 width / precision 作为参数（P2636）

C++20 里，宽度和精度必须写死在格式串里：`{:>10}`、`{:.3f}`。但实战中常常要「按某列宽度对齐」「精度从配置来」，宽度是运行期才知道的。C++20 的绕法是 `std::vformat` + 自己拼串，又丑又丢编译期检查。

P2636 给格式说明开了「嵌套占位符」：宽度和精度位置可以再写一个 `{}`，从后面的参数里取值。GCC 16.1.1 已经支持：

```cpp
// Standard: C++23
#include <print>
#include <iostream>

int main() {
    int width = 8;
    int prec = 2;
    std::println("[{:>{}}]", 42, width);          // 宽度从参数取
    std::println("[{:.{}}]", 3.14159265, prec);    // 精度从参数取
    return 0;
}
```

```text
[      42]
[3.1]
```

`{:>{}}` 第一个 `{}` 是占位符主体、第二个 `{}` 是宽度——`width=8` 被填进去，等价于 `{:>8}`。`{:.{}}` 同理，精度从 `prec` 取。注意这仍然保留编译期检查：嵌套进去的那个参数会被要求是整数类型，类型不对照编不过。

### 第二件：print / println 直接消费 format（下一篇主角）

`std::format` 返回的是 `std::string`，要输出到终端还得再套一层 `std::cout << ...`，多一次拷贝。C++23 的 `std::print` / `std::println`（`<print>` 头）直接接受 `std::format` 的格式串和参数，内部流式写出去，省掉中间那个 `std::string`：

```cpp
// Standard: C++23
#include <print>

int main() {
    std::println("Hello {} = {}", "x", 42);   // 自动带换行
    std::print("[no newline]");
    return 0;
}
```

```text
Hello x = 42
[no newline]
```

`println` 自带末尾换行、`print` 不带，语法和 `std::format` 完全一致——同一套格式串、同一套编译期类型检查，只是输出目标从「返回 string」变成「直接写流」。`print` 怎么选目标流、怎么跟 `sync_with_stdio(false)` 配合、性能上比 `cout` 好在哪，这些是下一篇（`std::print` 专篇）的内容，这里就不展开了。

::: warning 老 GCC 上 print 可能没有
`std::print` / `std::println` 需要 `<print>` 头和较新的 libstdc++。GCC 13 之前基本没有，GCC 14 起逐步可用。本机 GCC 16.1.1 实测 `<print>` 完整可用（`println`、`print`、`vprint` 都在）。如果你的项目要支持老工具链，`std::format` 本身（GCC 13 起）覆盖面比 `std::print` 广得多，跨工具链更稳。下沉到老环境时，常用 `fmt` 库作为 polyfill——它正是 `std::format` 的原型，API 几乎一致。
:::

## 自定义 formatter：给自定义类型加格式支持

`std::format` 开箱支持内置类型（整数、浮点、字符串、指针）。遇到自定义类型，默认是编不过的——`std::format("{}", my_point)` 会报「没有匹配的 formatter」。要让自己的类型也能塞进 `std::format`，给 `std::formatter` 写一个特化就行。

这里我们只点到一个最小用法——给一个 `Point` 加上「格式化成 `(x, y)`」的能力，不展开 formatter 解析器的完整实现（那本身可以单独写一篇）。最小特化只要实现两个函数：

```cpp
// Standard: C++20
#include <format>
#include <iostream>
#include <string>

struct Point {
    int x{};
    int y{};
};

// 给 Point 加格式化支持：特化 std::formatter<Point>
template <>
struct std::formatter<Point> {
    // 解析格式串里 {} 之间的说明部分；这里不认任何说明，直接接受
    constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    // 真正输出：把 Point 写成 "(x, y)"
    auto format(const Point& p, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "({}, {})", p.x, p.y);
    }
};

int main() {
    Point p{3, 4};
    std::cout << std::format("point = {}\n", p);
    std::cout << std::format("two points: {} and {}\n", Point{1, 1}, Point{9, 9});
    return 0;
}
```

```text
point = (3, 4)
two points: (1, 1) and (9, 9)
```

两个函数的分工很清晰：

- `parse` 负责吃掉 `{}` 之间的格式说明（比如 `{:>10}` 里的 `:>10`）。这里我们不支持任何说明，直接返回 `ctx.begin()` 表示「啥也没消费」。一旦你想让 `Point` 支持 `{:>10}` 这种对齐，就得在 `parse` 里解析、在 `format` 里应用——标准库内置 formatter 都是这么实现的。
- `format` 负责把值写出去。它拿到的 `ctx.out()` 是一个输出迭代器，我们复用 `std::format_to` 把 `(x, y)` 写进去就行。注意这里 `format_to` 里还能再嵌套用 `{}`，因为 `int` 是开箱支持的。

这个模式的妙处在于：**一旦你给自己的类型写了 formatter，它就能进任何接受 `std::formattable` 的地方**——不光是 `std::format`，C++23 的 `std::print`、`std::format_to`、日志框架、range 的格式化（C++23 的 `std::formatter<std::range>`）都能直接用，不用改这些组件一行代码。这是「标准化的扩展点」带来的红利，和「给容器写 `operator<<`」那种各自为政的做法相比，收敛得多。

## 几个真实容易踩的点

把这一路容易翻车的位置集中收一下，每个都对应上面的实测：

::: warning 格式串必须是字面量
`std::format` 的格式串必须是编译期常量。运行期才知道的串（配置文件、用户输入）走 `std::format` 会编译失败，得用 `std::vformat`，但那条路**没有编译期类型检查**，类型对不上的锅回到你自己头上。
:::

::: warning make_format_args 要传左值（C++20）
`std::vformat` 配 `std::make_format_args` 时，C++20 标准下参数必须是左值，传右值（字面量 `1`、`"str"`）编不过。LWG 3631 在 C++23 里改成 `const&` 允许右值，但本机 GCC 16.1.1（libstdc++）实测**尚未落地**，C++23 模式下传右值仍报错。暂时一律传左值最稳。
:::

::: warning format_to_n 的 res.size 是完整长度
`std::format_to_n` 返回的 `result.size` 是「如果没有截断会写多长」，不是「实际写了多长」。判断是否截断要用 `size > 容量`，不能用 `size` 当写入长度——真要拿实际写入位置，看 `result.out`。
:::

::: warning 数字千位分隔符在 libstdc++ 还没实现
格式说明里的 `,`（千位分隔符）按 P2931 是 C++26 才标准化的，libstdc++ 16.1.1 还没实现，`std::format("{:,}", 1234567)` 会**编译报错**（解析失败）。需要本地化数字分组的话，当前只能自己后处理或等 C++26 的 `L` 选项落地。别被老资料里 `{:,}` 能用的说法带偏。
:::

## 小结

`std::format` 的立意一句话能说清：**把 printf 的格式串表达力、iostream 的类型安全、Python f-string 的简洁语法，三合一**。几条关键结论收一下：

- 格式串语法：`{}` 占位符，按顺序或 `{0}{1}` 位置取参数；`{:` 之后是 `fill align width .prec type` 格式说明，控制对齐、宽度、精度、进制。
- 编译期类型检查是核心价值：`format_string` 的 `consteval` 构造把「格式说明 vs 实参类型」「占位符数 vs 实参数」的不匹配全部挡成编译期 error，而 `printf` 同样的错只是运行期 UB。
- 格式串必须是字面量；真要运行期串走 `std::vformat` + `make_format_args`，代价是没有编译期检查（C++20 还得传左值）。
- 写缓冲用 `format_to`（配 `back_inserter` 写 `string`、或裸指针写 `char` 缓冲），定长防越界用 `format_to_n`，只想知道长度用 `formatted_size`。
- 性能：`format` / `format_to` 紧追 `printf`（约 1.1–1.4 倍），`iostream` 明显最慢（2–3 倍）。要安全又不想慢，选 `format`。
- C++23 新增：P2636 让 width/precision 能从参数取（`{:>{}}`）；`std::print` / `std::println` 直接消费格式串输出，省掉中间 `std::string`——后者是下一篇的主角。
- 自定义类型：特化 `std::formatter`（实现 `parse` + `format`），类型就进所有 formattable 接口，不用改消费方。

下一篇我们专门讲 `std::print` / `std::println`——它怎么直接吃 `std::format` 的格式串、输出目标怎么选、和 `std::cout` 比性能好在哪里，把格式化这条线收完。

## 参考资源

- [cppreference: std::format](https://en.cppreference.com/w/cpp/utility/format/format) —— 主接口与格式串语法
- [cppreference: std::format_string](https://en.cppreference.com/w/cpp/utility/format/basic_format_string) —— 编译期校验机制（consteval 构造）
- [cppreference: std::formatter](https://en.cppreference.com/w/cpp/utility/format/formatter) —— 自定义类型的扩展点
- [cppreference: std::format_to_n](https://en.cppreference.com/w/cpp/utility/format/format_to_n) —— 定长缓冲写入与截断判定
- [P2636R4](https://wg21.link/p2636) —— C++23 运行期 width/precision 作为参数
- [{fmt} 库](https://github.com/fmtlib/fmt) —— `std::format` 的原型，跨工具链 polyfill
