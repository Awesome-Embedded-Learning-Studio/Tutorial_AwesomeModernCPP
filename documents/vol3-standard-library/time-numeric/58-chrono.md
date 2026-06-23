---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 讲透 chrono——duration 的编译期分数运算为什么能算 1.5Hz 的周期、steady_clock 凭什么是测耗时的唯一正确选择而 system_clock 会被 NTP 跳变坑、C++20 日历 year/month/day/Sunday[last] 与时区 zoned_time 的实战，以及 chrono 特化的 format 格式化
difficulty: advanced
order: 58
platform: host
prerequisites:
- format：C++20 的类型安全格式化
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 22
related:
- format：C++20 的类型安全格式化
- '<numeric>：累加、填充、内积与相邻差'
tags:
- host
- cpp-modern
- advanced
- 基础
title: 'chrono：duration、时钟与 C++20 日历'
---

# chrono：duration、时钟与 C++20 日历

时间这东西，看起来再朴素不过——`gettimeofday` 拿个秒数、相减一下不就完了？可真到了工程里，时间的坑能让你怀疑人生：测量一段代码的耗时，为什么偶尔测出**负数**？同一个时间戳，为什么在不同机器上打印出不一样的字符串？「2026 年 6 月的最后一个星期日」这种需求，要不要自己手算闰年和星期几？

`<chrono>` 这个库，就是来把这些坑一次性堵上的。它的设计哲学非常硬核——**把「一段时间」「一个时间点」「一个时钟」三类概念用类型严格区分，所有单位换算和精度损失都挪到编译期用分数算术解决**。C++11 引入了它，但很长一段时间大家只敢用「`steady_clock` 测耗时」这一小块；直到 C++20 把日历、时区、格式化补齐，chrono 才真正成为一个能扛起生产日志、调度、协议时间戳的完整库。

这一篇我们把 chrono 拆透：先看 `duration` 怎么用编译期分数运算做到「1.5Hz 的周期能算、500ms 不能隐式变 1s」，再看三种 `clock` 为什么只有 `steady_clock` 能测耗时——这里有个真实的、会被老资料带歪的坑；接着进 C++20 的日历和时区，看 `2026y/June/Sunday[last]` 这种写法是怎么做到编译期合法的；最后讲 chrono 特化的格式化，和上一篇的通用 `std::format` 对上。时区的本机支持情况我们全程实测，不空口断言。

## duration：用编译期分数算「一段时间」

`duration` 是 chrono 的地基。一句话定义：**一个刻度数（count）乘以一个刻度周期（period）**。`period` 是个 `std::ratio`，编译期分数，描述「一个 tick 等于多少秒」。

```cpp
// 标准库里的真身（简化）
template <typename Rep, typename Period = std::ratio<1>>
class duration {
    Rep rep_;   // 刻度数，通常是 int/long long/double
};
```

`Rep` 是「计数用什么类型存」，`Period` 是「一个 tick 多少秒」。于是 `seconds` 是 `duration<long long, ratio<1>>`（一个 tick 1 秒），`milliseconds` 是 `duration<long long, ratio<1, 1000>>`（一个 tick 千分之一秒），以此类推。

先感受一下字面量和最基本的用法：

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>

using namespace std::chrono;

int main() {
    auto half_hour = 30min;     // duration<...minutes>
    auto one_sec   = 1s;        // duration<...seconds>
    auto frame     = 16ms;      // 16 毫秒，常见帧时间
    std::cout << "30min = " << half_hour.count() << " min\n";
    std::cout << "1s    = " << one_sec.count() << " s\n";
    std::cout << "16ms  = " << frame.count() << " ms\n";
    return 0;
}
```

```text
30min = 30 min
1s    = 1 s
16ms  = 16 ms
```

`.count()` 取出内部存的那个刻度数。字面量 `h`/`min`/`s`/`ms`/`us`/`ns` 都在 `std::chrono_literals` 命名空间里（`using namespace std::chrono` 顺带就拿到了），写起来比 `milliseconds{16}` 直观得多。

### 编译期分数运算：1.5Hz 的周期怎么算

真正体现 chrono 设计功底的是 `Period` 的分数运算。标准库自带的 `seconds` / `milliseconds` 周期都是 10 的负幂次（1、1/1000、1/1000000），但现实里的周期不全是整数的——比如 NTSC 视频的帧率是 `60000/1001 ≈ 59.94` fps，一个「1.5Hz」的循环周期是 `1/1.5 = 2/3` 秒。这种「两秒三等分」的周期，传统做法只能存成 `double`，但 chrono 能**用编译期的分数精确表示**：

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
#include <ratio>

using namespace std::chrono;

// 1.5Hz 的周期 = 2/3 秒
using frame_period   = std::ratio<2, 3>;
using frame_duration = duration<long long, frame_period>;

int main() {
    frame_duration fd{1};   // 1 个 tick，恰好是 2/3 秒
    std::cout << "1 tick of (2/3)s\n";
    std::cout << "  as seconds (trunc) = "
              << duration_cast<seconds>(fd).count() << " s\n";
    std::cout << "  as milliseconds    = "
              << duration_cast<milliseconds>(fd).count() << " ms\n";
    std::cout << "  as microseconds    = "
              << duration_cast<microseconds>(fd).count() << " us\n";
    return 0;
}
```

```text
1 tick of (2/3)s
  as seconds (trunc) = 0 s
  as milliseconds    = 666 ms
  as microseconds    = 666666 us
```

注意几个细节。第一，`ratio<2, 3>` 是编译期的，`duration<long long, ratio<2, 3>>` 这个类型本身就把「周期 = 2/3 秒」编码进了类型系统，运行期零开销——`fd` 就是个 `long long`。第二，`ratio` 会自动约分，`ratio<6, 4>` 在标准库里被规约成 `3/2`（`num=3, den=2`），这是用 `constexpr` 算的最大公约数。第三，把 `2/3` 秒 `duration_cast` 成秒会**截断到 0**（整数 `long long` 存不了 0.666），这正是后面要讲的精度损失问题。

不同周期的 `duration` 相加，结果的 period 会自动取公分母（两个 period 的最大公约数做分母）。`1s + 500ms` 不需要你操心单位：

```cpp
auto sum = seconds{1} + milliseconds{500};
// sum 的类型是 milliseconds，count() == 1500
```

这套编译期分数算术是 chrono 区别于「存个 `double` 秒数」的朴素方案的根本：**所有单位换算在类型层面完成，整数算术保精度，编译器替你算公分母**。代价就是模板错误信息难看，但收益是类型安全。

### 隐式转换：为什么 500ms 不能变成 1s

duration 之间的隐式转换有一条硬规则：**只允许「无损」方向**。把「小单位」（高精度）转成「大单位」（低精度）时，只要 tick 数能整除就允许隐式；不能整除（会丢精度）就编译失败。反过来「大单位转小单位」（精度提高，比如 1s 转 1000ms）总是允许。

```cpp
// Standard: C++20
#include <chrono>
using namespace std::chrono;
int main() {
    seconds s = milliseconds{500};   // 500ms -> seconds，不能整除，会丢精度
    (void)s;
    return 0;
}
```

用 GCC 16.1.1 编，这一行**直接编译失败**：

```text
implicit.cpp:7:17: error: conversion from
  'duration<[...],ratio<[...],1000>>' to 'duration<[...],ratio<[...],1>>'
  requested
```

反过来 `milliseconds m = seconds{2}`（2s -> 2000ms，精度提高）能隐式过。这条规则的工程意义很大：**编译器替你挡住了「不知不觉丢了半秒」这种最阴险的精度 bug**。如果你确实想要截断，必须显式写 `duration_cast<seconds>(ms)`——把「精度损失」从一个静默 bug 变成一个明确的、看得见的操作。

### duration_cast 与 ceil / floor / round

`duration_cast` 默认是**截断**（向零取整）。`1750ms` 转 `seconds` 得 `1s`，那半秒就被截掉了。如果业务上需要别的取整方式，chrono 配了 `floor` / `ceil` / `round` 三个函数（C++17 起）：

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
using namespace std::chrono;

int main() {
    milliseconds ms{1750};   // 1.75s
    std::cout << "1750ms:\n";
    std::cout << "  duration_cast<seconds> = "
              << duration_cast<seconds>(ms).count() << " s (trunc)\n";
    std::cout << "  floor<seconds>         = "
              << floor<seconds>(ms).count() << " s\n";
    std::cout << "  ceil<seconds>          = "
              << ceil<seconds>(ms).count() << " s\n";
    std::cout << "  round<seconds>         = "
              << round<seconds>(ms).count() << " s\n";

    milliseconds ms2{1250};  // 1.25s，正好半秒，看 round 怎么处理
    std::cout << "\n1250ms:\n";
    std::cout << "  floor<seconds> = " << floor<seconds>(ms2).count() << " s\n";
    std::cout << "  ceil<seconds>  = " << ceil<seconds>(ms2).count() << " s\n";
    std::cout << "  round<seconds> = " << round<seconds>(ms2).count() << " s\n";
    return 0;
}
```

```text
1750ms:
  duration_cast<seconds> = 1 s (trunc)
  floor<seconds>         = 1 s
  ceil<seconds>          = 2 s
  round<seconds>         = 2 s

1250ms:
  floor<seconds> = 1 s
  ceil<seconds>  = 2 s
  round<seconds> = 1 s
```

`floor` 向下、`ceil` 向上、`round` 四舍五入（半数取偶数，所以 `1250ms` 舍到 `1s` 而不是 `2s`——这是银行家舍入，避免累积偏差）。这三个加上默认截断，凑齐了四种取整语义。计时这类场景里，如果你要算「这一帧过去了几个完整的 16ms」就该用 `floor`，要算「至少要分配几个缓冲」就该用 `ceil`，区别明显。

还有一个绕开整数截断的法子：用 `double` 当 `Rep`。`duration<double>{1.5}` 直接表示 1.5 秒，`duration<double, milli>` 表示毫秒级的浮点数，运算时不会丢精度（当然代价是浮点本身的精度限制）。科学计算、统计耗时分布的场景常用这一手。

## time_point 与三种 clock：测耗时为什么只能用 steady_clock

`duration` 是「一段时间」，`time_point` 是「一个时刻」。它的定义也简单：**某个时钟的 epoch（纪元）加上一个 duration**。

```cpp
// 简化
template <typename Clock, typename Duration = typename Clock::duration>
class time_point {
    Duration since_epoch_;
};
```

`time_point` 绑定了一个 `Clock`——来自不同时钟的 `time_point` 不能直接相减（类型不同，编译期挡住），这个设计正是为了避免把「墙钟时刻」和「单调时刻」混用。

那「时钟」是什么？标准库给了三种，而**到底用哪个**是 chrono 最容易踩的坑，我们先实测看清楚它们的属性。

### 三种 clock 的真实属性

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
using namespace std::chrono;

int main() {
    std::cout << std::boolalpha;
    std::cout << "system_clock::is_steady          = " << system_clock::is_steady << '\n';
    std::cout << "steady_clock::is_steady          = " << steady_clock::is_steady << '\n';
    std::cout << "high_resolution_clock::is_steady = " << high_resolution_clock::is_steady << '\n';
    return 0;
}
```

```text
system_clock::is_steady          = false
steady_clock::is_steady          = true
high_resolution_clock::is_steady = false
```

`is_steady` 的含义是「单调递增，不会回退」。把三个对一下：

- **`system_clock`**：墙钟（wall clock），表示现实世界的「现在是几点几分」。它的 `is_steady == false`——因为系统会通过 NTP（网络时间协议）或手动 `date` 命令调整它：NTP 为了校正时钟漂移会**慢慢拨**（slew），但当时钟偏快很多时也会**直接跳**（step），甚至往回拨。`system_clock` 的 epoch 是 Unix 纪元（1970-01-01 00:00:00 UTC），可以直接和 `time_t` 互转，适合用来打时间戳、和外部世界对账。
- **`steady_clock`**：单调时钟，`is_steady == true`，**保证永远不回退**。它的 epoch 是任意的（实现定义，通常系统启动时刻），值本身没有现实含义，但**两次读数相减一定 >= 0**。这正是测耗时所需要的不变量。
- **`high_resolution_clock`**：标准说它是「最小 tick 的时钟」，但**没规定它稳不稳**。实际上它就是某个其它时钟的别名（实现定义）。

第三个时钟有个被老资料广泛带偏的坑，我们单独挖一下。

::: warning high_resolution_clock 是别名，而且不是 steady 的那个
很多教程和博客会说「`high_resolution_clock` 在 libstdc++ 上是 `steady_clock` 的别名」，这个说法**在 GCC 16.1.1 上已经不成立**。我们实测一下它到底是谁的别名：

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
#include <type_traits>
using namespace std::chrono;
int main() {
    std::cout << std::boolalpha;
    std::cout << "hrc == steady_clock: "
              << std::is_same_v<high_resolution_clock, steady_clock> << '\n';
    std::cout << "hrc == system_clock: "
              << std::is_same_v<high_resolution_clock, system_clock> << '\n';
    return 0;
}
```

```text
hrc == steady_clock: false
hrc == system_clock: true
```

GCC 16.1.1 的 libstdc++ 里 `high_resolution_clock` **就是 `system_clock` 的别名**（源码里那一行 `using high_resolution_clock = system_clock;`），所以它的 `is_steady == false`。这意味着：如果你照着老资料用 `high_resolution_clock` 测耗时，你实际用的是可被 NTP 跳变的 `system_clock`，下面那个「测出负数」的坑你一个都躲不掉。

标准本身在 C++20 之后就**明确建议别再用 `high_resolution_clock`**——它是历史的过渡产物，行为实现定义、跨平台不一致。记住一条：**永远只用 `system_clock` 和 `steady_clock`，`high_resolution_clock` 当它不存在**。
:::

### 实测：为什么 system_clock 测耗时会被坑

口说无凭，我们跑一遍。让 CPU 忙等 200ms，分别用两种 clock 量：

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
using namespace std::chrono;

void busy(milliseconds ms) {
    auto start = steady_clock::now();
    while (steady_clock::now() - start < ms) { /* spin */ }
}

int main() {
    {
        auto t0 = steady_clock::now();
        busy(200ms);
        auto t1 = steady_clock::now();
        std::cout << "steady_clock measured: "
                  << duration_cast<milliseconds>(t1 - t0).count() << " ms\n";
    }
    {
        auto t0 = system_clock::now();
        busy(200ms);
        auto t1 = system_clock::now();
        std::cout << "system_clock measured: "
                  << duration_cast<milliseconds>(t1 - t0).count() << " ms\n";
    }
    return 0;
}
```

```text
steady_clock measured: 200 ms
system_clock measured: 200 ms
```

正常情况下两者都量出 200ms 左右，看着没区别。但**真正的坑在异常路径**：如果在 `busy` 执行期间，系统时钟被 NTP 往回拨（比如校正一个偏快的时钟），`t1 - t0` 会变成负数，或者荒谬地小。`steady_clock` 用的是 Linux 上的 `CLOCK_MONOTONIC`（内核单调时钟源），内核保证它永远单调递增，**不可能回退**，所以 `t1 - t0` 永远 >= 0。

这件事没法在一个用户态程序里稳定复现（你不能随手把系统时钟往回拨，那是 root 才能干、且会搅乱整台机器的操作），但它在生产环境**真实发生**：NTP 步进校正、容器迁移导致的时钟跳变、虚拟化环境的时间漂移，都会让 `system_clock` 的两次读数 delta 失真。日志里偶尔出现「这段代码耗时 -340ms」的负数，九成就是用了 `system_clock` 测耗时。

所以铁律是：**测耗时、测间隔，一律 `steady_clock`**；要打现实时间戳、和外部系统对账，用 `system_clock`。两件事别混。如果实在需要把「测出的耗时」转换成「现实时刻」（比如打日志），用 `steady_clock` 算 delta、用 `system_clock` 单独记一个起点，两者分工，不要互相转换——它们连 epoch 都不一样，转了也是错的。

## C++20 日历：把日期变成类型

到 C++20，chrono 加了一整套日历类型，把「2026 年 6 月 22 日」从一坨 `tm` 结构体和手写 `strftime` 变成了**带类型检查的、可编译期构造的对象**。这一节是 C++20 chrono 的重头戏。

核心类型是一组「日历字段」，每个都是独立的类：

- `year`、`month`、`day`：年、月、日三个独立类型；
- `year_month_day`：组合的日期；
- `weekday`：星期几；
- `hh_mm_ss`：一天之内的时间（时-分-秒）；
- `month_day`、`year_month`、`month_weekday` 等各种「部分日期」。

配上字面量（`2026y`、`June`、`22d`），写日期跟写普通表达式一样：

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
using namespace std::chrono;

int main() {
    auto ymd = 2026y / June / 22d;          // year_month_day
    std::cout << "ymd.ok()? " << ymd.ok() << '\n';

    // year_month_day -> sys_days（C++20 的「天数级别 time_point」）
    auto sys = sys_days{ymd};
    std::cout << "2026-06-22 = " << sys << '\n';

    // 算星期几
    weekday wd{sys};
    std::cout << "weekday: " << wd
              << " (ISO 编码 " << wd.iso_encoding() << ")\n";
    return 0;
}
```

```text
ymd.ok()? 1
2026-06-22 = 2026-06-22
weekday: Mon (ISO 编码 1)
```

`2026y / June / 22d` 这个表达式看着像在除法，其实是运算符重载——`year / month` 得到 `year_month`，再 `/ day` 得到 `year_month_day`。整套链式构造都是 `constexpr` 的，`ymd` 完全可以当编译期常量。`sys_days` 是 `time_point<system_clock, days>` 的别名，也就是「自 Unix 纪元以来的天数」，把一个日历日期变成可以参与时间运算的 `time_point`——这是日历和时钟的衔接点。

`weekday` 有两套编码：`c_encoding()`（周日 = 0，C 风格）和 `iso_encoding()`（周一 = 1，周日 = 7，ISO 8601 风格）。现代代码用 ISO 编码居多，因为「周几」在业务里通常按「周一到周日」理解。

### 类型合法性的内置校验

日历类型自带 `.ok()` 做合法性校验，这是手写日期解析永远做不到的。月、日是否在合法范围、日期是否真实存在（2 月 29 在非闰年非法）都查得清清楚楚：

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
using namespace std::chrono;

int main() {
    std::cout << std::boolalpha;
    std::cout << "June.ok()          = " << June.ok() << '\n';
    std::cout << "month{0}.ok()      = " << month{0}.ok() << '\n';
    std::cout << "month{13}.ok()     = " << month{13}.ok() << '\n';
    std::cout << "2026/2/29 ok?      = " << (year{2026}/2/29d).ok() << '\n';
    std::cout << "2024/2/29 ok?      = " << (year{2024}/2/29d).ok() << '\n';
    return 0;
}
```

```text
June.ok()          = true
month{0}.ok()      = false
month{13}.ok()     = false
2026/2/29 ok?      = false
2024/2/29 ok?      = true
```

`month{0}`（没有 0 月）、`month{13}`（没有 13 月）都 `ok() == false`，2026 年（平年）的 2 月 29 非法、2024 年（闰年）的合法。**闰年判断这件事，再也不用自己写 `year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)` 了**——`.ok()` 替你查。

### last：表达「最后一个周几」

C++20 日历最优雅的一块是 `last`。很多业务需求是「每月最后一个工作日」「六月最后一个周日」这种相对日期，传统做法要算「这个月有几天、那天是星期几」。chrono 把它做成了一个字面量：

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
using namespace std::chrono;

int main() {
    // 2026 年 6 月最后一个周日
    auto last_sun_june = year{2026} / June / Sunday[last];
    std::cout << "2026/June/Sunday[last] = " << sys_days{last_sun_june} << '\n';

    // 每月最后一天
    auto last_day = year{2026} / February / last;
    std::cout << "2026/February/last = " << sys_days{last_day} << '\n';
    return 0;
}
```

```text
2026/June/Sunday[last] = 2026-06-28
2026/February/last = 2026-02-28
```

`Sunday[last]` 是一个 `month_weekday_last`，表达「某月最后一个周日」。整个表达式 `year{2026} / June / Sunday[last]` 类型是 `year_month_weekday_last`，转 `sys_days` 时标准库内部算出具体日期——`2026-06-28`，确实是六月最后一个周日（六月最后一天是 30 号周二，往前推到周日就是 28 号）。`February/last` 自动给出该年二月的最后一天（平年 28）。这套 API 把日期算术从「手算日历」彻底解放出来。

### hh_mm_ss：把 duration 拆成时分秒

一天之内的时长用 `hh_mm_ss` 拆成「时-分-秒」三段，避免自己 `% 3600` 算来算去：

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
using namespace std::chrono;

int main() {
    auto hms = hh_mm_ss{4h + 30min + 7s + 500ms};
    std::cout << "hh_mm_ss = " << hms << '\n';
    std::cout << "  hours   = " << hms.hours().count() << '\n';
    std::cout << "  minutes = " << hms.minutes().count() << '\n';
    std::cout << "  seconds = " << hms.seconds().count() << '\n';
    return 0;
}
```

```text
hh_mm_ss = 04:30:07.500
  hours   = 4
  minutes = 30
  seconds = 7
```

`hh_mm_ss` 接受任意 `duration`，自动拆成时分秒（带小数秒，如果原 duration 精度比秒高）。它还能正确处理负时长（前面带负号），以及超过 24 小时的时长（`hours()` 可能 > 24）。做协议解析、倒计时显示时，它比自己写除法干净得多。

## C++20 时区：本机支持情况实测

C++20 的时区支持是 chrono 最后一块拼图。标准库要实现时区，**需要一份时区数据库**——在 Linux 上这通常是系统的 `/usr/share/zoneinfo/`（由 `tzdata` 包提供）。这意味着 C++20 时区功能**依赖运行环境的时区数据**，不是纯编译期就能用的。

先看核心类型：

- `time_zone`：一个时区（如 `Asia/Shanghai`），通过 `locate_zone(name)` 查找；
- `zoned_time`：把一个 `sys_time`（UTC 时间点）绑定到一个时区，得到当地时间的表示；
- `current_zone()`：返回本机当前时区。

我们本机（WSL2 Linux，已装 tzdata）实测：

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
using namespace std::chrono;

int main() {
    try {
        auto now = system_clock::now();
        std::cout << "current_zone: " << current_zone()->name() << '\n';

        // 同一个时间点，映射到不同时区
        auto sh = locate_zone("Asia/Shanghai");
        auto ny = locate_zone("America/New_York");
        auto utc = locate_zone("UTC");
        std::cout << "Shanghai = " << zoned_time{sh, now} << '\n';
        std::cout << "New York = " << zoned_time{ny, now} << '\n';
        std::cout << "UTC      = " << zoned_time{utc, now} << '\n';
    } catch (const std::exception& e) {
        std::cout << "EXCEPTION: " << e.what() << '\n';
    }
    return 0;
}
```

```text
current_zone: Asia/Shanghai
Shanghai = 2026-06-22 22:47:05.285182321 CST
New York = 2026-06-22 10:47:05.285182321 EDT
UTC      = 2026-06-22 14:47:05.285182321 UTC
```

同一个 UTC 时间点，映射到三个时区得到三个当地时间，而且自动带上了时区缩写（`CST`、`EDT`、`UTC`）——纽约是 `EDT`（Eastern Daylight Time，夏令时），说明 chrono 连夏令时（DST）规则都正确处理了（6 月纽约在夏令时内）。这一切都是基于系统的 zoneinfo 数据算出来的。

::: warning 时区数据库要靠运行环境提供
`current_zone()` / `locate_zone()` 依赖系统时区数据库。如果环境没装 tzdata（比如某些极简容器、嵌入式 Linux），这些调用会**抛 `std::runtime_error`**，程序直接挂。GCC 16.1.1 的 libstdc++ **不内置时区数据**，全部读 `/usr/share/zoneinfo/`。打包部署到精简环境时，要么确保 `tzdata` 装上，要么把时区功能做成可选、做好异常兜底。本机这台 WSL2 已经装好了，所以上面一切正常；换台裸容器可能就 `EXCEPTION: Timezone database not available` 了。
:::

时区这块最实用的场景是「服务部署在 UTC，但日志/前端要显示用户当地时区」。有了 `zoned_time`，你把所有时间戳以 UTC 存（用 `system_clock` + `sys_time<seconds>`），展示时再 `zoned_time{user_tz, utc_tp}` 转当地时区，一套类型安全的流水线，不用自己写 `+8 小时` 这种硬编码偏移（一旦涉及夏令时，硬编码偏移必错）。

## chrono 的格式化：和 std::format 对上

C++20 给 chrono 加了格式化支持，机制和上一篇讲的通用 `std::format` 是同一套——`%` 引导的 chrono 格式说明符，塞进 `std::format` 的 `{}` 占位符里。两者底层共用 `std::formatter` 的特化机制：标准库给 `sys_time`、`year_month_day`、`duration`、`weekday` 等 chrono 类型都特化了 `formatter`，所以它们能直接进 `std::format`，不用你自己写扩展。

通用 `std::format` 的语法（`{}` 占位符、编译期类型检查、字面量格式串）我们在[上一篇](../strings/52-format.md)讲透了，这里只讲 chrono 特化用到的 `%` 说明符。最常用的一组：

```cpp
// Standard: C++20
#include <chrono>
#include <format>
#include <iostream>
using namespace std::chrono;

int main() {
    auto sys = sys_days{2026y / June / 22d};
    std::cout << std::format("{:%Y-%m-%d}\n", sys);          // 2026-06-22
    std::cout << std::format("{:%A %B %d, %Y}\n", sys);      // Monday June 22, 2026
    std::cout << std::format("{:%Y年%m月%d日}\n", sys);       // 2026年06月22日

    // 带时间的 time_point
    auto tp = sys + 15h + 30min + 7s;
    std::cout << std::format("{:%Y-%m-%d %H:%M:%S}\n", tp);  // 2026-06-22 15:30:07
    std::cout << std::format("{:%F %T}\n", tp);              // %F=%Y-%m-%d, %T=%H:%M:%S
    std::cout << std::format("{:%R}\n", tp);                 // %H:%M -> 15:30

    // 12 小时制
    auto pm = sys + 21h + 5min;
    std::cout << std::format("{:%I:%M %p}\n", pm);           // 09:05 PM
    return 0;
}
```

```text
2026-06-22
Monday June 22, 2026
2026年06月22日
2026-06-22 15:30:07
2026-06-22 15:30:07
15:30
09:05 PM
```

几个高频说明符记一下：`%Y` 年、`%m` 月（补零）、`%d` 日、`%H` 时（24 小时）、`%M` 分、`%S` 秒、`%A` 星期全名、`%B` 月全名、`%p` AM/PM、`%I` 时（12 小时）。还有两个**组合**说明符特别常用：`%F` 等价于 `%Y-%m-%d`、`%T` 等价于 `%H:%M:%S`，写日志时间戳几乎都是 `{:%F %T}`。

`%c` 是 locale 相关的完整日期时间表示（`Mon Jun 22 15:30:07 2026` 这种），`%x` / `%X` 分别是 locale 的日期 / 时间表示——做国际化时有用，但依赖 locale 设置。

::: warning 通用 format 语法归上一篇
`{}` 占位符、位置参数、编译期类型检查、运行期格式串走 `vformat` 这些通用机制，都在 [format 那篇](../strings/52-format.md)讲过了。本篇只讲 chrono 特化的 `%` 说明符，别在这篇找 `{:>10}` 这种通用对齐语法——它对 chrono 类型一样适用（`std::formatter` 特化复用了通用解析），但属于通用 format 的范畴。
:::

### 反向：parse 把字符串变回时间点

有格式化输出，就有反向的解析。`std::chrono::parse`（C++20，配合 `>>` 或 `parse` 函数）用同一套 `%` 说明符，把字符串变回 `time_point` 或 `duration`：

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
#include <sstream>
using namespace std::chrono;

int main() {
    std::istringstream iss{"2026-06-22 15:30:07"};
    sys_time<seconds> tp;
    iss >> parse("%F %T", tp);
    if (iss) std::cout << "parsed sys_time: " << tp << '\n';

    std::istringstream ds{"10:30:45"};
    seconds dur;
    ds >> parse("%H:%M:%S", dur);
    if (ds) std::cout << "parsed duration: " << dur << '\n';
    return 0;
}
```

```text
parsed sys_time: 2026-06-22 15:30:07
parsed duration: 37845s
```

`parse` 解析失败时流会进入失败状态（和 `>>` 一致），用 `if (iss)` 判断。注意它解析出来的是 UTC `sys_time`，不是当地时间——如果要解析当地时间戳，得自己处理时区偏移（或用 `local_time` + `time_zone` 的 `to_sys` 转换）。做日志回放、协议解析时，`parse` 比手写 `strptime` + `mktime` 类型安全得多。

## C++23：print 直接吃 chrono，时区泄漏修复

C++23 给 chrono 补了两笔，都是顺水推舟的小改进。

第一笔是 `std::print` / `std::println`（C++23）**直接消费 chrono 类型**，不用再套一层 `std::format`。上一篇讲过 `std::print` 内部就是 `std::format`，这里 chrono 格式化同样享受这条捷径：

```cpp
// Standard: C++23
#include <chrono>
#include <print>
using namespace std::chrono;

int main() {
    auto sys = sys_days{2026y / June / 22d} + 15h + 30min;
    std::println("{:%Y-%m-%d %H:%M:%S}", sys);
    std::println("duration = {}", 1500ms);
    return 0;
}
```

```text
2026-06-22 15:30:00
duration = 1500ms
```

`println` 的格式串和 `std::format` 完全一致，`%` 说明符照用。注意第二行 `duration` 不带 `%` 直接 `{}`，标准库给 `duration` 特化了默认格式化（按其 period 单位输出，`1500ms`）。`print` / `println` 本身的机制（流式输出、省掉中间 `std::string`）在 [53-print 那篇](../strings/53-print.md)详讲，这里只点 chrono 的接入。

第二笔是 C++23 对 chrono 时区处理的若干修复（P1654 等），主要堵了「`zoned_time` 在某些构造路径下会泄漏时区指针」之类的边界问题，属于库实现的健壮性提升，日常用法不受影响。如果你的代码大量用 `zoned_time` 做长生命周期对象，升级到支持 C++23 的工具链能踩更少的坑。

::: warning 老 GCC 上 chrono 的 C++20 部分未必全
chrono 的 C++20 部分（日历、时区、格式化）在 GCC 里是逐步落地的：日历和格式化 GCC 11 起基本可用，但**时区（`zoned_time` / `current_zone`）直到 GCC 14 才完整实现**（需要 `<chrono>` 配合时区数据库）。本机 GCC 16.1.1 实测全部可用（日历、时区、格式化、`parse` 都跑通）。如果你的项目要支持 GCC 13 及更早，时区功能基本用不了，得退回 `date` 库（Howard Hinnant 的 `date`，正是 chrono C++20 日历时区的原型）。跨工具链部署前，先确认目标工具链的 chrono 支持范围。
:::

## 几个真实容易踩的点

把这一路容易翻车的位置集中收一下，每个都对应上面的实测：

::: warning 测耗时只能用 steady_clock
`system_clock` 是墙钟，会被 NTP 步进校正（甚至往回拨），两次读数的 delta 可能是负数或荒谬地小。`steady_clock` 用 `CLOCK_MONOTONIC`，内核保证单调递增。生产日志里出现「耗时 -300ms」的负数，九成是用了 `system_clock` 测耗时。**测耗时、测间隔 → `steady_clock`；打现实时间戳、对账 → `system_clock`**，两件事别混，连 epoch 都不一样，不能互相转换。
:::

::: warning high_resolution_clock 不是 steady_clock 的别名
老资料常说「`high_resolution_clock` 在 libstdc++ 是 `steady_clock` 的别名」，这在 GCC 16.1.1 上**不成立**——它是 `system_clock` 的别名（源码 `using high_resolution_clock = system_clock;`），`is_steady == false`。用它测耗时，等于在用会被跳变的 `system_clock`，前面的坑一个不落。标准 C++20 起明确建议弃用 `high_resolution_clock`。**当它不存在，只用 `system_clock` 和 `steady_clock`**。
:::

::: warning duration 之间的隐式转换只走无损方向
`seconds s = milliseconds{500}` 编不过，因为 500ms 变秒会丢精度（500/1000 不整）。要截断必须显式 `duration_cast<seconds>(ms)`。反向（大单位变小单位，如 `milliseconds m = seconds{2}`）才允许隐式。这条规则是编译器替你挡住「静默丢精度」的 bug，别嫌它烦。
:::

::: warning duration_cast 默认截断，不是四舍五入
`duration_cast<seconds>(1750ms)` 得 `1s`（截断），不是 `2s`。需要别的取整语义用 `floor` / `ceil` / `round`（`round` 是银行家舍入，半数取偶数）。想完全避免整数截断，用 `duration<double>` 存浮点秒。
:::

::: warning 时区功能依赖运行环境的 tzdata
`current_zone()` / `locate_zone()` 在没装 tzdata 的环境（极简容器、裸嵌入式 Linux）会抛 `std::runtime_error`。libstdc++ 不内置时区数据，全读 `/usr/share/zoneinfo/`。部署到精简环境前确认 tzdata 在位，或做好异常兜底。
:::

::: warning system_clock 的 epoch 是 1970-01-01 UTC
`system_clock::time_point` 的 epoch 是 Unix 纪元（1970-01-01 00:00:00 UTC），所以能直接和 `time_t`、文件系统时间戳、网络协议时间戳互转。`steady_clock` 的 epoch 是实现定义的（通常系统启动时刻），**值本身没有现实含义**，只能用来算 delta。别把 `steady_clock` 的 `time_since_epoch()` 当成「现实时刻」用，那只是「自系统启动以来多久」。
:::

## 小结

chrono 这个库的设计可以一句话概括：**把「时间」拆成 duration / time_point / clock 三类，用编译期分数算术保精度、用类型系统挡误用**。几条关键结论收一下：

- **duration**：刻度数 `Rep` × 周期 `Period`（编译期 `ratio`）。字面量 `h/min/s/ms/us/ns`；`ratio<2,3>` 这种非整周期也能精确表示（1.5Hz 的 2/3 秒周期），所有单位换算编译期完成。隐式转换只走无损方向，有损必须显式 `duration_cast`（默认截断）；`floor` / `ceil` / `round` 提供其它取整语义。

- **clock**：测耗时只能用 `steady_clock`（`is_steady == true`，单调递增，底层 `CLOCK_MONOTONIC`，不受 NTP 影响）；`system_clock` 是墙钟（epoch 是 Unix 纪元，可被 NTP 跳变，测耗时会出负数），打现实时间戳用；`high_resolution_clock` 是 `system_clock` 的别名（GCC 16.1.1 实测），标准已建议弃用，当它不存在。

- **C++20 日历**：`year/month/day` 独立类型 + 字面量（`2026y/June/22d`），`year_month_day` 带 `.ok()` 校验（闰年自动查），`weekday` 两套编码，`hh_mm_ss` 拆时分秒，`Sunday[last]` / `February/last` 表达「最后一个周几」「最后一天」这类相对日期，全部可编译期构造。

- **C++20 时区**：`time_zone` / `zoned_time` / `current_zone()` 依赖系统 tzdata，本机（WSL2 + tzdata）实测可用，能正确处理夏令时；部署到精简环境前确认 tzdata 在位。生产实践：时间戳以 UTC 存（`sys_time`），展示时 `zoned_time` 转当地时区，别硬编码 `+8` 偏移。

- **格式化**：chrono 类型特化了 `std::formatter`，直接进 `std::format`，用 `%` 说明符（`%Y-%m-%d %H:%M:%S` / `%F %T` 等），和[通用 format](../strings/52-format.md) 共用同一套 `{}` 机制；反向用 `std::chrono::parse` 解析。C++23 的 `std::println` 直接消费 chrono 类型，省掉中间 `std::string`。

- **epoch**：`system_clock` 是 1970-01-01 UTC（可与 `time_t` 互转）；`steady_clock` 是实现定义（通常系统启动），值无现实含义，只算 delta。

下一篇我们看标准库里另一个和时间/系统打交道的组件——`<filesystem>`，它怎么遍历目录、怎么跨平台抽象文件路径，把「文件系统操作」也纳入类型安全的轨道。

## 参考资源

- [cppreference: Chrono library](https://en.cppreference.com/w/cpp/chrono) —— chrono 全家族总览
- [cppreference: std::duration](https://en.cppreference.com/w/cpp/chrono/duration) —— duration 与 `ratio` 的编译期分数运算
- [cppreference: std::chrono::steady_clock](https://en.cppreference.com/w/cpp/chrono/steady_clock) —— `is_steady` 语义，测耗时的正确选择
- [cppreference: std::chrono::high_resolution_clock](https://en.cppreference.com/w/cpp/chrono/high_resolution_clock) —— 为什么标准建议弃用它
- [cppreference: C++20 calendar](https://en.cppreference.com/w/cpp/chrono#Calendar) —— `year_month_day` / `weekday` / `last` 日历类型
- [cppreference: std::chrono::zoned_time](https://en.cppreference.com/w/cpp/chrono/zoned_time) —— 时区与本地时间
- [cppreference: chrono formatting](https://en.cppreference.com/w/cpp/chrono/format) —— `%` 说明符完整表
- [Howard Hinnant: date 库](https://github.com/HowardHinnant/date) —— chrono C++20 日历时区的原型，老工具链的 polyfill
