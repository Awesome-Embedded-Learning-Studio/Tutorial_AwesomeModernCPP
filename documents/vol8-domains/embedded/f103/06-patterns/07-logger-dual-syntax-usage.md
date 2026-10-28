---
title: "把日志用起来：两种写法与两个观看面"
description: "examples/06_log 的 69 行 main 覆盖 logger 的用法面：拼接与格式串两种写法怎么选（一套 Formatter 底座、信标函数的编译期校验、render 按实参组合计价）、[501ms][Info ][led] 行格式逐段对表、毫秒时间戳的两个使用注意、MinLevel 裁剪用一条 strings 命令验证、PTY 自动化与 showAnalyzer 人看的两个观看面，v1 边界与自定义 Formatter 收尾"
chapter: 6
order: 7
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 实战
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 8
prerequisites:
  - "零开销日志组件完整踩坑记录：从 source_location 撞墙到反汇编验收"
related:
  - "终审在 map：『裁剪级别连字符串都不进固件』的完整证据链"
  - "从重载集到 Formatter 特化：一次嵌入式日志分发机制的选型讨论"
---

# 把日志用起来：两种写法与两个观看面

## 引言：内幕讲完了，这篇讲怎么用

日志系列咱们写到第七篇，前面六篇讲的全是内幕：

- Tag 怎么把调用点位置捎进来，[《零开销日志组件》](01-logger-zero-overhead.md)有过完整推导。
- 栈上的 `LineBuffer` 会不会打穿栈，[《函数局部的 LineBuffer 到底会不会打穿栈》](03-logger-stack-budget.md)量出了 376 字节的瞬态帧。
- 裁剪宣称靠哪三层证据成立，[《终审在 map》](05-map-is-the-verdict.md)落了锤。
- 18200 字节都花在哪，[《零开销的价格表》](06-zero-overhead-price-list.md)列了清单。

凭什么信它？咱们前六篇已经答完。那么怎么把它接进您自己的工程？笔者用这一篇来回答。素材笔者选了 libestdx 的 `examples/06_log`：`main.cpp` 一共就 69 行，咱们要讲的两种写法、行格式、时间戳、裁剪验证、两个观看面全串在里面。

咱们让它跑起来看输出，您看到的是 Renode 实抓的日志流：

```text
[0ms][Error][boot] libestdx 07_log example
[0ms][Warn ][boot] MinLevel=Info, clock=HalTickClock
[0ms][Info ][boot] watch PTY /tmp/libestdx-log
[501ms][Info ][led] blink=1
[501ms][Info ][ledf] n=1 hex=0x1
[1002ms][Info ][led] blink=2
[1002ms][Info ][ledf] n=2 hex=0x2
```

从输出里咱们能认出三样东西：

- boot 三行用 Error、Warn、Info 三个级别，级别字段都占五格宽。
- 主循环里两种写法各打一行。
- 时间戳逐行递进。

banner 里的 `07_log` 和目录名 `06_log` 对不上？咱们原样复制、一个字节没动。其实 banner 字面量从示例出生那天就写着 `07_log`，`renode.resc` 的头注释也是同款名字，咱们翻遍仓库历史也没找到 `examples/07_log` 目录。所以这就是历史残留，您不用管它。

## 五行接入：别名即配置

咱们要接入的全部配置就压在 `main.cpp` 顶上几行：

```cpp
using Serial = estdx::stm32f1::Uart<estdx::stm32f1::UartInstance::Usart1>;
using Log = estdx::logger::Log<estdx::logger::UartLogSink<Serial>, estdx::logger::LogLevel::Info,
                               128, estdx::stm32f1::HalTickClock>;

static_assert(estdx::logger::LogSink<estdx::logger::UartLogSink<Serial>>);
static_assert(estdx::logger::LogClock<estdx::stm32f1::HalTickClock>);
```

咱们把 `Log` 的四个模板参数一口气说清：

- sink 用谁：示例里是 USART1 的 `UartLogSink<Serial>`。
- 编译期级别地板 `MinLevel`。
- 行缓冲宽度 128 字节。
- 时钟。

后三个不写就是缺省的 `Info`、`128`、`NoClock`，不要时间戳的话您连第四个参数都能省。

README 那段咱们对照过两个入口，而它们背后是同一台引擎：README 里演示的是引擎本体 `Logger<...>::Self().info(...)`，示例用的就是静态调用面 `Log<...>`。

笔者选 `Log` 的理由是方言统一：跟 `Uart`、`Gpio` 一样全静态、无实例，咱们调日志的手感就跟点灯一致。

那两行 `static_assert` 也值得咱们学走。咱们用 logger 自己定义的概念就能把合同检查放在别名旁边：

- `LogSink`：能 `write(std::span<const char>)` 的类型当 sink。
- `LogClock`：能 `now_ms()` 的类型当时钟。

咱们不用等错误在装配链深处炸开：类型不满足时编译器当场就报。

## 两种写法：一套底座，两副手感

示例的主循环咱们直接搬过来：

```cpp
for (std::uint32_t n = 1;; ++n) {
    Led::toggle();
    HAL_Delay(500);
    Log::info(estdx::logger::Tag("led"), "blink=", n);
    Log::infof(estdx::logger::Tag("ledf"), "n={} hex={:x}", n, n);
}
```

两种写法的差别，咱们用几条清单说清：

- 拼接式 `info`：实参按顺序逐个进缓冲，fold 表达式保证顺序就是实参顺序，每个实参由 `Formatter<T>` 特化负责变成文本。
- 格式串式 `infof`：`std::format` 风格的模板行。
- 字段词汇表一共三样：`{}` 任意可格式化值，`{:x}` 整数十六进制，`{:N}` 定宽。
- `{:x}` 内部包成 `Hex`，输出 `0x` 加小写、不补零，日志流里的 `hex=0xa` 就是它打的。
- `{:N}` 只补空格、从不截断。
- `{{`、`}}` 是字面量大括号的转义。

底下是不是同一套底座？咱们在源码里对上了。格式串的 `format_to` 走的仍是 `Formatter` 分发：两边字面量的最后归宿都是同一个 `append(string_view)`。咱们看到拼接式落的是 `"blink="`，格式串落的也是字面量块。两种写法为什么共用同一套？这套分发机制[《从重载集到 Formatter 特化》](02-dispatch-overload-to-formatter.md)里咱们拆过一遍。

格式串还多给咱们一样东西：编译期校验，`std::format` 的 P2216R3 提案就是原型。会被拦下的情况咱们直接列出来：

- 字段数与实参数不符。
- spec 在词汇表外：`{0}` 这类 arg-id 不支持。
- `{:x}` 用在非整数上。

拦在哪？就在 `FormatString` 的 consteval 构造函数。报错的姿势咱们觉得挺嵌入式，咱们让校验器去调非 constexpr 的信标函数，报错会把函数名原样端到咱们眼前。两个名字直接抄给您：`format_string_field_count_mismatch`、`format_string_x_requires_integral`。不用 `consteval throw` 也是刻意的，无异常工具链上笔者试过，连永远走不到的 `throw` 分支也会在语法层被拒掉。

那怎么选？笔者的默认是拼接式，看中的就是没有渲染机器、一行一个标签加值最直白。什么时候轮到格式串？行模板固定要复用的时候就顺手，字段要十六进制或定宽的时候也一样，您想让字段错配在编译期拦下来更是它的主场。咱们把代价也摆出来：`render`（格式串的渲染函数）按实参组合也各生成一份实例化，[《零开销的价格表》](06-zero-overhead-price-list.md)里就量过，示例里的三份实例化也就各 1.3KB，于是得了“三胞胎”的诨名。咱们再看拼接式：同样按组合内联，不过少叠一层 `render`。所以笔者的选法一句话：拼接当日常、格式串按需再上就好。

入口咱们数下来一共六个级别、每级一对：`warn` 对 `warnf`、以此类推。boot 三连横幅用了 Error、Warn、Info 三级，五格字段的两种形态咱们一眼看全：Error 满格，而 Warn 和 Info 补一个空格。

## 行格式与时间戳：逐段对表

咱们拿 `[501ms][Info ][led] blink=1` 对照 `logger.hpp` 的 `append_header` 逐段放进表里：

| 段 | 内容 | 来源 |
|---|---|---|
| `[501ms]` | 毫秒时间戳 | `AClock::now_ms()`，示例是 `HalTickClock`（包 `HAL_GetTick`） |
| `[Info ]` | 级别，定宽 5 | `LevelName` 加 `{:5}`，短名右侧补空格 |
| `[led]` | tag | `Tag("led")` 的文本 |
| ` ` | 一个空格 | 头部与 payload 的分界 |
| `blink=1` | payload | 两种写法各自的装配结果 |
| `\r\n` | 行尾 | 固定 CRLF |

对了，级别段的定宽正是 `{:5}` 打的，咱们读行格式时正好碰上一次。

配置怎么影响段？咱们列三条：

- 没配时钟（`NoClock`）时，时间戳段连实例化都不发生，`if constexpr` 在编译期就把它抹掉。
- `kLocation` 默认关。打开之后 tag 后面多一段 `[file:line]`，位置就存在每个 `Tag` 里，怎么捎进来的，[第一篇《零开销日志组件》](01-logger-zero-overhead.md)讲过。
- README 的行格式 `[123ms][Info ][tag][file:line] payload` 括号里写着“时间戳与位置段按配置裁剪”，按配置裁的也就是时间戳与 `[file:line]` 两段，`[tag]` 恒在。

咱们看 `HalTickClock`：它包着 `HAL_GetTick`，毫秒数走的也是 `std::uint32_t`。回绕一圈要多少天？咱们算过大约 49.7 天，所以您别直接拿两条日志做减法当间隔。

boot 三行全是 `[0ms]`？原因笔者也查过：三条横幅在第一个 tick 到来前就打完了。全零咱们确认是正常现象，不是您时钟没配好。

咱们把行长也算一遍：128 字节里永远留 3 字节给收尾，125 字节就是您的内容预算，超了就截断。截断的行结尾也会多一个 `~`，每行咱们只调一次 `Sink::write`。您写日志时要瞄一眼最长的那行，所以别让关键信息被截在 `~` 前面。

运行期咱们还能拧一个旋钮 `Log::setLevel`：收放范围压在编译期地板以内。`MinLevel=Info` 的固件里其实根本没编进 Debug 调用，所以运行时 `setLevel(Trace)` 也救不回任何东西。想看 Debug？您改 `MinLevel` 重新编译就好。

## 裁剪验证：一条 strings 命令

咱们在示例里埋了验证用的牺牲品：

```cpp
Log::debug(estdx::logger::Tag("boot"), "debug-marker-must-not-survive-MinLevel-Info");
```

咱们怎么验证它真的消失？命令只留一条：

```sh
arm-none-eabi-strings -a build/examples/06_log/log_example.bin | grep must-not-survive
```

咱们看到零输出就算过。您把 `MinLevel` 改成 `Debug` 再构建一遍：同一命令就能查到 1 行。咱们引用[第五篇《终审在 map》](05-map-is-the-verdict.md)的判决书，您再看 text 差价就是 508 字节（18200 对 18708）。

有一条告诫咱们必须重复，命令就只认 ARM 的 bin。host 产物为什么不算数？笔者在 x86-64 上实测过，结果 GCC 把短字面量拆成 `movabs` 立即数塞进指令流。于是 host 上 strings 查无此串就有两种可能：真裁了，或者就是被拆了。三重陷阱咱们就留给[《终审在 map》](05-map-is-the-verdict.md)解剖。

运行时咱们也备了第二道兜底。咱们让 `check_renode.py` 逐字节检查流内容、断言 `[Debug` 与 marker 不出现。编译期裁剪要是因构建配置意外失效怎么办？咱们一跑校验它就立刻尖叫。

## 两个观看面：PTY 给机器，showAnalyzer 给人

日志流从 USART1 出来之后给谁看？咱们给 Renode 配了两个面：一个喂脚本、另一个喂人。

面一咱们选了 PTY 做自动化面，`renode.resc` 里咱们翻到的只有两步：

- `CreateUartPtyTerminal` 挂主机 PTY。
- `connector Connect` 把它接到 USART1。

默认路径咱们也留在 `/tmp/libestdx-log`。

对了，脚本里 `$bin?=`、`$uart_pty?=` 的问号咱们也专门确认过：意思是“可覆盖的默认值”。而 `check_renode.py` 正是吃了这一点，咱们让它把 PTY 指到临时目录再 include 脚本。跑完的清理咱们也不用管：临时目录自己会消失。

校验怎么做的？咱们让它逐 marker 做字节级匹配：

```python
(b"[Error][boot] libestdx 07_log example\r\n", 10),
(b"[Warn ][boot] MinLevel=Info, clock=HalTickClock\r\n", 5),
(b"[Info ][boot] watch PTY /tmp/libestdx-log\r\n", 5),
(b"[Info ][led] blink=1\r\n", 15),
(b"[Info ][ledf] n=1 hex=0x1\r\n", 5),
```

咱们点两处细节。一处是 `` `"Warn "` `` 带的行尾空格、另一处就是每条以 `\r\n` 收尾。行尾空格就是定宽 5 的直接后果，咱们写 marker 时就必须带上它：要按字节匹配，而差一个空格就永远等不到，脚本开头的文档字符串也就专门为此打了预防针。时间戳段咱们反而用正则 `\[\d+ms\]` 放行，毫秒值咱们就不做字面断言。

全部校验咱们一条命令跑完：`cmake --build build --target check_log_renode`。命令安静退下来咱们就把横幅、两种写法、裁剪、时间戳一次核完。

面二咱们留给 `showAnalyzer`。咱们比对 `renode_console.resc` 和 PTY 版发现差别不止一行：PTY 那三行（`$uart_pty` 变量、`CreateUartPtyTerminal`、`connector Connect`）换成了 `showAnalyzer usart1`，头注释也换成了人读版的说明。

那开了它之后呢？带主机与虚拟时间戳的日志行就直接打进 Renode 控制台，咱们连第二个终端都不用开。您快速过一眼输出就用 `cmake --build build --target sim_log_console`。

PTY 咱们想人肉盯也行。`sim_log_example` 起来之后您就另开一个终端去连 `/tmp/libestdx-log`，咱们用 `screen /tmp/libestdx-log 115200`。退出时您按 `Ctrl-A`、`K`、`Y`。您在真正的开发板上用 `flash_log_example` 烧录：行格式跟仿真里的一模一样。

## v1 边界与自定义类型

v1 的边界咱们列成四条，您拿来用要有数：

- 发送是阻塞式：UART sink 走 `try_send`、有界超时、尽力而为，超时怎么从波特率推导在[第五篇《终审在 map》](05-map-is-the-verdict.md)。
- 仅限线程上下文，ISR 里禁用。
- 无 float，要打原始位型就包 `Hex`。
- 单 sink。

自定义类型入日志的口子咱们也留一句：特化 `Formatter<T>`、写 `static format(LineAppender auto&, const T&)`。`LineAppender` 就是行缓冲的追加接口（`format` 里对它调 `append`）、形状同 `std::formatter`。写法与取舍[《从重载集到 Formatter 特化》](02-dispatch-overload-to-formatter.md)里咱们全讲过。

## 注意事项与常见错误

| 症状 | 原因 | 解法 |
|---|---|---|
| `format_string_field_count_mismatch` 编译失败 | 字段数与实参数不符 | 对齐数量 |
| `format_string_x_requires_integral` | `{:x}` 用在非整数上 | 传整数，或去掉 `:x` |
| `format_string_unknown_spec` | spec 在词汇表外（含 `{0}` arg-id） | 只用 `{}`、`{:x}`、`{:N}` |
| 校验脚本 marker 等不到 | 级别段行尾空格、`\r\n` 都是字节 | marker 写成 `` `"Warn "` `` 带空格带 CRLF |
| host 产物上 strings 查无 marker | GCC 把字面量拆成 movabs 立即数 | 终审在 ARM 的 bin 上做 |
| `setLevel(Trace)` 后 Trace 仍无输出 | 低于编译期地板的调用体是空的 | 改 `MinLevel` 重建 |
| 行尾出现 `~` | payload 超 125 字节内容预算 | 精简内容或调大行缓冲模板参数 |
| `logger: float rejected` 或单字符被拒 | 浮点与单字符被定向拒绝 | `Hex` 或定点，单字符传 `string_view("(")` |

## 参考资源

- [libestdx 仓库（examples/06_log 与 logger 模块）](https://github.com/Awesome-Embedded-Learning-Studio/libestdx)
- [logger 模块源码目录](https://github.com/Awesome-Embedded-Learning-Studio/libestdx/tree/main/include/libestdx/logger)
- [P2216R3：std::format 的编译期检查](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p2216r3.html)
- [Renode 文档](https://renode.readthedocs.io/)
