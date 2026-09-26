---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: A deep dive into chrono—why duration's compile-time fractional arithmetic
  can compute the period of 1.5Hz, why steady_clock is the only correct choice for
  measuring elapsed time while system_clock gets burned by NTP jumps, hands-on coverage
  of the C++20 calendar (year/month/day/Sunday[last]) and time zones (zoned_time),
  plus chrono's specialized format support
difficulty: advanced
order: 58
platform: host
prerequisites:
- 'format: Type-Safe Formatting in C++20'
- 'Deep Dive into vector: Three Pointers, Reallocation, and Iterator Invalidation'
reading_time_minutes: 22
related:
- 'format: Type-Safe Formatting in C++20'
- 'numeric: Accumulate, Fill, Inner Product, and Adjacent Difference'
tags:
- host
- cpp-modern
- advanced
- 基础
title: 'chrono: duration, Clocks, and C++20 Calendars'
translation:
  source: documents/vol3-standard-library/time-numeric/58-chrono.md
  source_hash: 2f42ec4087ce9706e85098e230750972376d1a55a7da54bc6501aca73bffb3d3
  translated_at: '2026-09-26T00:06:46+00:00'
  engine: anthropic
  token_count: 8200
---

# chrono: duration, Clocks, and C++20 Calendars

Time looks about as plain as it gets—call `gettimeofday`, grab some seconds, subtract, done. Right? But once real engineering enters the picture, time's pitfalls will make you question your life choices: why does measuring a code segment's elapsed time occasionally come out **negative**? Why does the same timestamp print as different strings on different machines? And a requirement like "the last Sunday of June 2026"—are you really supposed to hand-compute leap years and weekdays yourself?

`<chrono>` is the library that plugs all of these holes at once. Its design philosophy is thoroughly hardcore: **it strictly separates three concepts—"a span of time", "a point in time", and "a clock"—into distinct types, and pushes every unit conversion and precision loss to compile time, where fractional arithmetic settles them**. C++11 introduced it, but for a long stretch people only dared use the one small corner of "steady_clock for timing"; it wasn't until C++20 filled in calendars, time zones, and formatting that chrono finally grew into a complete library able to carry production logging, scheduling, and protocol timestamps.

In this article we take chrono apart down to the gears: first, how `duration` uses compile-time fractional arithmetic to achieve "the period of 1.5Hz is computable, while 500ms cannot implicitly become 1s"; then the three `clock`s and why only `steady_clock` can measure elapsed time—a real pitfall here that older material steers you wrong on; then into C++20's calendars and time zones, to see how an expression like `2026y/June/Sunday[last]` manages to be compile-time legal; and finally chrono's specialized formatting, which lines up with the previous article's generic `std::format`. Throughout, we test the machine's actual time-zone support hands-on—no armchair assertions.

## duration: A Span of Time as Compile-Time Fractions

`duration` is chrono's foundation. The one-sentence definition: **a number of ticks (the count) times a tick period (the period)**. `period` is a `std::ratio`—a compile-time fraction describing "how many seconds one tick equals".

```cpp
// The real thing in the standard library (simplified)
template <typename Rep, typename Period = std::ratio<1>>
class duration {
    Rep rep_;   // the tick count, usually int/long long/double
};
```

`Rep` is "what type stores the count"; `Period` is "how many seconds one tick is". So `seconds` is `duration<long long, ratio<1>>` (one tick is 1 second), `milliseconds` is `duration<long long, ratio<1, 1000>>` (one tick is one thousandth of a second), and so on down the line.

First, a feel for the literals and the most basic usage:

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>

using namespace std::chrono;

int main() {
    auto half_hour = 30min;     // duration<...minutes>
    auto one_sec   = 1s;        // duration<...seconds>
    auto frame     = 16ms;      // 16 milliseconds, a typical frame time
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

`.count()` digs out the tick count stored inside. The literals `h`/`min`/`s`/`ms`/`us`/`ns` all live in the `std::chrono_literals` namespace (`using namespace std::chrono` brings them along for the ride), and they read far more naturally than `milliseconds{16}`.

### Compile-Time Fraction Arithmetic: Computing the Period of 1.5Hz

What really shows off chrono's design chops is `Period`'s fractional arithmetic. The standard library's built-in `seconds` / `milliseconds` periods are all negative powers of ten (1, 1/1000, 1/1000000), but real-world periods are not all integers—the frame rate of NTSC video is `60000/1001 ≈ 59.94` fps, and the period of a "1.5Hz" loop is `1/1.5 = 2/3` of a second. A period like "two seconds split into three equal parts" traditionally could only be stored as a `double`, but chrono can **represent it exactly as a compile-time fraction**:

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
#include <ratio>

using namespace std::chrono;

// the period of 1.5Hz = 2/3 s
using frame_period   = std::ratio<2, 3>;
using frame_duration = duration<long long, frame_period>;

int main() {
    frame_duration fd{1};   // 1 tick, which is exactly 2/3 s
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

Note a few details. First, `ratio<2, 3>` is compile-time: the very type `duration<long long, ratio<2, 3>>` encodes "period = 2/3 second" into the type system, with zero runtime overhead—`fd` is just a `long long`. Second, `ratio` reduces automatically: `ratio<6, 4>` is normalized to `3/2` inside the standard library (`num=3, den=2`), via a `constexpr` greatest common divisor. Third, `duration_cast`-ing `2/3` of a second into seconds **truncates to 0** (an integer `long long` cannot hold 0.666)—precisely the precision-loss issue we take up shortly.

Add `duration`s with different periods, and the result's period automatically lands on the common denominator (the greatest common divisor of the two periods serving as the denominator). `1s + 500ms` never makes you worry about units:

```cpp
auto sum = seconds{1} + milliseconds{500};
// sum's type is milliseconds, count() == 1500
```

This compile-time fractional arithmetic is the fundamental thing separating chrono from the naive "store a `double` count of seconds" scheme: **all unit conversion happens at the type level, integer arithmetic preserves precision, and the compiler computes the common denominator for you**. The price is ugly template error messages; the payoff is type safety.

### Implicit Conversion: Why 500ms Cannot Become 1s

Implicit conversion between durations obeys one hard rule: **only the "lossless" direction is permitted**. Converting a "smaller unit" (higher precision) into a "larger unit" (lower precision) is implicitly allowed only when the tick count divides evenly; when it doesn't (precision would be lost), the compile fails. The reverse direction—"larger unit into smaller" (precision increases, e.g. 1s into 1000ms)—is always allowed.

```cpp
// Standard: C++20
#include <chrono>
using namespace std::chrono;
int main() {
    seconds s = milliseconds{500};   // 500ms -> seconds: not evenly divisible, precision would be lost
    (void)s;
    return 0;
}
```

Compiled with GCC 16.1.1, this line **flat-out fails to compile**:

```text
implicit.cpp:7:17: error: conversion from
  'duration<[...],ratio<[...],1000>>' to 'duration<[...],ratio<[...],1>>'
  requested
```

The reverse, `milliseconds m = seconds{2}` (2s -> 2000ms, higher precision), passes implicitly. The engineering payoff of this rule is enormous: **the compiler blocks for you the sneakiest class of precision bug—"half a second silently vanished"**. If you genuinely want truncation, you must write `duration_cast<seconds>(ms)` explicitly—turning "precision loss" from a silent bug into a deliberate, visible operation.

### duration_cast and ceil / floor / round

`duration_cast` **truncates by default** (rounds toward zero). Converting `1750ms` to `seconds` yields `1s`, and the half second is simply cut off. If your business logic needs a different rounding mode, chrono provides `floor` / `ceil` / `round` (since C++17):

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

    milliseconds ms2{1250};  // 1.25s, exactly half; watch how round handles it
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

`floor` rounds down, `ceil` rounds up, `round` rounds to nearest (ties to even—that is why `1250ms` rounds to `1s` instead of `2s`; this is banker's rounding, avoiding accumulated bias). These three plus default truncation round out four rounding semantics. In timing scenarios: computing "how many complete 16ms units elapsed this frame" calls for `floor`; computing "at minimum how many buffers must I allocate" calls for `ceil`—the difference is real.

There is also one way around integer truncation: make `double` the `Rep`. `duration<double>{1.5}` directly represents 1.5 seconds, `duration<double, milli>` gives floating-point milliseconds, and arithmetic loses no precision (the price being floating point's own precision limits, of course). Scientific computing and elapsed-time distribution statistics reach for this move all the time.

## time_point and the Three Clocks: Why Only steady_clock Can Measure Elapsed Time

`duration` is "a span of time"; `time_point` is "a moment". Its definition is just as simple: **some clock's epoch, plus a duration**.

```cpp
// simplified
template <typename Clock, typename Duration = typename Clock::duration>
class time_point {
    Duration since_epoch_;
};
```

A `time_point` is bound to a `Clock`—`time_point`s from different clocks cannot be directly subtracted (different types, caught at compile time). This design exists precisely to keep "wall-clock moments" and "monotonic moments" from being mixed.

So what is a "clock"? The standard library provides three, and **which one to use** is chrono's most treacherous spot. Let's test their real properties first.

### The Real Properties of the Three Clocks

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

`is_steady` means "monotonically nondecreasing, never steps back". Comparing the three:

- **`system_clock`**: the wall clock, representing the real world's "what time is it right now". Its `is_steady == false`—because the system adjusts it via NTP (Network Time Protocol) or a manual `date` command: to correct clock drift, NTP mostly **slews** (nudges gradually), but when the clock runs far ahead it will **step** (jump), sometimes even backwards. `system_clock`'s epoch is the Unix epoch (1970-01-01 00:00:00 UTC); it interconverts directly with `time_t`, which makes it the right choice for timestamping and reconciling with the outside world.
- **`steady_clock`**: the monotonic clock, `is_steady == true`, **guaranteed never to go backwards**. Its epoch is arbitrary (implementation-defined, usually system boot); the raw value carries no real-world meaning, but **the difference of two readings is always >= 0**. That is exactly the invariant measuring elapsed time requires.
- **`high_resolution_clock`**: the standard says it is "the clock with the smallest tick", but **does not require it to be steady**. In practice it is an alias of one of the other clocks (implementation-defined).

That third clock comes with a trap that older material widely propagates, so let's dig into it on its own.

::: warning high_resolution_clock is an alias—and not of the steady one
Plenty of tutorials and blogs claim "`high_resolution_clock` is an alias of `steady_clock` on libstdc++". That claim **no longer holds on GCC 16.1.1**. Let's test which clock it actually aliases:

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

In GCC 16.1.1's libstdc++, `high_resolution_clock` **is an alias of `system_clock`** (the source line `using high_resolution_clock = system_clock;`), hence its `is_steady == false`. Which means: if you follow older material and measure elapsed time with `high_resolution_clock`, what you are actually using is `system_clock`—a clock NTP can jump—and you will not dodge a single one of the "measured a negative duration" traps below.

The standard itself has, since C++20, **explicitly advised against using `high_resolution_clock`**—it is a historical transitional artifact whose behavior is implementation-defined and inconsistent across platforms. Remember one thing: **use only `system_clock` and `steady_clock`, and treat `high_resolution_clock` as if it did not exist**.
:::

### Hands-On: Why Timing with system_clock Gets You Burned

Claims are cheap, so let's run it. Have the CPU busy-wait 200ms and measure it with both clocks:

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

Under normal conditions both clocks measure about 200ms—no visible difference. But **the real trap hides on the exception path**: if the system clock gets stepped backwards by NTP while `busy` is running (correcting a clock that runs fast, say), `t1 - t0` turns negative, or absurdly small. `steady_clock` uses `CLOCK_MONOTONIC` on Linux (the kernel's monotonic clock source); the kernel guarantees it is monotonically nondecreasing and **cannot step back**, so `t1 - t0` is always >= 0.

This cannot be reliably reproduced inside a userspace program (you cannot casually yank the system clock backwards—that takes root and wrecks the whole machine), but it **genuinely happens** in production: NTP step corrections, clock jumps from container migration, time drift in virtualized environments—all of them distort the delta between two `system_clock` readings. When the logs occasionally show "this code took -340ms", nine times out of ten someone measured elapsed time with `system_clock`.

So the iron law: **measuring elapsed time or intervals—always `steady_clock`**; stamping real-world time and reconciling with external systems—use `system_clock`. Do not mix the two jobs. And if you truly need to turn "a measured duration" into "a real-world moment" (for logging, say), compute the delta with `steady_clock` and record the starting point separately with `system_clock`—divide the labor; do not convert between them. They do not even share an epoch; any conversion would be wrong anyway.

## C++20 Calendars: Dates Become Types

As of C++20, chrono gained a whole set of calendar types, turning "June 22, 2026" from a blob of `tm` structs plus hand-written `strftime` into **type-checked objects constructible at compile time**. This section is the main event of C++20 chrono.

The core types are a set of "calendar fields", each an independent class:

- `year`, `month`, `day`: three independent types for year, month, and day;
- `year_month_day`: the combined date;
- `weekday`: the day of the week;
- `hh_mm_ss`: the time of day (hours-minutes-seconds);
- `month_day`, `year_month`, `month_weekday`, and various other "partial dates".

Paired with the literals (`2026y`, `June`, `22d`), writing dates reads like writing an ordinary expression:

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
using namespace std::chrono;

int main() {
    auto ymd = 2026y / June / 22d;          // year_month_day
    std::cout << "ymd.ok()? " << ymd.ok() << '\n';

    // year_month_day -> sys_days (C++20's "days-granularity time_point")
    auto sys = sys_days{ymd};
    std::cout << "2026-06-22 = " << sys << '\n';

    // which day of the week is it?
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

The expression `2026y / June / 22d` looks like division, but it is operator overloading—`year / month` yields a `year_month`, and `/ day` then yields a `year_month_day`. The whole chained construction is `constexpr`; `ymd` can absolutely serve as a compile-time constant. `sys_days` is an alias of `time_point<system_clock, days>`—"days since the Unix epoch"—which turns a calendar date into a `time_point` that can take part in time arithmetic. That is the hinge where calendar meets clock.

`weekday` has two encodings: `c_encoding()` (Sunday = 0, C style) and `iso_encoding()` (Monday = 1, Sunday = 7, ISO 8601 style). Modern code mostly uses the ISO encoding, because in business contexts "day of week" is usually understood Monday-through-Sunday.

### Built-in Validation of Calendar Legality

Calendar types ship with `.ok()` for validity checking—something hand-written date parsing can never offer. Whether month and day fall in legal range, whether the date actually exists (February 29 is invalid in a common year)—all checked cleanly:

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

`month{0}` (there is no month 0) and `month{13}` (there is no month 13) are both `ok() == false`; February 29 of 2026 (a common year) is invalid, while February 29 of 2024 (a leap year) is valid. **Leap-year checking—never again write `year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)` yourself**—`.ok()` checks it for you.

### last: Expressing "the Last Weekday of the Month"

The most elegant piece of the C++20 calendar is `last`. Plenty of business requirements are relative dates like "the last business day of each month" or "the last Sunday of June"; the traditional route means computing "how many days this month has, and which weekday that day is". chrono turned it into a literal:

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
using namespace std::chrono;

int main() {
    // the last Sunday of June 2026
    auto last_sun_june = year{2026} / June / Sunday[last];
    std::cout << "2026/June/Sunday[last] = " << sys_days{last_sun_june} << '\n';

    // the last day of the month
    auto last_day = year{2026} / February / last;
    std::cout << "2026/February/last = " << sys_days{last_day} << '\n';
    return 0;
}
```

```text
2026/June/Sunday[last] = 2026-06-28
2026/February/last = 2026-02-28
```

`Sunday[last]` is a `month_weekday_last` meaning "the last Sunday of some month". The full expression `year{2026} / June / Sunday[last]` has type `year_month_weekday_last`; when converted to `sys_days`, the standard library works out the concrete date internally—`2026-06-28`, which is indeed the last Sunday of June (the last day of June is Tuesday the 30th; stepping back to Sunday lands on the 28th). `February/last` automatically yields the last day of February in that year (28 in a common year). This API frees date arithmetic from "hand-computing the calendar" once and for all.

### hh_mm_ss: Splitting a duration into Hours, Minutes, and Seconds

For time spans within a day, `hh_mm_ss` splits a duration into its "hours-minutes-seconds" parts, sparing you all that `% 3600` arithmetic:

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

`hh_mm_ss` accepts any `duration` and splits it into hours, minutes, and seconds automatically (with a fractional-seconds part, if the original duration is finer than seconds). It also handles negative durations correctly (with a leading minus sign) and spans beyond 24 hours (`hours()` may exceed 24). For protocol parsing and countdown displays, it is far cleaner than writing the divisions yourself.

## C++20 Time Zones: Local Support, Tested Hands-On

C++20's time-zone support is chrono's last puzzle piece. To implement time zones, the standard library **needs a time-zone database**—on Linux this is usually the system's `/usr/share/zoneinfo/` (provided by the `tzdata` package). This means C++20 time-zone functionality **depends on the runtime environment's time-zone data**; it is not something you can use purely at compile time.

The core types first:

- `time_zone`: one time zone (such as `Asia/Shanghai`), looked up with `locate_zone(name)`;
- `zoned_time`: binds a `sys_time` (a UTC time point) to a time zone, yielding the local-time representation;
- `current_zone()`: returns the machine's current time zone.

Tested on our machine (WSL2 Linux with tzdata installed):

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
using namespace std::chrono;

int main() {
    try {
        auto now = system_clock::now();
        std::cout << "current_zone: " << current_zone()->name() << '\n';

        // the same time point, mapped into different time zones
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

The same UTC time point maps into three time zones, yielding three local times—each automatically carrying its zone abbreviation (`CST`, `EDT`, `UTC`). New York shows `EDT` (Eastern Daylight Time, daylight saving time), meaning chrono even handles DST rules correctly (New York in June is inside daylight saving time). All of this is computed from the system's zoneinfo data.

::: warning The time-zone database must be provided by the runtime environment
`current_zone()` / `locate_zone()` depend on the system time-zone database. In an environment without tzdata (certain minimal containers, bare embedded Linux), these calls **throw `std::runtime_error`** and the program dies outright. GCC 16.1.1's libstdc++ **bundles no time-zone data**; it reads everything from `/usr/share/zoneinfo/`. When packaging for a stripped-down deployment, either make sure `tzdata` gets installed, or make time-zone functionality optional with proper exception fallbacks. Our WSL2 machine has it installed, which is why everything above worked; on a bare container you might instead see `EXCEPTION: Timezone database not available`.
:::

The most practical time-zone scenario is "the service deploys in UTC, but logs/frontend must display the user's local zone". With `zoned_time`, you store all timestamps as UTC (`system_clock` + `sys_time<seconds>`), then at display time convert with `zoned_time{user_tz, utc_tp}`—one type-safe pipeline, no hand-rolled `+8 hours` hardcoded offsets (the moment daylight saving time enters the picture, a hardcoded offset is guaranteed wrong).

## chrono's Formatting: Where It Meets std::format

C++20 brought chrono formatting support, and the machinery is the same one from the previous article's generic `std::format`—`%`-led chrono format specifiers slotted into `std::format`'s `{}` placeholders. The two share the `std::formatter` specialization mechanism underneath: the standard library specializes `formatter` for `sys_time`, `year_month_day`, `duration`, `weekday`, and the other chrono types, so they drop straight into `std::format` with no extension code from you.

The generic `std::format` syntax (`{}` placeholders, compile-time type checking, literal format strings) was covered thoroughly in [the previous article](../strings/52-format.md); here we cover only the `%` specifiers the chrono specializations use. The most common set:

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

    // a time_point with a time of day
    auto tp = sys + 15h + 30min + 7s;
    std::cout << std::format("{:%Y-%m-%d %H:%M:%S}\n", tp);  // 2026-06-22 15:30:07
    std::cout << std::format("{:%F %T}\n", tp);              // %F=%Y-%m-%d, %T=%H:%M:%S
    std::cout << std::format("{:%R}\n", tp);                 // %H:%M -> 15:30

    // 12-hour clock
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

A few high-frequency specifiers to memorize: `%Y` year, `%m` month (zero-padded), `%d` day, `%H` hour (24-hour), `%M` minute, `%S` second, `%A` full weekday name, `%B` full month name, `%p` AM/PM, `%I` hour (12-hour). Two **combination** specifiers are especially common: `%F` is equivalent to `%Y-%m-%d`, and `%T` to `%H:%M:%S`; log timestamps are almost always `{:%F %T}`.

`%c` is the locale-dependent full date-time representation (things like `Mon Jun 22 15:30:07 2026`), while `%x` / `%X` are the locale's date / time representations—useful when doing internationalization, but they depend on locale settings.

::: warning Generic format syntax belongs to the previous article
The `{}` placeholders, positional arguments, compile-time type checking, runtime-built format strings via `vformat`—all that generic machinery was covered in [the format article](../strings/52-format.md). This article covers only the chrono-specific `%` specifiers; do not look here for generic alignment syntax like `{:>10}`—it does apply to chrono types too (the `std::formatter` specializations reuse the generic parsing), but that is generic-format territory.
:::

### The Reverse Direction: parse Turns Strings Back into Time Points

Where there is formatted output, there is parsing in reverse. `std::chrono::parse` (C++20, used with `>>` or the `parse` function) takes the same `%` specifiers and turns strings back into `time_point`s or `duration`s:

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

When `parse` fails, the stream enters a failure state (consistent with `>>`); check it with `if (iss)`. Note that what it parses is a UTC `sys_time`, not local time—to parse local-time timestamps you have to deal with the zone offset yourself (or convert via `local_time` plus a `time_zone`'s `to_sys`). For log replay and protocol parsing, `parse` is far more type-safe than hand-rolled `strptime` + `mktime`.

## C++23: print Consumes chrono Directly, and Time Zone Leak Fixes

C++23 added two strokes to chrono, both small improvements going with the current.

The first: `std::print` / `std::println` (C++23) **consume chrono types directly**, without wrapping a layer of `std::format`. The previous article showed that `std::print` is `std::format` inside, and chrono formatting enjoys the same shortcut here:

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

`println`'s format string is fully identical to `std::format`'s—the `%` specifiers work the same. Note the second line prints a `duration` with a plain `{}` and no `%`: the standard library gives `duration` a default format specialization (printed in its own period's unit, `1500ms`). The mechanics of `print` / `println` themselves (streaming output, skipping the intermediate `std::string`) are detailed in [the print article](../strings/53-print.md); here we only point out chrono's on-ramp.

The second stroke is a batch of C++23 fixes to chrono's time-zone handling (P1654 and friends), mainly plugging edge cases such as "`zoned_time` leaking the time-zone pointer on certain construction paths"—a robustness improvement in the library implementation that leaves everyday usage untouched. If your code keeps `zoned_time` objects alive for long stretches, upgrading to a C++23-capable toolchain spares you some rakes.

::: warning On older GCC, chrono's C++20 pieces may be incomplete
chrono's C++20 portion (calendars, time zones, formatting) landed in GCC step by step: calendars and formatting became mostly usable in GCC 11, but **time zones (`zoned_time` / `current_zone`) were not fully implemented until GCC 14** (requiring `<chrono>` plus a time-zone database). On our GCC 16.1.1, everything works (calendars, time zones, formatting, and `parse` all ran). If your project must support GCC 13 or earlier, time-zone functionality is essentially unusable—you would fall back to the `date` library (Howard Hinnant's `date`, precisely the prototype of chrono's C++20 calendar and time zones). Before deploying across toolchains, confirm the target toolchain's chrono coverage.
:::

## Pitfalls That Genuinely Bite

Let's gather up the crash-prone spots from this whole route, each backed by the hands-on runs above:

::: warning Only steady_clock can measure elapsed time
`system_clock` is a wall clock: NTP can step it (even backwards), so the delta of two readings can come out negative or absurdly small. `steady_clock` uses `CLOCK_MONOTONIC`, which the kernel guarantees to be monotonic. A negative "took -300ms" in production logs is, nine times out of ten, elapsed time measured with `system_clock`. **Measuring durations and intervals → `steady_clock`; stamping real-world time, reconciling → `system_clock`**—two different jobs, do not mix them; they do not even share an epoch, so they cannot be converted into each other.
:::

::: warning high_resolution_clock is not an alias of steady_clock
Older material often claims "`high_resolution_clock` is an alias of `steady_clock` in libstdc++"—that does **not** hold on GCC 16.1.1; it is an alias of `system_clock` (source: `using high_resolution_clock = system_clock;`), with `is_steady == false`. Timing with it is equivalent to timing with the jumpable `system_clock`—every trap above, unescaped. Since C++20 the standard explicitly recommends deprecating `high_resolution_clock`. **Pretend it does not exist; use only `system_clock` and `steady_clock`**.
:::

::: warning Implicit conversion between durations goes only in the lossless direction
`seconds s = milliseconds{500}` does not compile, because turning 500ms into seconds loses precision (500/1000 does not divide evenly). Truncating requires an explicit `duration_cast<seconds>(ms)`. The reverse direction (larger unit to smaller, e.g. `milliseconds m = seconds{2}`) is what is allowed implicitly. This rule is the compiler blocking "silently lost precision" bugs for you—do not resent it.
:::

::: warning duration_cast truncates by default; it does not round
`duration_cast<seconds>(1750ms)` yields `1s` (truncation), not `2s`. For other rounding semantics use `floor` / `ceil` / `round` (`round` is banker's rounding—ties to even). To avoid integer truncation entirely, store floating seconds in `duration<double>`.
:::

::: warning Time-zone functionality depends on the runtime environment's tzdata
`current_zone()` / `locate_zone()` throw `std::runtime_error` in environments without tzdata (minimal containers, bare embedded Linux). libstdc++ bundles no time-zone data; it reads `/usr/share/zoneinfo/` entirely. Before deploying into a stripped-down environment, confirm tzdata is in place, or prepare exception fallbacks.
:::

::: warning system_clock's epoch is 1970-01-01 UTC
`system_clock::time_point`'s epoch is the Unix epoch (1970-01-01 00:00:00 UTC), which is why it interconverts directly with `time_t`, filesystem timestamps, and network protocol timestamps. `steady_clock`'s epoch is implementation-defined (usually system boot), and **the raw value has no real-world meaning**—only deltas count. Never use `steady_clock`'s `time_since_epoch()` as a "real-world moment"; it is only "how long since the system booted".
:::

## Summary

chrono's design compresses into one sentence: **split "time" into duration / time_point / clock, preserve precision with compile-time fractional arithmetic, and block misuse with the type system**. The key conclusions, collected:

- **duration**: tick count `Rep` × period `Period` (a compile-time `ratio`). Literals `h/min/s/ms/us/ns`; non-integer periods like `ratio<2,3>` are represented exactly too (the 2/3-second period of 1.5Hz), with all unit conversion done at compile time. Implicit conversion goes only in the lossless direction; lossy conversions require an explicit `duration_cast` (which truncates by default); `floor` / `ceil` / `round` provide the other rounding semantics.

- **clock**: elapsed time only ever via `steady_clock` (`is_steady == true`, monotonic, backed by `CLOCK_MONOTONIC`, unaffected by NTP); `system_clock` is the wall clock (Unix-epoch based, jumpable by NTP, yields negative timings) for real-world timestamping; `high_resolution_clock` is an alias of `system_clock` (tested on GCC 16.1.1), recommended for deprecation by the standard—treat it as nonexistent.

- **C++20 calendars**: `year/month/day` as independent types plus literals (`2026y/June/22d`), `year_month_day` with `.ok()` validation (leap years checked automatically), `weekday`'s two encodings, `hh_mm_ss` splitting out hours-minutes-seconds, and `Sunday[last]` / `February/last` for "last such-and-such weekday" / "last day" relative dates—all compile-time constructible.

- **C++20 time zones**: `time_zone` / `zoned_time` / `current_zone()` depend on the system's tzdata; tested working on our machine (WSL2 + tzdata), with daylight saving time handled correctly; confirm tzdata is present before deploying into minimal environments. Production practice: store timestamps as UTC (`sys_time`), convert to the local zone with `zoned_time` at display time, and never hardcode a `+8` offset.

- **Formatting**: chrono types specialize `std::formatter`, so they drop straight into `std::format` with `%` specifiers (`%Y-%m-%d %H:%M:%S` / `%F %T`, etc.), sharing the same `{}` machinery as [generic format](../strings/52-format.md); the reverse direction parses with `std::chrono::parse`. C++23's `std::println` consumes chrono types directly, skipping the intermediate `std::string`.

- **epoch**: `system_clock` is 1970-01-01 UTC (interconvertible with `time_t`); `steady_clock` is implementation-defined (usually system boot), its raw value carries no real-world meaning—deltas only.

In the next article we look at another standard-library component that deals with time and the system—`<filesystem>`: how it walks directories, how it abstracts file paths across platforms, and how it brings filesystem operations onto the type-safety track.

## References

- [cppreference: Chrono library](https://en.cppreference.com/w/cpp/chrono) — an overview of the entire chrono family
- [cppreference: std::duration](https://en.cppreference.com/w/cpp/chrono/duration) — duration and `ratio`'s compile-time fractional arithmetic
- [cppreference: std::chrono::steady_clock](https://en.cppreference.com/w/cpp/chrono/steady_clock) — the semantics of `is_steady`, and the right choice for timing
- [cppreference: std::chrono::high_resolution_clock](https://en.cppreference.com/w/cpp/chrono/high_resolution_clock) — why the standard advises abandoning it
- [cppreference: C++20 calendar](https://en.cppreference.com/w/cpp/chrono#Calendar) — the `year_month_day` / `weekday` / `last` calendar types
- [cppreference: std::chrono::zoned_time](https://en.cppreference.com/w/cpp/chrono/zoned_time) — time zones and local time
- [cppreference: chrono formatting](https://en.cppreference.com/w/cpp/chrono/format) — the complete `%` specifier table
- [Howard Hinnant: the date library](https://github.com/HowardHinnant/date) — the prototype of chrono's C++20 calendar and time zones, and the polyfill for older toolchains
