---
chapter: 7
cpp_standard:
- 17
- 20
description: 讲透 charconv 的 from_chars/to_chars 为什么是标准库最快的数字字符串互转路径——无 locale、无异常、无分配、错误用返回码表达，并用真实 benchmark 画出与 stoi/to_string/snprintf 的数量级差距
difficulty: intermediate
order: 51
platform: host
prerequisites:
- string 深入：SSO、COW 与 resize_and_overwrite
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
reading_time_minutes: 14
related:
- char8_t 与 UTF-8 字符串
tags:
- host
- cpp-modern
- intermediate
- 优化
title: charconv：零开销的数字与字符串互转
---

# charconv：零开销的数字与字符串互转

数字和字符串的互转，大概是标准库里最日常、也最容易被随手写糊的一个角落。要格式化一个 `int`，大家第一反应是 `std::to_string`；要解析一个 `int`，又第一反应去够 `std::stoi` 或 `std::atoi`。多数场景下这没毛病——能跑、够清楚。可一旦把视线挪到性能敏感的地方（高频日志、序列化、协议解析、CSV/JSON 之类的东西里那成千上万次的字段转换），这俩老伙计就立刻露馅：`to_string` 每次都给你 `new` 一个 `std::string` 出来，`stoi` 不光得经过一整套 locale 查找，还可能抛异常。

C++17 给我们送来了一组专门干这活的原语：`<charconv>` 里的 `from_chars` 和 `to_chars`。它俩的设计目标只有一个——**做标准库里最快的数字↔字符串互转**。为了做到这一点，标准委员会狠心砍掉了一堆"便利"特性：不读格式串、不分配、不依赖 locale、不抛异常、甚至连前导空白都不帮你跳。换回来的，是几倍到十几倍的速度。这一篇我们就把这套原语拆开跑一遍，重点是讲清楚它为什么这么设计、怎么用才不会踩坑，并用真实 benchmark 量化它能快多少。

## 为什么它能快：四个"不做"

要看懂 `charconv` 的快，得先看它替我们砍掉了什么。我们拿最常用的 `std::stoi("42")` 来对照：

- `stoi` 内部要查 **locale**（地区设置）。哪怕你什么都不配，C locale 里的"小数点是句号"这种规则也得走一遍查找链路，因为标准得允许德语 locale 把小数点写成逗号。
- `stoi` 的错误处理走 **异常**。解析失败时它 `throw std::invalid_argument`，溢出时 `throw std::out_of_range`。异常机制本身在正常路径上开销不大，但它逼得实现里得保留一堆"出错怎么收拾"的分支。
- `stoi` 接收的是 `const std::string&`，结果也得先有个地方落——解析过程里往往伴随临时构造。
- `to_string` 反过来，**每次都返回一个新 `std::string`**，意味着一次堆分配（短串走 SSO 能省，但 `int` 转出来动辄十几位，多半过 SSO 阈值）。

`charconv` 把这四样全砍了：

1. **无 locale**：`from_chars` / `to_chars` 的行为对全世界所有 locale 都一样，不查任何表。小数点永远是句号。
2. **无异常**：出错信息塞进返回值里的一个 `std::errc`，正常路径完全不碰异常机制。
3. **无分配**：`to_chars` 不返回 `string`，它直接往**你给的缓冲区**里写字节；`from_chars` 直接读你给的字符区间，解析结果写进你传进来的变量。
4. **无格式串**：没有 `"%d"`、`"%.6f"` 这种东西要解析。整数就是整数，浮点数的格式（科学计数 / 定点 / hex）用一个枚举传，编译期就定死了。

这四刀下去，换来的就是"裸转换"——除了把数字的二进制表示和 ASCII 字符做映射，几乎不做别的事。代价是你得自己管缓冲、自己判返回码、自己处理前导空白。我们接下来一条条看怎么把这些事情做对。

## to_chars：往你的缓冲里直写

先看整数版。`to_chars` 的签名长这样（整数）：

```cpp
// Standard: C++17
struct to_chars_result {
    char* ptr;            // 写完后的下一个位置（尾后指针）
    std::errc ec;         // 成功时为 {}（即 0）
};

to_chars_result to_chars(char* first, char* last, int value, int base = 10);
```

它接收一段**你分配好的**字符缓冲 `[first, last)`，把 `value` 写进去，返回写到了哪里。注意返回的 `ptr` 是尾后指针——这意味着你拿它能立刻算出写了几个字节（`ptr - first`），也意味着**它不写 null 结尾**。这点和 `sprintf` 截然不同，`sprintf` 会补一个 `\0`，`to_chars` 不会，因为它不想浪费那一个字节、也不想假设你要拿这段字符当 C 字符串用。

最小用法，把一个 `int` 转出来再塞回 `std::string`：

```cpp
// Standard: C++17
#include <charconv>
#include <string>

char buf[16];
int value = 12345678;
auto res = std::to_chars(buf, buf + sizeof(buf), value);
// res.ptr 指向写完后的下一个位置；没有 '\0'
std::string out(buf, res.ptr);   // 用 [buf, res.ptr) 这段构造，恰好 8 个字符
```

跑一下确认：

```text
to_chars int: 12345678
```

`to_chars` 比较容易踩的第一个坑就在"不写 `\0`"上。如果你习惯性地把 `buf` 直接当 C 字符串用（`printf("%s", buf)` 或者 `std::string(buf)`），多半会读到后面未初始化的垃圾直到撞上一个偶然的 `\0`。正确做法永远是用 `(buf, res.ptr)` 这对迭代器/指针来界定你刚写的那段。

::: warning to_chars 不写 null 结尾
`to_chars` 写完数字就停，不追加 `\0`。需要 C 字符串就自己 `*res.ptr = '\0';`（前提是缓冲还剩至少一字节），需要 `std::string` 就用 `std::string(buf, res.ptr)` 构造。直接拿 `buf` 当字符串用必踩雷。
:::

整数的 `to_chars` 还支持 `base` 参数（2 到 36），想要二进制、十六进制、三十二进制都行：

```cpp
// Standard: C++17
char buf[32];
auto r = std::to_chars(buf, buf + sizeof(buf), 255, 16);   // 十六进制
// [buf, r.ptr) = "ff"
```

### 缓冲不够大怎么办

`to_chars` 的错误处理很克制：**只有一种出错情况**，就是你给的缓冲装不下。这时候 `ptr` 被设成 `last`（缓冲尾后），`ec` 设成 `std::errc::value_too_large`：

```cpp
// Standard: C++17
char buf[3];
auto r = std::to_chars(buf, buf + sizeof(buf), 123456);
// r.ptr == buf + 3 (== last), r.ec == std::errc::value_too_large
```

实测一下 GCC 16.1.1 的行为：

```text
to_chars 123456 into buf[3]: ec==value_too_large? 1 ptr==last? 1
```

所以判成功就一句：`if (res.ec == std::errc{})`（或者等价的 `if (!res.ec)`）。整数不会因为数值本身出错——任何 `int` 都能转出来——所以你只要保证缓冲够大就行。一个稳妥的上界：`int` 十进制最多 11 位（含负号），`std::numeric_limits<int>::digits10 + 2` 给的缓冲对整数绝对够。

## from_chars：读一段字符，填一个变量

反方向的 `from_chars` 签名：

```cpp
// Standard: C++17
struct from_chars_result {
    const char* ptr;       // 解析停下的位置
    std::errc ec;          // 成功为 {}; 失败为 invalid_argument 或 result_out_of_range
};

from_chars_result from_chars(const char* first, const char* last,
                             int& value, int base = 10);
```

它读 `[first, last)` 这段字符，把解析出来的数写进 `value`，返回停在哪里。成功时 `ptr` 指向**第一个没被识别为数字的字符**——这个设计很实用，意味着你拿 `ptr` 就能继续往后解析下一个字段，解析和"剩下多少没读"一次性给你了。

最小用法：

```cpp
// Standard: C++17
std::string s = "42abc";   // 注意后面跟了非数字
int v = 0;
auto res = std::from_chars(s.data(), s.data() + s.size(), v);
// res.ec == {}, v == 42, res.ptr 指向 'a'（s.data()+2）
```

实测：

```text
from_chars '42abc' -> value=42 ptr-offset=2 ec=0
```

`ptr` 停在偏移 2，正好是 `'a'` 的位置——数字部分被吃掉了，剩下的原样留着。这就比 `stoi` 那套"再传一个 `size_t* pos` 出来告诉你停在哪"要顺手。

`from_chars` 有两类失败，分别对应两个错误码：

- **`std::errc::invalid_argument`**：输入压根不是数字（比如 `"abc"`，或者空区间）。
- **`std::errc::result_out_of_range`**：输入是合法数字，但超出了目标类型的范围（比如把 `"999999999999999999999"` 往 `int` 里塞）。

实测溢出：

```text
from_chars overflow int -> ec is err=1
```

还有一个细节值得记住——**出错时 `value` 保持不变**。`from_chars` 在解析失败时不会动你传进去的变量。这点下面踩坑那节还会用到。

### from_chars 不跳前导空白

这是和 `stoi` / `strtod` 行为差最大的一个点，也是最常被忽略的坑。`from_chars` **不跳过任何前导空白**——它要求输入的第一个字符就得是数字（或符号）。前面有空格？直接判定为 `invalid_argument`。

我们拿带前导空白的 `"   42"` 对照测一下 `from_chars` 和 `stoi`：

```cpp
// Standard: C++17
std::string s = "   42";
int v = -1;
auto r = std::from_chars(s.data(), s.data() + s.size(), v);
// r.ec == std::errc::invalid_argument, v 仍是 -1（没被改）

size_t idx = 0;
int sv = std::stoi(s, &idx);
// sv == 42, idx == 5 —— stoi 主动跳过了前导空白
```

实测对比：

```text
from_chars '   42': ec-ok=0 value=-1 (v unchanged on err)
stoi '   42': value=42 consumed=5 (skips leading ws)
```

`from_chars` 返回失败、`v` 原封不动是 `-1`；`stoi` 却乐呵呵地跳过空格解析出了 42。这不是 `from_chars` 的 bug，而是刻意的取舍：跳空白是 locale 相关的（什么算空白得查 `isspace` 表），跳了就违背了"无 locale"的设计目标。所以用 `from_chars` 解析用户输入或文件字段时，**你自己先把前导空白 `trim` 掉**，或者用 `std::find_if` 找到第一个非空白字符再喂给它。

::: warning from_chars 不跳前导空白
`from_chars` 要求输入首字符就是数字或符号，前导空白直接判 `invalid_argument`。这和 `stoi` / `strtod` 主动跳空白的行为相反。解析带空白的输入（用户键入、CSV 字段）要自己先 trim。
:::

## 实测性能：差距到底有多大

讲了这么多"为什么快"，到底快多少？这一节我们真跑一下。本机 GCC 16.1.1，编译命令 `g++ -std=c++20 -O2`，每个用例跑 5,000,000 次（整数）/ 3,000,000 次（浮点），测整条管道（含结果落盘）的墙钟耗时。

先看**整数 → 字符串**（`to_chars` vs `std::to_string` vs `std::snprintf("%d")`）：

```text
[int->str] to_chars :  46.6 ms
[int->str] to_string:  49.8 ms
[int->str] snprintf : 171.1 ms
```

这里有个反直觉的点值得单独说：**整数路径上 `to_chars` 和 `to_string` 几乎一样快**。原因不是 `to_chars` 没优势，而是现代 libstdc++（GCC 11+ 起）的 `std::to_string(int)` **底层就是拿 `to_chars` 实现的**。所以整数的格式化，二者本来就是一回事，差异只在 `to_string` 外面多套了一层 `std::string` 构造。真正的断层在 `snprintf`——它得解析 `"%d"` 格式串、还要处理 locale，比前两个慢了 3 倍多。

把方向反过来，**字符串 → 整数**（`from_chars` vs `std::stoi` vs `std::atoi`）：

```text
[str->int] from_chars:  28.4 ms
[str->int] stoi     :  82.9 ms
[str->int] atoi     :  93.1 ms
```

这次差距就拉开了：`from_chars` 比 `stoi` 快接近 **3 倍**，比 `atoi` 快 3 倍多。`stoi` 慢主要慢在 locale 查找和异常处理路径（哪怕不抛，分支也在），`atoi` 慢在它得走 C 的 `strtod` 家族那套、且要 null 结尾。这里 `to_string` 不能反过来当 `from_chars` 的"同一实现"用，所以解析方向上 `charconv` 的优势是实打实的。

浮点才是 `charconv` 真正大显身手的地方。**`double` → 字符串**（`to_chars` vs `std::to_string` vs `std::snprintf("%.17g")`）：

```text
[dbl->str] to_chars :  96.7 ms
[dbl->str] to_string: 814.6 ms
[dbl->str] snprintf : 765.7 ms
```

`to_chars` 比 `to_string` 快了 **8 倍多**，比 `snprintf` 也快近 8 倍。这个量级就不是小修小补了——它来自 `charconv` 用的那套现代浮点格式化算法（参考论文 Ryū / Schubfach 那一脉，`to_chars` 的实现目标是"最短可往返表示"，而且全程不分配、不查 locale）。任何要把大量浮点序列化出去的场景（数值结果落盘、metrics 导出、科学数据），换 `to_chars` 基本是白捡一个数量级。

> 这些微秒绝对值在不同机器、不同负载下会浮动，但**数量级关系是稳健的**：整数方向 `to_chars` 与 `to_string` 持平、远快于 `snprintf`；解析方向 `from_chars` 比 `stoi` 快约 3 倍；浮点方向 `to_chars` 比传统手段快近一个数量级。我连续跑了三轮，结论一致。

## 浮点：chars_format 与 GCC 的支持现状

讲完整数，来看浮点。浮点版的 `from_chars` / `to_chars` 多了一个 `std::chars_format` 参数：

```cpp
// Standard: C++17
enum class chars_format {
    scientific = 0x1,   // 1.234e+05
    fixed      = 0x2,   // 123456.789
    hex        = 0x4,   // 1.8p+1
    general    = scientific | fixed   // 自动挑（to_chars 默认）
};
```

`to_chars` 对 `double` 的默认行为（不传格式）是 `general`，但会输出**最短的可往返表示**——即用最少的字符保证 `from_chars` 能原样读回同一个 `double`。这一点比 `sprintf` 的 `"%.6f"`（固定位数）或 `std::to_string(double)`（固定 6 位小数）都更聪明：

```cpp
// Standard: C++17
char buf[64];
double d = 123456.789;
auto r1 = std::to_chars(buf, buf + sizeof(buf), d);                 // "123456.789"
auto r2 = std::to_chars(buf, buf + sizeof(buf), d,
                        std::chars_format::scientific);             // "1.23456789e+05"
auto r3 = std::to_chars(buf, buf + sizeof(buf), d,
                        std::chars_format::fixed);                  // "123456.789"
```

实测三种格式：

```text
to_chars default: 123456.789
to_chars scientific: 1.23456789e+05
to_chars fixed: 123456.789
```

浮点 `from_chars` 反过来，按 `chars_format` 去读。hex 格式有点特殊：它用 `p` 分隔二进制指数（`1.8p+1` 表示 `1.5 × 2¹ = 3.0`），跟 `%a` 一脉：

```cpp
// Standard: C++17
std::string s = "1.8p+1";   // 1.5 * 2^1 = 3.0
double d = 0;
auto r = std::from_chars(s.data(), s.data() + s.size(), d, std::chars_format::hex);
// d == 3.0
```

实测：

```text
from_chars double hex '1.8p+1' -> d=3
```

### GCC 的支持现状（实测 16.1.1）

`<charconv>` 的浮点部分有个历史包袱：**C++17 虽然定义了浮点 `from_chars` / `to_chars`，但实现难度大，几个主流编译器花了很久才补齐**。GCC 的浮点 `from_chars` 直到 **11.1** 才完整落地（`to_chars` 浮点更早，8.1 起），Clang/libc++ 那边一度更晚。这导致网上很多老资料会写"浮点 from_chars 不可用"——今天已经过时。

本机 GCC 16.1.1 实测：浮点 `from_chars`（scientific / fixed / hex 三种格式）和 `to_chars`（含默认最短表示）全部可用，上面那些例子都是真跑通的。所以：**只要你不需要兼容 GCC 10 及更早版本，浮点 charconv 可以放心用**。如果是跨平台库、要照顾老工具链，编译期用特性宏 `__cpp_lib_to_chars` 探测（`#ifdef __cpp_lib_to_chars`），它在 GCC 16.1.1 上有定义；注意 `<charconv>` 没有单独的 `__cpp_lib_charconv` 宏，别写错名字。

## 几个真实容易踩的点

把 `charconv` 这一路上最容易翻车的位置集中收一下，每条都是上面实测验证过的：

::: warning 忽略返回值里的 ec
`from_chars` / `to_chars` 都靠返回的 `ec` 报错，不抛异常。**忘判 `ec`、直接用 `value` 是最常见的坑**。好在出错时 `from_chars` 不动 `value`（保持原值），所以你看到的会是"变量里的旧值"，是个隐蔽 bug——程序不崩，结果却是错的。养成习惯：`if (auto r = std::from_chars(...); r.ec == std::errc{}) { 用 value }`。

`to_chars` 这边，忘判 `ec` 的后果更直接：缓冲不够时它什么都没写进去，你拿 `(buf, r.ptr)` 构造字符串会拿到未初始化内容。
:::

::: warning 缓冲给小了
`to_chars` 缓冲不够时返回 `ptr == last`、`ec == value_too_large`，且**不保证写了什么**（可能写了一部分、可能没写）。整数缓冲给 `std::numeric_limits<T>::digits10 + 2`（十进制位数 + 符号 + 余量）足够；浮点用最短表示时，给个 32 字节一般够 `double`。拿不准就开大点，`charconv` 不嫌缓冲大。
:::

::: warning from_chars 不跳前导空白
前面强调过，这里再钉一遍：`from_chars` 要求首字符就是数字或符号，不跳空白、不跳正负号外的任何东西。解析外部输入（用户键入、文件、网络字段）务必先 trim，或者用 `std::find_if` + `!std::isspace` 定位到第一个有效字符再喂给它。
:::

::: warning to_chars 不写 null 结尾
`to_chars` 写完数字即停，不补 `\0`。要 C 字符串自己补（`*r.ptr = '\0'`），要 `std::string` 用 `std::string(buf, r.ptr)`。直接 `printf("%s", buf)` 必踩雷。
:::

::: warning 符号位的边界
`from_chars` 对无符号类型也能吃掉前导 `-` 吗？**不能**——`"-1"` 往 `unsigned` 里塞会返回 `invalid_argument`（实测见上面的 unsigned 例子，`u` 保持不变）。这和 `strtoul` 接受负数、再转成无符号值的行为不同。想兼容带符号写法的无符号字段，得自己先按有符号解析、再判范围。
:::

## 小结

`charconv` 的价值就一句话——**它是标准库数字↔字符串互转的性能上限**，代价是你得自己管缓冲、判返回码、处理空白。几条关键结论收一下：

- 四个"不做"换来速度：无 locale、无异常、无分配、无格式串。`to_chars` 直接写你给的缓冲，`from_chars` 直接读你给的区间。
- 整数方向 `to_chars` 与 `std::to_string` 几乎一样快（libstdc++ 的 `to_string(int)` 底层就是 `to_chars`），但都远快于 `snprintf`。
- 解析方向 `from_chars` 比 `stoi` 快约 3 倍（实测 28 ms vs 83 ms，500 万次）。
- 浮点方向 `to_chars` 比传统手段快近一个数量级（实测 97 ms vs 800 ms，300 万次），靠的是现代最短可往返格式化算法。
- 浮点 `from_chars` / `to_chars` 在 GCC 16.1.1 全可用（含 scientific / fixed / hex 三格式）；兼容老工具链用 `__cpp_lib_to_chars` 探测。
- 五个高频坑：忘判 `ec`、缓冲太小、不跳前导空白、`to_chars` 不写 `\0`、无符号类型拒收 `-`。

下一篇我们讲 `<format>`（C++20）和 `<print>`（C++23）——那俩是在这套底层原语之上盖的"带格式串、带类型安全、带 locale 支持"的高层设施，方便是方便了，但代价是没法做到 `charconv` 这种裸性能。理解了 `charconv` 这一层，回头再看 `format` 内部为什么要调 `to_chars`，就会非常自然。

## 参考资源

- [cppreference: std::to_chars](https://en.cppreference.com/w/cpp/utility/to_chars) —— `to_chars` 系列重载、`chars_format`、返回值语义
- [cppreference: std::from_chars](https://en.cppreference.com/w/cpp/utility/from_chars) —— `from_chars` 系列重载、错误码（`invalid_argument` / `result_out_of_range`）
- [cppreference: std::chars_format](https://en.cppreference.com/w/cpp/utility/chars_format) —— `scientific` / `fixed` / `hex` / `general` 四种格式
- [P0067R5: Elementary string conversions](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0067r5.html) —— `charconv` 入标准的提案，讲了"无 locale / 无异常 / 无分配"的设计动机
