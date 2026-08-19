---
chapter: 7
cpp_standard:
- 11
- 17
description: 讲透 std::regex 的基本三件套、迭代器分词与捕获组，并用真实 benchmark 暴露它比 string::find 慢一个量级、循环里构造更慢的事实，最后给出该用谁、何时换第三方库的判断
difficulty: intermediate
order: 54
platform: host
prerequisites:
- string 深入：SSO、COW 与 resize_and_overwrite
- 算法总览（上）：非修改式、修改式与查找，面对一个问题怎么挑
reading_time_minutes: 16
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'regex：标准库最重的文本工具与它的代价'
---

# regex：标准库最重的文本工具与它的代价

前面我们走完了容器、迭代器和算法三块，用的都是 `string::find`、`find_first_of` 这类"看名字就会用"的接口。这一篇换一个画风——标准库里最重的一个文本工具：`<regex>`。

为什么要专门拎一篇出来讲它，原因很实在：`std::regex` 是标准库里**少有的、功能强到能直接写工程级模式、同时又慢到能拖垮一整段热路径**的组件。它支持捕获组、回溯引用、命名分组、零宽断言、四种语法、大小写无关……你日常写正则想要的东西基本都有；代价是它跑起来比手写字符串查找慢一个量级以上。这件事很多新手不知道，等到上线被 CPU 打爆才发现。所以这一篇我们不只讲怎么用，更重要的是把它"重"在哪、"慢"在哪用真实数据摊开给你看，让你知道什么时候该用它、什么时候该绕开它。

我们从最基本的三件套开始，跑通用法，然后专门拿一节做性能对比——那是这篇的核心价值。

## 三件套：match / search / replace

`<regex>` 顶层的三个函数正好对应文本处理最常见的三种需求：

- `std::regex_match` —— **整串**必须完全匹配模式（整个字符串从头到尾都得对得上）；
- `std::regex_search` —— 在串里**搜索**任意一处匹配的子串（找到就返回，不要求整串）；
- `std::regex_replace` —— 把所有（或部分）匹配替换成别的内容。

这三个名字看着像，语义差很多，新手最容易在 `match` 和 `search` 上栽。`match` 要求**整串**匹配，差一个字符都不行；`search` 只要串里有那么一段对得上就算赢。我们先把这个区别跑出来：

```cpp
// Standard: C++17
#include <iostream>
#include <regex>
#include <string>

int main()
{
    std::string email = "charlie@example.com";
    std::regex email_re(R"(^\w+@\w+\.\w+$)");

    // regex_match：整串必须完全匹配
    std::cout << "regex_match 邮箱: "
              << std::boolalpha << std::regex_match(email, email_re) << '\n';
    // 整串对得上 -> true

    // 同一个模式,换成"前后带其它字"的串,match 直接 false
    std::cout << "regex_match 带垃圾: "
              << std::regex_match(std::string("联系 charlie@example.com 谢谢"), email_re) << '\n';

    // regex_search：在串里搜子串,不要求整串
    std::string text = "订单 #12345 已于 2026-06-22 发货";
    std::regex num_re(R"(\d+)");
    std::smatch m;
    if (std::regex_search(text, m, num_re)) {
        std::cout << "search 找到第一段数字: " << m[0]
                  << " (位置 " << m.position(0) << ")\n";
    }

    return 0;
}
```

用 `g++ -std=c++17 -O2`（本机 GCC 16.1.1）跑：

```text
regex_match 邮箱: true
regex_match 带垃圾: false
search 找到第一段数字: 12345 (位置 8)
```

注意两个细节。第一，模式串我们写成了 `R"(...)"`——这是 C++11 的**原始字符串字面量**，里面的反斜杠不会被 C++ 编译器先吃掉一次。正则里 `\d`、`\w`、`\.` 满地都是反斜杠，不用原始字符串就得写成 `"\\d+"` 这种看着眼花的双反斜杠，用 `R"()"` 是正则场景下的标配写法。第二，`regex_search` 只返回**第一处**匹配，想拿后面所有的得用迭代器，这正是下一节的内容。

### smatch：捕获组怎么取

上面用到的 `std::smatch`（`match_results<std::string::const_iterator>` 的别名）不只是一个布尔结果——它把整个匹配和**子匹配**全存了下来。模式里每一对括号就是一个捕获组，`m[0]` 是整段匹配，`m[1]`、`m[2]`... 是第几个括号。我们拿一段带时间戳的日志演示：

```cpp
// Standard: C++17
std::string log = "2026-06-22T14:30:01 INFO user=alice";
std::regex ts_re(R"((\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2}))");
std::smatch ts;
if (std::regex_search(log, ts, ts_re)) {
    std::cout << "完整时间戳: " << ts[0] << '\n';
    std::cout << "年=" << ts[1] << " 月=" << ts[2] << " 日=" << ts[3]
              << " 时=" << ts[4] << " 分=" << ts[5] << " 秒=" << ts[6] << '\n';
}
```

```text
完整时间戳: 2026-06-22T14:30:01
年=2026 月=06 日=22 时=14 分=30 秒=01
```

括号从左到右编号，`(\d{4})` 是第 1 组对应年，依次往后。`ts[0]` 永远是"整段被匹配上的文本"。捕获组是正则在工程里真正有用的地方——提取结构化字段、解析协议头、改写模板，全靠它。

### regex_replace：把匹配换掉

第三个函数负责改写。默认它**替换所有**匹配，传 `format_first_only` 标志可以只换第一处：

```cpp
// Standard: C++17
std::string log = "2026-06-22T14:30:01 INFO user=alice";
std::regex num_re(R"(\d+)");

std::string masked = std::regex_replace(log, num_re, std::string("[NUM]"));
std::cout << "replace 打码: " << masked << '\n';

std::string first_only = std::regex_replace(log, num_re, std::string("#"),
                                            std::regex_constants::format_first_only);
std::cout << "replace 仅第一处: " << first_only << '\n';
```

```text
replace 打码: [NUM]-[NUM]-[NUM]T[NUM]:[NUM]:[NUM] INFO user=alice
replace 仅第一处: #-06-22T14:30:01 INFO user=alice
```

这里有个坑要先点一下：`regex_replace` 的替换串**不要写裸的 `$`**。ECMAScript 语法里 `$1`、`$&` 这些是反向引用（`$1` 代表第 1 个捕获组、`$&` 代表整段匹配）。你要是单纯想把数字换成字面 `$`，得小心它被当成特殊符号。简单场景照上面那样换成一个普通字符串就行。

## 迭代器：遍历所有匹配 + 分词

三件套能覆盖大部分写改读需求，但有两件事它干不了：遍历串里**所有**匹配（`regex_search` 只给第一处）、按模式**分词**。标准库为此配了两个迭代器。

`std::regex_iterator` 把"每次 `++` 给下一个匹配"做成了迭代器，于是遍历所有匹配就是一行的范围 `for`：

```cpp
// Standard: C++17
std::string text = "电话 138-1234-5678, 备用 010-8765-4321, 也可以 159-0000-1111";
std::regex phone_re(R"((\d{3})-(\d{4})-(\d{4}))");

for (std::sregex_iterator it(text.begin(), text.end(), phone_re), end; it != end; ++it) {
    std::cout << "  区号=" << (*it)[1] << " 号码=" << (*it)[2] << "-" << (*it)[3] << '\n';
}
```

```text
  区号=138 号码=1234-5678
  区号=010 号码=8765-4321
  区号=159 号码=0000-1111
```

注意那个默认构造的 `end`——它是"哨兵"，表示"匹配到末尾了"。这个套路上一篇讲流迭代器时见过（`istream_iterator` 的 EOF 哨兵），套路一样：你不用提前知道串里有几个匹配，迭代器自己会在末尾停下。解引用 `*it` 拿到的是一个 `smatch`，所以 `(*it)[1]` 直接取捕获组。

另一个是 `std::regex_token_iterator`，专门干分词的活。它的关键参数是最后一个：传 `-1` 表示"要匹配**之间**的内容"（也就是把匹配当分隔符，拿剩下的字段）；传 `0` 或非负数表示"要匹配本身或第 N 个捕获组"：

```cpp
// Standard: C++17
std::string csv = "alpha,beta,,gamma,delta";   // 故意留一个空字段
std::regex comma_re(",");

std::sregex_token_iterator tit(csv.begin(), csv.end(), comma_re, -1);
std::sregex_token_iterator tend;
int idx = 0;
for (; tit != tend; ++tit) {
    std::cout << "  [" << idx++ << "] '" << tit->str() << "'\n";
}
```

```text
  [0] 'alpha'
  [1] 'beta'
  [2] ''
  [3] 'gamma'
  [4] 'delta'
```

五段，连续逗号中间那个空字段被原样保留成了 `[2] ''`。这一点很关键——和很多手写 `split` 不同，`regex_token_iterator` 不会把连续分隔符合并，连续分隔符之间的空串照样算一段，上面 `[2] ''` 就是证据。

::: warning 别把分词写成手写循环
很多人遇到"按分隔符切串"第一反应是手写一个找分隔符、切子串的循环。先想想你要的行为是不是要**保留空字段**——手写循环很容易把连续分隔符当成一个，悄悄丢字段。`regex_token_iterator` 语义明确（保留空字段），行为可预期。当然，如果你的需求是"就是想丢空字段"，那另说，但那是一个**有意识的决定**，不是 bug。
:::

## 默认语法：ECMAScript，和你在 JS/Python 里写的差不多

`std::regex` 默认用的是 **ECMAScript** 语法——没错，就是 JavaScript 那套。这意味着你在 JS 里写惯的 `\d`、`\w`、`\s`、`{n,m}`、`(?:...)`、`(?=...)` 这些基本都能照搬过来。日常用得到的元字符和字符类收一下：

| 写法 | 含义 |
|---|---|
| `.` | 任意一个字符（默认不含换行） |
| `\d` `\D` | 数字 / 非数字 |
| `\w` `\W` | 单词字符（字母数字下划线）/ 非 |
| `\s` `\S` | 空白 / 非空白 |
| `*` `+` `?` | 0+ / 1+ / 0或1 |
| `{n}` `{n,m}` | 恰好 n 次 / n 到 m 次 |
| `[abc]` `[^abc]` | 字符集 / 取反 |
| `^` `$` | 行首 / 行尾 |
| `(...)` `(?:...)` | 捕获组 / 非捕获组 |
| `(?=...)` `(?!...)` | 顺序环视（正向/负向） |
| `\1` | 反向引用第 1 组 |

`std::regex` 构造时可以传 `std::regex_constants` 里的语法标志切换到别的语法（`extended`、`grep`、`awk` 等 POSIX 系），或者叠 `icase` 让匹配不区分大小写。这里有个真实容易踩的坑：**不同语法对字符类的支持不一样**。比如 `\d` 是 ECMAScript 的缩写，换到 `extended`（POSIX）语法就不认了——POSIX 系写数字得用 `[0-9]`：

```cpp
// Standard: C++17
using namespace std::regex_constants;
std::string s = "abc 123 XYZ";

std::regex def_re(R"(\w+\s\d+)");                 // 默认 ECMAScript,\w \d 都认
std::smatch m;
if (std::regex_search(s, m, def_re)) std::cout << "默认 ECMAScript: '" << m[0] << "'\n";

std::regex ext_re(R"([0-9]+)", extended);         // POSIX extended,\d 不认,用 [0-9]
if (std::regex_search(s, m, ext_re)) std::cout << "POSIX extended [0-9]+: '" << m[0] << "'\n";

std::regex icase_re("hello", icase);              // 叠 icase 不区分大小写
std::cout << "icase 匹配 'HELLO': " << std::boolalpha
          << std::regex_search(std::string("say HELLO world"), icase_re) << '\n';
```

```text
默认 ECMAScript: 'abc 123'
POSIX extended [0-9]+: '123'
icase 匹配 'HELLO': true
```

实际工程里九成以上的场景你都用默认 ECMAScript，这块不用记太多，知道"想换语法、想加大小写无关，靠 `regex_constants`"就够了。

## 实测：regex 真的慢一个量级

铺垫完用法，现在到这篇最重要的部分了。光说"regex 慢"是空口断言，我们用一个真实 benchmark 把它和几种替代方案摆在一起跑。

场景很普通：处理 10 万行日志，每行判断"行里有没有连续 4 位以上的数字"。这是个典型的"热路径上扫一堆文本、每行做一次匹配"的需求。我们对比五种写法：

1. `std::regex` 预编译好、循环外构造一次，每行 `regex_search`；
2. `std::regex` **在循环里每行重新构造**（反面教材）；
3. `string::find_first_of("0123456789")` 找第一个数字字符；
4. 手写字符状态机：数连续数字字符，到 4 个就命中；
5. `std::any_of` + `std::isdigit`，找到第一个数字就停。

所有方案命中数都一样（74810 行），保证比的是速度而不是语义差异。best-of-N、`-O2`：

```text
best-of-N 微秒数(100000 行,本机 GCC 16.1.1 -O2):
  regex  (预编译)    : 54347 us
  regex  (循环内构造): 253600 us
  find_first_of      : 3276 us
  手写状态机         : 935 us
  any_of + isdigit   : 1219 us

相对(以 find_first_of 为 1x):
  regex 预编译 / find     = 16.6x
  regex 循环构造 / find   = 77.4x
  手写状态机 / find       = 0.285x

构造一次 std::regex("\d{4,}") best: 2405 ns (2.405 us)
```

数据很直白。几个结论：

- 即便 `regex` 对象**预编译**好、循环外只构造一次，它也比 `find_first_of` 慢 **16 倍**，比手写状态机慢 **近 60 倍**。
- 要是手贱把 `std::regex re(pattern)` 写进了循环里，每行重编译一次 NFA，慢到 **77 倍**——构造一次 regex 要 1~2 微秒（见最后一行），循环 10 万次光构造就烧掉一大半时间。
- 反过来，手写状态机和 `any_of+isdigit` 这类"只为这一个具体需求量身写"的代码，最快能压到 `find` 的三分之一。这就是通用工具和专用工具的代价差。

这里要诚实补一句：微秒绝对值会随机器、负载波动（我们多跑几轮，预编译版在 16~18x 之间漂），但**数量级结论是稳健的**——`std::regex` 比手写字符串查找慢一个量级，这件事在所有主流实现（libstdc++ / libc++ / MSVC）上都成立，不是 GCC 的锅。原因在下一节展开。

::: warning 别在循环里构造 regex 对象
这是 `<regex>` 第一大坑。`std::regex` 的构造函数要做**语法分析 + 编译 NFA**，本身就不是一个便宜操作（上面实测一次 1~2us，复杂模式更久）。把它放进热循环等于每轮重编译一次状态机，性能直接塌方。正确做法：模式固定就把 `std::regex` 对象**提到循环外**构造一次（甚至做成 `static const`），循环里只调 `regex_search`。这一条改下来，上面的 77x 直接降回 16x。
:::

## 它为什么这么慢：回溯 NFA 的代价

数据有了，机制得讲清楚，不然就成了"记住 regex 慢"的结论而无头无尾。

`std::regex`（ECMAScript 语法）底层是一个**回溯式 NFA**（非确定有限自动机）。它的工作方式不是"预先把模式编译成一个一次扫描就出结果的状态机"，而是"一边扫输入、一边在模式的所有可能走法之间试，走不通就回溯再试"。这套机制的好处是功能强——捕获组、反向引用、零宽断言、回溯引用这些花活儿，回溯 NFA 天然支持，而那些功能在理论上根本不能用纯 DFA 表达。代价是**最坏情况是指数级**的。

我们用一个经典反面教材把这事可视化：模式 `(a+)+b`，喂一长串 `a`、结尾故意不给 `b`。这个模式会让回溯 NFA 对同一批 `a` 反复试各种分组组合，输入每长一点，耗时翻几番：

```cpp
// Standard: C++17
std::regex bad_re(R"((a+)+b)");
for (int n : {16, 20, 24, 28}) {
    std::string s(static_cast<std::size_t>(n), 'a');   // n 个 a,结尾没有 b
    auto t0 = std::chrono::steady_clock::now();
    bool m = std::regex_match(s, bad_re);
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "n=" << n << " matched=" << std::boolalpha << m << " 耗时 " << ms << " ms\n";
}
```

```text
n=16 matched=false 耗时 5 ms
n=20 matched=false 耗时 93 ms
n=24 matched=false 耗时 1656 ms
n=28 matched=false 耗时 22752 ms
```

想看指数爆炸？点开下面这个在线示例（n=28 要 22 秒会超时，在线只跑到 n=24，已能看出每加 4 个字符耗时乘约 20 倍）：

<OnlineCompilerDemo
  title="病态回溯：(a+)+b 的指数爆炸"
  source-path="code/examples/vol3/54_regex_backtracking.cpp"
  description="模式 (a+)+b 喂 16/20/24 个 a，耗时指数增长；n=28 实测约 22 秒，在线省略——这是回溯 NFA 不保证线性时间的本质"
  allow-run
/>

看这个增长：输入长度从 16 涨到 28（只多了 12 个字符），耗时从 5 毫秒炸到 **22 秒**。每加 4 个字符，耗时大约乘 18——这是教科书级别的**病态回溯**（catastrophic backtracking）。这种模式一旦落到接收外部输入的代码路径上（比如用户提交的字符串过这种正则），就是一个现成的 DoS 漏洞。这不是 GCC 实现烂，是回溯 NFA 的本质：它**不保证**线性时间，复杂度由模式结构决定。

这一点是后面要讲的"什么时候该换第三方库"的根因。

## 何时该用，何时该绕开

把代价讲透了，决策反而清晰。我们给一套判断：

**该用 `std::regex` 的场景**——模式的复杂度才是你愿意付性能代价的理由：

- 结构化字段解析：邮箱、电话、URL、ISO 时间戳、带捕获组的协议头。这些用 `find` 写起来又臭又长还容易错，正则一两行搞定，可读性碾压。
- 嵌套/可选结构：带可选部分、分支 `(|)`、重复分组的模式，手写状态机会写成意大利面。
- 一次性脚本、冷启动配置解析、低频调用路径：一年跑不了几次的地方，写得对、写得快比跑得快重要。

**该绕开 `std::regex` 的场景**——性能或可控性更重要：

- **简单字面量匹配**：就找一段固定字符串，用 `string::find`。别拿正则大炮打蚊子。
- **简单字符集判断**：找"有没有数字"、"有没有空白"这种，用 `find_first_of` 或 `any_of+isdigit`，比 regex 快一两个量级。
- **热路径、批量数据**：每秒处理几万到几十万条文本的服务端解析，`std::regex` 的常数因子和最坏情况指数级退化都是隐患。
- **接收外部输入的模式**：用户传进来的字符串直接当模式，病态回溯风险（见上一节的 22 秒）。

绕开的时候有三个去向，按场景选：

- **简单字面量/字符集** —— 标准库自带的 `find` / `find_first_of` / `any_of` / `search`（这一卷的算法篇和 `string` 篇都讲过），零额外依赖，最快。
- **要正则的表达力、又要线性时间保证** —— Google 的 **RE2**。它是基于自动机理论的，**保证匹配时间关于输入长度线性**，不会病态回溯。代价是不支持反向引用、零宽断言里的一些花活儿（正是这些功能让标准库正则无法线性化）。服务端解析外部输入的首选。
- **编译期已知模式、要极致性能** —— **CTRE**（Compile-Time Regular Expressions，C++17 起）。模式是编译期常量，它在编译期就把模式编译成状态机，运行时是零开销的手写状态机水平。模式固定、对性能敏感的场景非常合适。

这几个库这一篇不展开（本卷聚焦标准库），但你要知道：**它们的快，本质上都是绕开了"回溯 NFA"这条路线**——RE2 用自动机换来线性时间，CTRE 用编译期求值消掉运行期编译开销。标准库 `<regex>` 之所以慢，就是因为它选择了功能最全、但常数最大、还不保证线性的那条路线。

## 几个真实容易踩的点

集中收一下这一路的坑，每条都有上面的实测或机制支撑：

::: warning 循环里构造 regex
第一大坑。`std::regex` 构造 = 语法分析 + 编译 NFA，一次 1~2us 起。把它放进热循环 = 每轮重编译。模式固定就提到循环外（或 `static const`），循环里只调 `regex_search`。上面 benchmark 里这条差了 4~5 倍。
:::

::: warning match 和 search 别混
`regex_match` 要求**整串**匹配，`regex_search` 只要串里有匹配。新手写邮箱校验用 `regex_match` 是对的（要整串是邮箱），但写成 `regex_search` 就会放过 `"联系 abc@x.com 谢谢"` 这种带垃圾的串。要校验"整个字符串就是某格式"用 `match`，要"提取串里的某段"用 `search`。
:::

::: warning 病态回溯是 DoS 漏洞
`(a+)+`、`(a|a)*`、嵌套量词这类模式，接收不可信输入时会指数级退化（上面 28 个 a 跑 22 秒）。接收外部模式或外部输入走正则的服务端代码，要么改用 RE2（线性时间保证），要么对输入长度设上限。别让用户用一个字符串把你的线程卡死。
:::

::: warning 转义反斜杠,用原始字符串
正则里满地反斜杠，C++ 字符串字面量里 `\` 是转义符。要么写 `"\\d+"`（双反斜杠，难读），要么用 `R"(\d+)"`（原始字符串，所见即所得）。正则场景统一用 `R"()"`，少很多 bug。
:::

::: warning 模式串异常会在构造期抛 std::regex_error
模式写错了（括号不配对、非法的量词嵌套等），`std::regex` 构造函数会抛 `std::regex_error`，带一个 `code()` 和 `what()`。接收外部模式的代码一定要 try/catch，否则一个非法模式直接让你的进程挂。预编译的固定模式构造一次，错了编译期/启动期就能发现，危害小。
:::

## 小结

`std::regex` 是标准库里**功能最全、也最重**的文本工具。把它当成"啥都能干但别滥用"的家伙，记住这几条：

- 三件套各司其职：`regex_match`（整串匹配，做校验）、`regex_search`（搜子串，做提取）、`regex_replace`（改写）。`smatch` 取捕获组：`m[0]` 整段、`m[1]` 第 1 组。
- 两个迭代器：`regex_iterator` 遍历所有匹配、`regex_token_iterator` 按模式分词（`-1` 取分隔符之间的字段，保留空字段）。
- 默认 ECMAScript 语法，`\d \w \s` 这些和 JS 一致；想换语法或加大小写无关，用 `std::regex_constants`。
- **真实代价**：预编译好的 `regex` 比 `find_first_of` 慢约 **16 倍**，比手写状态机慢近 60 倍；在循环里构造更慢到 **77 倍**。数量级结论稳健，绝对值随机器漂。
- 慢的根因是**回溯 NFA**：功能强（支持捕获组、反向引用、零宽断言）但最坏情况**指数级**，`(a+)+` 喂 28 个 a 能跑 22 秒，是潜在的 DoS 漏洞。
- 决策：复杂模式（邮箱/电话/时间戳/嵌套结构）用它，简单字面量用 `find`，性能敏感或接收外部输入上 RE2（线性保证）/ CTRE（编译期求值）。

下一篇我们离开文本工具，进入输入输出与文件系统——先从最基础、也最常被吐槽“慢”的 `<iostream>` 讲起，看它到底慢在哪、又该怎么把它用对。

## 参考资源

- [cppreference: `<regex>` 头文件总览](https://en.cppreference.com/w/cpp/regex) —— 整个正则库的入口
- [cppreference: std::regex_match](https://en.cppreference.com/w/cpp/regex/regex_match) —— 整串匹配语义
- [cppreference: std::regex_search](https://en.cppreference.com/w/cpp/regex/regex_search) —— 子串搜索语义
- [cppreference: std::regex_iterator](https://en.cppreference.com/w/cpp/regex/regex_iterator) —— 遍历所有匹配
- [cppreference: std::regex_token_iterator](https://en.cppreference.com/w/cpp/regex/regex_token_iterator) —— 分词迭代器
- [cppreference: std::regex_constants::syntax_option_type](https://en.cppreference.com/w/cpp/regex/syntax_option_type) —— ECMAScript / extended / icase 等语法标志
- [RE2 项目](https://github.com/google/re2) —— Google 的线性时间正则引擎，服务端处理外部输入的首选替代
- [CTRE 项目](https://github.com/hanickadot/compile-time-regular-expressions) —— 编译期正则，C++17 起，模式固定时接近手写状态机性能
