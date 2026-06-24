---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 'Here is the translation of the description, adhering to the technical
  style and terminology guidelines:


  A deep dive into `chrono`: why the compile-time fractional arithmetic of `duration`
  can calculate a period of 1.5Hz, why `steady_clock` is the only correct choice for
  measuring elapsed time while `system_clock` can be tripped up by NTP jumps, practical
  applications of C++20 calendars (`year`/`month`/`day`/`Sunday[last]`) and time zones
  (`zoned_time`), as well as `chrono`-specific formatting.'
difficulty: advanced
order: 58
platform: host
prerequisites:
- format：C++20 的类型安全格式化
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 22
related:
- format：C++20 的类型安全格式化
- <numeric>：累加、填充、内积与相邻差
tags:
- host
- cpp-modern
- advanced
- 基础
title: 'chrono: duration, Clocks, and C++20 Calendars'
translation:
  source: documents/vol3-standard-library/time-numeric/58-chrono.md
  source_hash: 2f42ec4087ce9706e85098e230750972376d1a55a7da54bc6501aca73bffb3d3
  translated_at: '2026-06-24T00:51:00.108718+00:00'
  engine: anthropic
  token_count: 6079
---
# chrono: Durations, Clocks, and C++20 Calendars

Time seems simple enough—just grab the seconds with `gettimeofday` and subtract them, right? But in real-world engineering, the pitfalls of time handling can make you question your existence: Why does measuring code execution time occasionally yield a **negative number**? Why does the same timestamp print differently on different machines? Do we really have to manually calculate leap years and days of the week for requirements like "the last Sunday of June 2026"?

The `<chrono>` library exists to fill these pits once and for all. Its design philosophy is hardcore—**it strictly distinguishes between three concepts: "a duration," "a time point," and "a clock," moving all unit conversions and precision loss calculations to compile-time using fraction arithmetic.** It was introduced in C++11, but for a long time, most developers only dared to use the "measure execution time with `steady_clock`" feature. It wasn't until C++20 completed the library with calendars, time zones, and formatting that chrono truly became a complete solution capable of handling production logs, scheduling, and protocol timestamps.

In this article, we will dissect chrono thoroughly: First, we look at how `duration` uses compile-time fraction arithmetic to achieve logic like "1.5 Hz periods are computable, but 500ms cannot implicitly become 1s." Next, we examine why only `steady_clock` among the three clocks is suitable for measuring elapsed time—revealing a real pitfall often misrepresented in older documentation. Then, we move into C++20 calendars and time zones to see how syntax like `2026y/June/Sunday[last]` is validated at compile time. Finally, we cover chrono-specific formatting and connect it with the generic `std::format` discussed in the previous article. We will test local time zone support throughout rather than making empty assertions.

## duration: Calculating "A Span of Time" with Compile-Time Fractions

`duration` is the foundation of chrono. A one-sentence definition: **a tick count multiplied by a tick period.** The `period` is a `std::ratio`, a compile-time fraction describing "how many seconds one tick equals."

```cpp
// 标准库里的真身（简化）
template <typename Rep, typename Period = std::ratio<1>>
class duration {
    Rep rep_;   // 刻度数，通常是 int/long long/double
};
```

`Rep` is the type used for the counter, and `Period` is the duration of one tick in seconds. Therefore, `seconds` is `duration<long long, ratio<1>>` (one tick equals one second), and `milliseconds` is `duration<long long, ratio<1, 1000>>` (one tick equals one thousandth of a second), and so on.

Let's first explore literals and the most basic usage:

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

`.count()` extracts the stored number of ticks. The literals `h`/`min`/`s`/`ms`/`us`/`ns` reside in the `std::chrono_literals` namespace (which is included automatically with `using namespace std::chrono`), making them much more intuitive to write than `milliseconds{16}`.

### Compile-time Fraction Arithmetic: Calculating the Period for 1.5Hz

The true power of the chrono design lies in the fractional arithmetic of the `Period` template parameter. The standard library's `seconds` and `milliseconds` use periods that are negative powers of 10 (1, 1/1000, 1/1000000), but real-world periods aren't always integers. For example, the NTSC video frame rate is `60000/1001 ≈ 59.94` fps, and a "1.5Hz" cycle period is `1/1.5 = 2/3` seconds. While traditional approaches would require storing this as a `double`, chrono allows us to **represent this precisely using compile-time fractions**:

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

Here are a few details to note. First, `ratio<2, 3>` is evaluated at compile time. The type `duration<long long, ratio<2, 3>>` encodes "period = 2/3 seconds" directly into the type system, resulting in zero runtime overhead—`fd` is simply a `long long`. Second, `ratio` automatically reduces fractions; `ratio<6, 4>` is normalized to `3/2` (`num=3, den=2`) in the standard library by calculating the greatest common divisor using `constexpr`. Third, casting a `2/3` second `duration` to seconds via `duration_cast` will **truncate to 0** (an integer `long long` cannot represent 0.666), which illustrates the precision loss issue we will discuss shortly.

When adding `duration`s with different periods, the resulting period automatically takes the common denominator (the least common multiple of the two periods acts as the denominator). We don't need to worry about the units for `1s + 500ms`:

```cpp
auto sum = seconds{1} + milliseconds{500};
// sum 的类型是 milliseconds，count() == 1500
```

This compile-time fractional arithmetic is the core differentiator between `<chrono>` and the naive approach of "storing a `double` for seconds": **all unit conversions happen at the type level, integer arithmetic preserves precision, and the compiler calculates the least common denominator for you.** The cost is ugly template error messages, but the payoff is type safety.

### Implicit Conversions: Why 500ms Cannot Become 1s

There is a strict rule for implicit conversions between `duration`s: **only "lossless" directions are allowed**. When converting from a "smaller unit" (higher precision) to a "larger unit" (lower precision), an implicit conversion is permitted only if the tick count is exactly divisible; otherwise (if precision would be lost), compilation fails. Conversely, converting from a "larger unit" to a "smaller unit" (increasing precision, such as 1s to 1000ms) is always allowed.

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

Compiling with GCC 16.1.1, this line **fails to compile directly**:

```text
implicit.cpp:7:17: error: conversion from
  'duration<[...],ratio<[...],1000>>' to 'duration<[...],ratio<[...],1>>'
  requested
```

Conversely, `milliseconds m = seconds{2}` (2s -> 2000ms, increased precision) works implicitly. This rule has significant practical value: **the compiler prevents insidious precision bugs where you "unintentionally lose half a second."** If you truly intend to truncate, you must explicitly write `duration_cast<seconds>(ms)`—turning "precision loss" from a silent bug into a distinct, visible operation.

### duration_cast versus ceil / floor / round

`duration_cast` defaults to **truncation** (rounding towards zero). Converting `1750ms` to `seconds` yields `1s`, discarding the remaining half second. If your application requires a different rounding strategy, chrono provides three functions: `floor`, `ceil`, and `round` (available since C++17):

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

`floor` rounds down, `ceil` rounds up, and `round` rounds to the nearest integer (halfway cases round to the nearest even number, so `1250ms` rounds to `1s` instead of `2s`—this is banker's rounding, which avoids cumulative bias). Together with the default truncation, these four cover all rounding semantics. In timing scenarios, if we need to calculate "how many complete 16ms intervals have passed in this frame," we should use `floor`; if we need to calculate "how many buffers must be allocated at a minimum," we should use `ceil`. The distinction is clear.

There is another way to bypass integer truncation: use `double` as the `Rep`. `duration<double>{1.5}` directly represents 1.5 seconds, and `duration<double, milli>` represents millisecond-precision floating-point numbers. Calculations do not lose precision (though the trade-off is the inherent precision limit of floating-point numbers). This trick is commonly used in scientific computing and for profiling execution time distributions.

## time_point and the Three Clocks: Why Only steady_clock Is Suitable for Measuring Duration

`duration` represents a "span of time," while `time_point` represents a "specific moment." Its definition is simple: **an epoch (starting point) of a specific clock plus a duration**.

```cpp
// 简化
template <typename Clock, typename Duration = typename Clock::duration>
class time_point {
    Duration since_epoch_;
};
```

`time_point` is bound to a `Clock`. `time_point` objects from different clocks cannot be directly subtracted (the types differ, so the compiler blocks this). This design specifically prevents mixing "wall clock time" with "monotonic time".

So, what is a "clock"? The standard library provides three, and **choosing the right one** is the easiest pitfall to stumble into with chrono. Let's first measure and clarify their properties.

### Real-world properties of the three clocks

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

The meaning of `is_steady` is "monotonically increasing, never moving backward." Let's compare the three:

- **`system_clock`**: This is a wall clock, representing "what time is it" in the real world. Its `is_steady == false`—because the system adjusts it via NTP (Network Time Protocol) or manual `date` commands: NTP will **slew** (gradually adjust) to correct clock drift, but if the clock is too fast, it will also **step** (jump), or even move backward. The epoch of `system_clock` is the Unix epoch (1970-01-01 00:00:00 UTC). It can be directly converted to and from `time_t`, making it suitable for timestamps and reconciliation with the external world.
- **`steady_clock`**: A monotonic clock with `is_steady == true`, **guaranteed to never move backward**. Its epoch is arbitrary (implementation-defined, typically system boot time). The value itself has no real-world meaning, but **subtracting two readings is guaranteed to be >= 0**. This is exactly the invariant needed for measuring time intervals.
- **`high_resolution_clock`**: The standard says it is the "clock with the smallest tick," but **does not mandate whether it is steady**. In practice, it is an alias for some other clock (implementation-defined).

There is a pitfall with the third clock that is widely misrepresented in older materials, so let's dig into it separately.

::: warning high_resolution_clock is an alias, and usually not the steady one
Many tutorials and blogs claim that "`high_resolution_clock` is an alias for `steady_clock` on libstdc++". This statement **no longer holds true on GCC 16.1.1**. Let's test to see which clock it is actually an alias for:

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

In GCC 16.1.1's libstdc++, `high_resolution_clock` **is simply an alias for `system_clock`** (the source code literally says `using high_resolution_clock = system_clock;`), so its `is_steady == false`. This means that if you follow older tutorials and use `high_resolution_clock` to measure execution time, you are actually using `system_clock`, which is subject to NTP adjustments. You will fall right into the trap of "negative duration" measurements described below.

The C++ standard itself, since C++20, **explicitly recommends against using `high_resolution_clock`**. It is a legacy transitional artifact with implementation-defined behavior and inconsistent cross-platform behavior. Remember this rule: **Always use only `system_clock` and `steady_clock`. Pretend `high_resolution_clock` does not exist.**
:::

### Real-world Test: Why `system_clock` Fails at Measuring Duration

Talk is cheap. Let's run the code. We will busy-wait the CPU for 200 ms and measure it using both clocks:

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

Under normal circumstances, both measure approximately 200 ms, appearing identical. However, **the real pitfall lies in the exceptional path**: if the system clock is stepped back by NTP while `busy` is executing (for example, to correct a fast-running clock), `t1 - t0` will become negative or absurdly small. `steady_clock` uses `CLOCK_MONOTONIC` on Linux (a kernel monotonic clock source), which the kernel guarantees will always increase monotonically and **can never go backward**, ensuring `t1 - t0` is always >= 0.

This issue is difficult to reproduce stably in a user-space program (you can't casually step the system clock back, as that requires root privileges and disrupts the entire machine), but it **genuinely happens** in production environments: NTP step corrections, clock jumps caused by container migration, and time drift in virtualized environments can all distort the delta between two `system_clock` readings. If you occasionally see negative numbers like "this code took -340ms" in logs, it is 90% likely that `system_clock` was used for measuring duration.

Therefore, the iron rule is: **for measuring duration or intervals, always use `steady_clock`**; use `system_clock` for real-world timestamps and reconciliation with external systems. Do not mix the two. If you absolutely need to convert a "measured duration" into a "wall-clock time" (e.g., for logging), use `steady_clock` to calculate the delta and `system_clock` to record a separate start point. Let each handle its own task—do not convert between them, as their epochs differ, making any conversion incorrect.

## C++20 Calendars: Turning Dates into Types

With C++20, chrono added a comprehensive set of calendar types, transforming "June 22, 2026" from a clump of `tm` structs and hand-written `strftime` calls into **type-safe, compile-time constructible objects**. This section is the highlight of C++20 chrono.

The core types are a set of "calendar fields," each being an independent class:

- `year`, `month`, `day`: Three independent types for year, month, and day;
- `year_month_day`: A combined date;
- `weekday`: Day of the week;
- `hh_mm_ss`: Time within a day (hour-minute-second);
- `month_day`, `year_month`, `month_weekday`, and other "partial date" types.

Combined with literals (`2026y`, `June`, `22d`), writing dates is just like writing ordinary expressions:

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

The expression `2026y / June / 22d` looks like division, but it is actually operator overloading—`year / month` yields `year_month`, and then `/ day` yields `year_month_day`. The entire chain of construction is `constexpr`, so `ymd` can serve as a compile-time constant. `sys_days` is an alias for `time_point<system_clock, days>`, representing "days since the Unix epoch." It converts a calendar date into a `time_point` capable of time arithmetic—this is the bridge between the calendar and the clock.

`weekday` has two encodings: `c_encoding()` (Sunday = 0, C style) and `iso_encoding()` (Monday = 1, Sunday = 7, ISO 8601 style). Modern code predominantly uses ISO encoding, because "day of the week" in business logic is usually understood as "Monday through Sunday."

### Built-in Validation

Calendar types include a built-in `.ok()` method for validation—something manual date parsing can never achieve. It checks whether months and days are within valid ranges, and whether the date actually exists (for example, February 29 is illegal in a non-leap year):

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

`month{0}` (there is no 0th month) and `month{13}` (there is no 13th month) both return `ok() == false`. February 29th is invalid for the year 2026 (a common year) but valid for 2024 (a leap year). **We no longer need to write the leap year check `year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)` ourselves**—`.ok()` handles the validation for us.

### last: Specifying "the last weekday"

One of the most elegant features of the C++20 calendar is `last`. Many business requirements involve relative dates like "the last weekday of the month" or "the last Sunday in June." The traditional approach requires calculating "how many days are in this month and what day of the week it is." chrono simplifies this into a literal:

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

`Sunday[last]` is a `month_weekday_last`, representing "the last Sunday of a month." The entire expression `year{2026} / June / Sunday[last]` has the type `year_month_weekday_last`. When converted to `sys_days`, the standard library calculates the specific date internally—`2026-06-28`, which is indeed the last Sunday in June (the last day of June is the 30th, a Tuesday, so counting back to Sunday gives us the 28th). `February/last` automatically yields the last day of February for that year (the 28th in a common year). This API completely liberates date arithmetic from "manual calendar calculations."

### hh_mm_ss: Splitting Duration into Hours, Minutes, and Seconds

We use `hh_mm_ss` to split a duration within a day into three segments—"hours, minutes, and seconds"—avoiding the need to manually calculate with `% 3600`:

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

`hh_mm_ss` accepts any `duration` and automatically splits it into hours, minutes, and seconds (including fractional seconds if the original `duration` has higher precision than seconds). It also correctly handles negative durations (displaying a leading minus sign) and durations exceeding 24 hours (where `hours()` may be greater than 24). When working with protocol parsing or countdown displays, this is much cleaner than writing division logic yourself.

## C++20 Time Zones: Real-world Support Status

Time zone support in C++20 completes the `<chrono>` library. To implement time zones, the standard library **requires a time zone database**—on Linux, this is typically the system's `/usr/share/zoneinfo/` (provided by the `tzdata` package). This means C++20 time zone functionality **relies on the runtime environment's time zone data**; it is not purely a compile-time feature.

Let's look at the core types:

- `time_zone`: Represents a specific time zone (e.g., `Asia/Shanghai`), found via `locate_zone(name)`.
- `zoned_time`: Associates a `sys_time` (UTC time point) with a time zone to produce a local time representation.
- `current_zone()`: Returns the system's current time zone.

We tested this on our local machine (WSL2 Linux with `tzdata` installed):

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

The same UTC time point maps to three different time zones, yielding three distinct local times, complete with automatic time zone abbreviations (`CST`, `EDT`, `UTC`). New York shows `EDT` (Eastern Daylight Time), which indicates that `chrono` correctly handles Daylight Saving Time (DST) rules (New York is observing DST in June). All of this is calculated based on the system's zoneinfo data.

::: warning Timezone databases rely on the runtime environment
`current_zone()` and `locate_zone()` depend on the system's timezone database. If the environment lacks `tzdata` (common in minimal containers or embedded Linux), these calls will **throw a `std::runtime_error`**, causing the program to crash. GCC 16.1.1's libstdc++ **does not bundle timezone data**; it reads entirely from `/usr/share/zoneinfo/`. When deploying to stripped-down environments, ensure `tzdata` is installed, or make timezone features optional with proper exception handling. The local WSL2 machine used here has `tzdata` installed, so everything works as expected; on a bare container, you might get `EXCEPTION: Timezone database not available`.
:::

The most practical use case for time zones is when "services are deployed in UTC, but logs or frontends need to display the user's local time." With `zoned_time`, we can store all timestamps in UTC (using `system_clock` + `sys_time<seconds>`) and convert to the local time zone for display using `zoned_time{user_tz, utc_tp}`. This creates a type-safe pipeline, eliminating the need for manual, hard-coded offsets like `+8 hours` (which are guaranteed to fail once DST is involved).

## Formatting with chrono: Integrating with std::format

C++20 added formatting support for `chrono`, using the same mechanism as the generic `std::format` discussed in the previous chapter—chrono format specifiers led by `%` are placed inside `{}` placeholders. Both share the underlying `std::formatter` specialization mechanism: the standard library provides specializations for chrono types like `sys_time`, `year_month_day`, `duration`, and `weekday`, so they work directly with `std::format` without requiring custom extensions.

We covered the general `std::format` syntax (placeholders, compile-time type checking, and literal format strings) thoroughly in the [previous article](../strings/52-format.md). Here, we focus only on the `%` specifiers used for chrono specializations. The most common set is:

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

Let's note down several frequently used specifiers: `%Y` for year, `%m` for month (zero-padded), `%d` for day, `%H` for hour (24-hour), `%M` for minute, `%S` for second, `%A` for full weekday name, `%B` for full month name, `%p` for AM/PM, and `%I` for hour (12-hour). There are also two **composite** specifiers that are particularly useful: `%F` is equivalent to `%Y-%m-%d`, and `%T` is equivalent to `%H:%M:%S`. Log timestamps almost always use `{:%F %T}`.

`%c` is the locale's standard date and time representation (like `Mon Jun 22 15:30:07 2026`), while `%x` and `%X` are the locale's date and time representations, respectively. These are useful for internationalization but rely on locale settings.

::: warning General format syntax was covered in the previous article
General mechanisms such as `{}` placeholders, positional arguments, compile-time type checking, and runtime format strings via `vformat` were covered in the [format article](../strings/52-format.md). This article focuses only on chrono-specific `%` specifiers. Don't look for general alignment syntax like `{:>10}` here—while it applies to chrono types (since `std::formatter` specializations reuse the general parsing), it belongs to the general format topic.
:::

### Reverse: Parsing Strings Back into Time Points

Just as we have formatted output, we have the reverse operation: parsing. `std::chrono::parse` (C++20, used with `>>` or the `parse` function) uses the same set of `%` specifiers to convert strings back into `time_point` or `duration` objects:

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

When `parse` fails, the stream enters a failed state (consistent with `>>`), which we can check using `if (iss)`. Note that the result is a UTC `sys_time`, not the local time. To parse a local timestamp, we must handle the timezone offset manually (or use `local_time` + `time_zone`'s `to_sys` for conversion). For log replay and protocol parsing, `parse` is much more type-safe than hand-rolling `strptime` + `mktime`.

## C++23: `print` Directly Consumes Chrono, Timezone Leak Fix

C++23 adds two finishing touches to chrono, both being logical improvements.

The first is that `std::print` and `std::println` (C++23) **consume chrono types directly**, eliminating the need to wrap them in `std::format`. As discussed in the previous article, `std::print` uses `std::format` internally, so chrono formatting benefits from this shortcut as well:

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

The format string for `println` is fully consistent with `std::format`, so the `%` specifiers apply as-is. Note that in the second line, `duration` uses `{}` without a `%`; the standard library provides a specialized default formatter for `duration` (outputting based on its period, e.g., `1500ms`). The underlying mechanism of `print` / `println` (streaming output, eliminating the intermediate `std::string`) is covered in detail in [Section 53-print](../strings/53-print.md); here we only highlight the integration with chrono.

The second point involves several fixes in C++23 regarding chrono time zone handling (such as P1654). These primarily address edge cases like "`zoned_time` leaking time zone pointers under certain construction paths." This represents a robustness improvement in the library implementation and does not affect daily usage. If your code heavily uses `zoned_time` for objects with long lifecycles, upgrading to a toolchain supporting C++23 will help you avoid fewer pitfalls.

::: warning C++20 Chrono support in older GCC may be incomplete
The C++20 parts of chrono (calendars, time zones, formatting) were implemented gradually in GCC: calendars and formatting have been mostly available since GCC 11, but **time zones (`zoned_time` / `current_zone`) were not fully implemented until GCC 14** (requiring `<chrono>`配合配合 the time zone database). Testing on the local machine with GCC 16.1.1 shows full availability (calendars, time zones, formatting, and `parse` all work). If your project needs to support GCC 13 or earlier, time zone functionality is basically unusable, and you should fall back to the `date` library (Howard Hinnant's `date`, which is the prototype of the C++20 chrono calendar/time zone). Before deploying across toolchains, verify the target toolchain's chrono support scope.
:::

## Common Real-world Pitfalls

Let's round up the common places where things can go wrong, each corresponding to the tests above:

::: warning Use steady_clock for measuring elapsed time
`system_clock` is a wall-clock, subject to NTP step adjustments (or even stepping backwards), so the delta between two reads could be negative or absurdly small. `steady_clock` uses `CLOCK_MONOTONIC`, which the kernel guarantees to be monotonically increasing. If you see "elapsed -300ms" in production logs, it's 90% likely because `system_clock` was used for timing. **Measuring elapsed time/intervals → `steady_clock`; recording real-world timestamps/reconciliation → `system_clock`**. Don't mix these two; even their epochs differ, and they cannot be converted to each other.
:::

::: warning high_resolution_clock is NOT an alias for steady_clock
Older documentation often states that "`high_resolution_clock` is an alias for `steady_clock` in libstdc++". This is **not true** on GCC 16.1.1—it is an alias for `system_clock` (source code: `using high_resolution_clock = system_clock;`), with `is_steady == false`. Using it to measure time is equivalent to using `system_clock`, which is subject to jumps, falling into every trap mentioned above. Since C++20, the standard explicitly suggests deprecating `high_resolution_clock`. **Pretend it doesn't exist and only use `system_clock` and `steady_clock`**.
:::

::: warning Implicit conversion between durations only goes lossless
`seconds s = milliseconds{500}` fails to compile because converting 500ms to seconds loses precision (500/1000 is not an integer). To truncate, you must explicitly use `duration_cast<seconds>(ms)`. The reverse direction (large unit to small unit, e.g., `milliseconds m = seconds{2}`) allows implicit conversion. This rule is the compiler blocking "silent precision loss" bugs for you, so don't find it annoying.
:::

::: warning duration_cast truncates by default, it does not round
`duration_cast<seconds>(1750ms)` yields `1s` (truncated), not `2s`. For different rounding semantics, use `floor` / `ceil` / `round` (`round` uses banker's rounding, rounding half-way cases to even). To completely avoid integer truncation, use `duration<double>` to store floating-point seconds.
:::

::: warning Time zone functionality depends on the runtime environment's tzdata
`current_zone()` / `locate_zone()` will throw `std::runtime_error` in environments without tzdata installed (minimal containers, bare embedded Linux). libstdc++ does not bundle time zone data; it reads entirely from `/usr/share/zoneinfo/`. Before deploying to a stripped-down environment, confirm tzdata is present or implement exception fallbacks.
:::

::: warning system_clock's epoch is 1970-01-01 UTC
The epoch of `system_clock::time_point` is the Unix epoch (1970-01-01 00:00:00 UTC), so it can be directly converted with `time_t`, filesystem timestamps, and network protocol timestamps. The epoch of `steady_clock` is implementation-defined (usually system boot time), and **the value itself has no real-world meaning**; it can only be used to calculate deltas. Don't treat `steady_clock`'s `time_since_epoch()` as a "real-world moment"; it is merely "time since system boot."
:::

## Summary

The design of the chrono library can be summarized in one sentence: **Decompose "time" into three categories—duration / time_point / clock—using compile-time fractional arithmetic to guarantee precision and the type system to prevent misuse.** Let's round up the key conclusions:

- **duration**: A count of ticks `Rep` × a period `Period` (compile-time `ratio`). Literals `h/min/s/ms/us/ns` are available; non-integer periods like `ratio<2,3>` can also be represented precisely (e.g., a 2/3 second period for 1.5Hz). All unit conversions are completed at compile time. Implicit conversions only go in the lossless direction; lossy conversions require explicit `duration_cast` (defaults to truncation); `floor` / `ceil` / `round` provide other rounding semantics.

- **clock**: Use `steady_clock` for measuring elapsed time (`is_steady == true`, monotonically increasing, underlying `CLOCK_MONOTONIC`, unaffected by NTP). `system_clock` is a wall clock (epoch is Unix epoch, subject to NTP jumps, measuring elapsed time can yield negative numbers); use it for real-world timestamps. `high_resolution_clock` is an alias for `system_clock` (observed on GCC 16.1.1), and the standard suggests deprecation, so pretend it doesn't exist.

- **C++20 Calendar**: Independent types for `year/month/day` + literals (`2026y/June/22d`), `year_month_day` includes `.ok()` validation (leap years checked automatically), `weekday` has two encoding schemes, `hh_mm_ss` decomposes hours, minutes, and seconds, `Sunday[last]` / `February/last` express relative dates like "last weekday" or "last day". All can be constructed at compile time.

- **C++20 Time Zones**: `time_zone` / `zoned_time` / `current_zone()` depend on system tzdata. Tested locally (WSL2 + tzdata) and available, correctly handling Daylight Saving Time. Before deploying to minimal environments, confirm tzdata is present. Production practice: store timestamps in UTC (`sys_time`), convert to local time zone using `zoned_time` for display, and avoid hardcoding `+8` offsets.

- **Formatting**: Chrono types specialize `std::formatter`, working directly with `std::format` using `%` specifiers (`%Y-%m-%d %H:%M:%S` / `%F %T`, etc.), sharing the same `{}` mechanism as [generic format](../strings/52-format.md); parsing in reverse uses `std::chrono::parse`. C++23's `std::println` consumes chrono types directly, saving the intermediate `std::string`.

- **epoch**: `system_clock` is 1970-01-01 UTC (convertible with `time_t`); `steady_clock` is implementation-defined (usually system boot), the value has no real-world meaning and is only for calculating deltas.

In the next section, we will look at another standard library component that interacts with time and the system—`<filesystem>`. We will see how it traverses directories, abstracts file paths cross-platform, and brings "filesystem operations" into the realm of type safety.

## Reference Resources

- [cppreference: Chrono library](https://en.cppreference.com/w/cpp/chrono) — Overview of the entire chrono family
- [cppreference: std::duration](https://en.cppreference.com/w/cpp/chrono/duration) — Compile-time fractional arithmetic with `duration` and `ratio`
- [cppreference: std::chrono::steady_clock](https://en.cppreference.com/w/cpp/chrono/steady_clock) — `is_steady` semantics, the correct choice for measuring elapsed time
- [cppreference: std::chrono::high_resolution_clock](https://en.cppreference.com/w/cpp/chrono/high_resolution_clock) — Why the standard suggests deprecating it
- [cppreference: C++20 calendar](https://en.cppreference.com/w/cpp/chrono#Calendar) — `year_month_day` / `weekday` / `last` calendar types
- [cppreference: std::chrono::zoned_time](https://en.cppreference.com/w/cpp/chrono/zoned_time) — Time zones and local time
- [cppreference: chrono formatting](https://en.cppreference.com/w/cpp/chrono/format) — Complete table of `%` specifiers
- [Howard Hinnant: date library](https://github.com/HowardHinnant/date) — The prototype of C++20 chrono calendar/time zones, a polyfill for older toolchains
