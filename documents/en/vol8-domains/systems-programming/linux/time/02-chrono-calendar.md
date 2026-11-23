---
title: "C++20 std::chrono in Depth: Calendars and Time Zones"
description: "How std::chrono's clocks land on the OS clocks, and what shape leap seconds and time zones take in the type system: runtime cross-checks beyond is_same (system_clock versus clock_gettime(CLOCK_REALTIME) back to back differ by -411 ns, steady_clock versus CLOCK_MONOTONIC by -50 ns — same source), four rulers for one instant (utc−system=+27 s, TAI−UTC=+37 s matching the previous article's kernel-side CLOCK_TAI−CLOCK_REALTIME 37 s, GPS−UTC=+18 s), leap seconds entering the type system (2016-12-31 23:59:60 really exists on the utc_clock axis, get_leap_second_info gives is_leap_second=1 and elapsed=27 on the leap second itself, the tzdb leap-second table holds 27 entries from 1972-07-01 to 2017-01-01), hard time-zone cases (New York's 01:30 on 2026-11-01 occurs twice — choose::earliest gives 05:30Z EDT while latest gives 06:30Z EST; 02:30 on 2026-03-08 does not exist, mapping out to 03:00 never comes back; constructions without choose throw ambiguous_local_time and nonexistent_local_time respectively, with the full exception texts archived; China's 1988-04-17 02:30 did not exist that year, July CDT +0900 versus January CST +0800), tzdb landing on the OS (strace proving openat hits exactly /usr/share/zoneinfo/tzdata.zi and leapseconds; first get_tzdb 1.751 ms, second 50 ns returning the same reference), and a closing one-line log with four columns (steady for measurement, epoch for persistence, utc for auditing, zoned for presentation)"
chapter: 8
order: 2
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 27
prerequisites:
  - "时钟源与 POSIX 时间 API:机器里的九把钟"
  - "chrono：duration、时钟与 C++20 日历"
related:
  - "时钟源与 POSIX 时间 API:机器里的九把钟"
  - "定时器全景:alarm→setitimer→timer_create→timerfd"
  - "chrono：duration、时钟与 C++20 日历"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/time/02-chrono-calendar.md
  source_hash: fc604b97e29973df6e39ed8870b48829f9a43b0907e844404c7f40922d155432
  translated_at: '2026-11-22T15:36:15+00:00'
  engine: glm-5.3
  token_count: 15798
---

# C++20 std::chrono in Depth: Calendars and Time Zones

In the [previous article](./01-clock-sources.md) we felt out the kernel side of the clocks: we measured the temperament of all nine clockids, saw the 37 seconds sitting between CLOCK_TAI and CLOCK_REALTIME, and walked the vDSO ticket-free lane once. But when you actually sit down to write an application, what your fingers type is usually not `clock_gettime` but `std::chrono`. So the question follows: what exactly is the relationship between the standard library's two regulars, `system_clock` and `steady_clock`, and the `CLOCK_*` set from the last article? Did the standard library build its own clocks from scratch, or do they simply sit on top of the kernel's?

Let us push two questions deeper. Take leap seconds: last time we only saw one inside the difference between two clocks, but once it actually lands on the timeline it is a genuinely extra second in a day. Look at the night of December 31, 2016 — that night's 23:59:60 really does carry 60 in the seconds field. Can the type system hold such an instant? And time zones: China observed daylight saving time from 1986 to 1991, and 2:30 in the early morning of April 17, 1988 simply did not exist in Shanghai that year. Can C++ take care of things like that?

The boundary is drawn up front. The standard-library volume's [chrono article](../../../../vol3-standard-library/time-numeric/58-chrono.md) has already finished the library's main course — duration's compile-time fractional arithmetic, why `steady_clock` monopolizes elapsed-time measurement, calendar literals like `2026y/June/Sunday[last]`, and the format and parse specifiers were all covered there and will not be retaught here. This is the systems-programming volume, and it asks a different kind of question. E1 asks how chrono's clocks land on the OS, E2 looks at the shape leap seconds take in the type system, E3 settles who guards ambiguous and nonexistent local times, and E4 traces where the time-zone database comes from and what loading it costs. E5 runs calendar types through one round of reconciliation against the timeline, and in the closing E6 we put four readings of the same instant into one log line.

The experiments are numbered E1 through E6, corresponding to the six file groups t1 through t6 under the repository archive `code/volumn_codes/vol8/systems-programming/linux/time/02-chrono-calendar/`; the code and all raw output are archived. Each E in this article belongs to this article alone: the E-series of the previous clock-sources article and the E-series of the next timer article talk about different things even when the numbers match, so when you browse the archive, trust the directory. Output blocks in the body are mostly excerpts — skipped sections and section banners are dropped, and deleted lines within a block are marked `...` — so when reconciling against the archive, the archive is authoritative.

The environment baseline, as usual, stated up front so every later number can be checked against it: the experiments ran on the author's WSL2 with the 6.18.33.2-microsoft-standard-WSL2 kernel build, an AMD Ryzen 7 9700X CPU, g++ 16.2.1, compiled uniformly with `-std=c++20 -O2 -Wall -Wextra` (t6 adds `-pthread`), glibc 2.44, and zero warnings across all experiments. The time-zone database is the distribution tzdata 2026c, holding 341 zones, 257 links, and a 27-entry leap-second table; the machine's `/etc/localtime` points to Asia/Shanghai. All `.out` files come from the same run on 2026-10-04. Any reading tied to the current instant, and any timing figure (nanosecond-scale differences, millisecond-scale load times), will change on every rerun; what we cite are counts, orders of magnitude, and behavior. One environmental limitation is recorded honestly: the only human-language locale installed is en_US.utf8, so chrono's localized formatting can only be shown in English here — Chinese month names will not come out on this machine. That is an environment constraint, not a library defect.

## E1: Where the Clocks Land: chrono's Clocks Cross-Checked Against the Kernel's

### is_same only covers the type half

Which clock `high_resolution_clock` aliases was already adjudicated in the vol3 chrono article, and this machine's g++ 16.2.1 still returns the same verdict:

```text
is_same<high_resolution_clock, system_clock> : 是
is_same<high_resolution_clock, steady_clock> : 否
system_clock::is_steady=0  steady_clock::is_steady=1
```

The alias speaks to identity at the type level, and `is_same` answers that at compile time. What we really want to know is a runtime matter: which kernel clock does chrono's `now()` land on? The standard only prescribes semantics — `system_clock`'s epoch must be the Unix epoch, `steady_clock` must be monotonic — while which clock is actually read underneath is the implementation's freedom. How libstdc++ lands on Linux is something we settle with experiments, not guesses.

### Read back to back; the difference is the read latency

The cross-check method is plain: at one instant we read twice back to back, once through chrono and once through `clock_gettime`. If the difference falls in the order of read latency, they are reading the same clock:

```cpp
auto a = ch::system_clock::now().time_since_epoch().count(); // 纳秒
timespec ts{};
clock_gettime(CLOCK_REALTIME, &ts);
auto b = ch::system_clock::duration(
    ch::seconds(ts.tv_sec) + ch::nanoseconds(ts.tv_nsec)).count();
std::printf("system_clock − CLOCK_REALTIME = %lld ns → %s\n", (long long)(a - b),
            std::llabs(a - b) < 100000 ? "同源(都读墙上钟)" : "不同源?");
```

Here is what t1 printed:

```text
system_clock − CLOCK_REALTIME = -411 ns → 同源(都读墙上钟)
steady_clock  − CLOCK_MONOTONIC  = -50 ns → 同源(都读单调钟)
```

The two readings are separated by one function call, and -411 ns and -50 ns are exactly the cost of that little work; the criterion is 100 microseconds — anything much beyond that and same-source is off the table. Nanosecond-scale differences shift from run to run: this round we saw 411 and 50, a rerun might show three-hundred-something or twenty-something, and only the order of magnitude holds. So the answer can come down: libstdc++'s `system_clock` reads `CLOCK_REALTIME`, `steady_clock` reads `CLOCK_MONOTONIC`, and the standard library builds no clock of its own — it sits on top of the kernel's two. The previous article's E4 measured a vDSO `clock_gettime` at 16.9 ns per call; since the very same call is being made here, the ticket-free lane applies to `chrono::now()` unchanged, and you do not pay for an extra layer of system calls on the standard library's behalf.

### One instant, four rulers

With C++20, chrono's household gained three new rulers: `utc_clock`, `tai_clock`, and `gps_clock`. To convert among them we must know where each ruler's zero mark is drawn: `system_clock` and `utc_clock` both use the epoch 1970-01-01 00:00:00 UTC, tai (International Atomic Time) draws its zero at 1958-01-01, and gps (Global Positioning System) at 1980-01-06 — all fixed by the standard. The four rulers measure the same stream of time from different zero points, so the physical offset we want is what remains after the zero-point difference is subtracted:

```cpp
auto u   = ch::system_clock::now();
auto utc = ch::clock_cast<ch::utc_clock>(u);
auto tai = ch::clock_cast<ch::tai_clock>(u);
auto gps = ch::clock_cast<ch::gps_clock>(u);
```

Here is the E1c section of t1's output:

```text
system_clock 纪元 1970-01-01 UTC:  1791121304.997 s(UTC 刻度,不数闰秒)
utc_clock    纪元 1970-01-01 UTC:  1791121331.997 s(UTC 刻度,1972 以来 27 次闰秒都数进去)
tai_clock    纪元 1958-01-01 TAI:  2169812541.997 s
gps_clock    纪元 1980-01-06 GPS:  1475156522.997 s

把纪元差扣掉之后,剩下的就是物理偏移:
utc − system                     = +27 s → 1972 以来插入的闰秒数
tai − system − 378,691,200(纪元差)= +37 s → TAI−UTC 当下值
gps − system + 315,964,800(纪元差) = +18 s → GPS−UTC 当下值(GPS 领先)
(TAI−UTC=37 对表篇 1 E1 的 CLOCK_TAI−CLOCK_REALTIME=37 s:内核与库读同一份闰秒数据)
```

All four readings carry .997 in the fraction — they really are four readings of one instant. The remaining differences sit entirely in whole seconds, and we take them ruler by ruler. `system_clock`'s axis is scaled in POSIX ticks, where a day is fixed at 86400 seconds and leap seconds are not counted; that is why it stands 27 seconds away from `utc_clock`: the utc axis counts in all 27 leap seconds inserted since 1972, so the same physical instant reads 27 larger there. Neither the TAI nor the GPS ruler counts leap seconds, and the two brothers differ by a constant 19 seconds: on the day GPS started work in 1980, TAI−UTC happened to be exactly 19 seconds; UTC later absorbed 18 more leap seconds, so TAI−UTC rose to 37 while GPS−UTC stayed at 18. Subtract 19 from 37 and you get exactly 18.

A hidden thread connects the clock-sources article to this one, and we name it: the previous article's E1 printed `TAI - REALTIME = 37000000020 ns` — the kernel-side reading, where the 37 seconds are kneaded into the difference of two clockids. This article's E1 conversion yields the same 37 — the library-side reading, where the leap seconds are baked into `clock_cast` between types. The two readings consume the same data, and where that data actually lives and how it gets read in is exactly what E4's strace will show us.

### steady_system_clock: no proposal number on record, not shipped here

The standard actually contains no `steady_system_clock`; what C++20 added is exactly the three — utc, tai, gps — and on this machine's g++ 16.2.1 the name does not compile, nor does a matching feature-test macro exist; both facts were verified by us. As for the composite semantics of a clock that is simultaneously monotonic and wall-mapped, no proposal number is on record today, and there is no hurry discussing it before it actually lands. This article's focus stays on the three that have landed.

## E2: The Shape of Leap Seconds in the Type System

### 23:59:60 is really on the axis

The POSIX axis has no room for leap seconds: the second after 2016-12-31 23:59:59 jumps straight to 2017-01-01 00:00:00, so the extra second of that night has no slot on this ruler and can only live in annotations. The `utc_clock` axis counts leap seconds in, so we let the addition happen on it:

```cpp
auto t235959 = clock_cast<utc_clock>(
    sys_days{2016y / December / 31} + 23h + 59min + 59s);
auto leap  = t235959 + 1s;  // utc_clock 的加法:这一秒是闰秒
auto after = leap + 1s;     // 越过闰秒
```

From t2's output, the four utc lines:

```text
...
utc_clock: 2016-12-31 23:59:59
utc_clock +1s = 2016-12-31 23:59:60   ← 23:59:60 真的在轴上
utc_clock +2s = 2017-01-01 00:00:00
闰秒所在秒的亚秒位: 2016-12-31 23:59:60 60
```

The seconds field reaches 60 and the formatter recognizes it — `%S` yields 60 on a leap second. The same `+1s` performed on the sys axis would jump the reading straight to 2017-01-01 00:00:00; the difference between the two rulers shows up plainly in this one addition. Anyone who has built systems that reconcile against time-synchronization sources will feel the weight immediately: 23:59:60 is no longer a string to special-case with an `if` — it is a proper tick on the axis.

### get_leap_second_info: is this second a leap second?

Formatting alone is not enough; we also need to ask: is a given instant a leap second, and how many have been counted since the epoch? That is precisely what C++20's `get_leap_second_info` does, and this machine's g++ 16.2.1 ships it. t2 asked at four instants:

```cpp
auto a = get_leap_second_info(t235959); // 闰秒前一秒
auto b = get_leap_second_info(leap);    // 闰秒本体
auto c = get_leap_second_info(clock_cast<utc_clock>(sys_days{2017y / January / 2}));
auto d = get_leap_second_info(clock_cast<utc_clock>(sys_days{2015y / January / 2}));
```

```text
2016-12-31 23:59:59 → is_leap_second=0, 已经历闰秒=26
2016-12-31 23:59:60 → is_leap_second=1, 已经历闰秒=27(含当次)
2017-01-02          → is_leap_second=0, 已经历闰秒=27
2015-01-02          → is_leap_second=0, 已经历闰秒=25
```

The reading is straightforward: the column labeled as leap seconds experienced prints `elapsed`, telling us how many leap seconds the axis has counted so far. On the leap second itself `is_leap_second=1` and the count including the current one is 27, while the second before is 26; after crossing that pre-dawn moment of 2017 it settles at 27, and at the start of 2015 it was 25. Set these numbers against E1's `utc−system=+27` and you see both are saying the same thing — one read out of a conversion difference, the other out of a per-instant query.

### 27 entries in the table; 10 plus 27 makes 37

We dug out the leap-second records themselves from the tzdb (chrono's in-process time-zone database, fetched via `get_tzdb()`):

```text
tzdb 版本 2026c,leap_seconds 条数=27
首条 1972-07-01,末条 2017-01-01
...
```

The table's first entry falls in July 1972, the year the leap-second mechanism opened, and the last falls in January 2017 — the insertion at the end of 2016. None has been inserted since, and a negative leap second has never happened. One spot easily fails to reconcile, so let us settle it: TAI−UTC is currently 37 seconds while the table holds only 27 entries; the missing 10 seconds are the initial offset that already existed when the mechanism started in 1972. 10 plus 27 sums to 37, and the kernel's `CLOCK_TAI` running 37 seconds fast, the library's `tai−utc` 37 seconds, and the table's 27 entries plus the initial offset all agree.

So when should you genuinely use `utc_clock`? Our engineering judgment: wherever the timeline must survive interrogation by leap seconds — audit logs, reconciliation against time-synchronization systems — use it. For epoch values that ordinary business logic persists, `system_clock` is enough; E6 will gather this division of labor into one place.

## E3: Ambiguity and Nonexistence: Hard Cases of Constructing Backward from local_time

### Constructing backward from a wall time is where it gets real

Construction in the `zoned_time(zone, sys_time)` direction never goes wrong: you supply an absolute instant, and putting a zone on it merely changes the presentation — that is what vol3 demonstrated. The hard direction is the reverse: when you read a local-style time out of a log or user input and construct a `zoned_time` from it, you are forcing the type system to answer — which absolute instant does this local time denote? On most days the sentence is unambiguous, but on the nights when daylight saving time shifts the clock forward or back, there is more than one answer, and sometimes not even one.

The New York zone is a textbook regular, so we open with it and save China's own example for the finale.

### The fall-back night: the same 01:30 comes twice

Consider the fall-back night. On November 1, 2026, New York dials EDT back to EST — 2 a.m. is turned back to 1 a.m. — so the local 01:30 gets lived through twice:

```cpp
local_seconds ambiguous = local_days{2026y / November / 1} + 1h + 30min;
zoned_time earliest = zoned_time("America/New_York", ambiguous, choose::earliest);
zoned_time latest   = zoned_time("America/New_York", ambiguous, choose::latest);
```

Here is t3's output:

```text
01:30 choose::earliest(第 1 次,EDT): 2026-11-01 01:30:00 EDT -0400  (对应 UTC 2026-11-01 05:30:00)
01:30 choose::latest(第 2 次,EST):   2026-11-01 01:30:00 EST -0500  (对应 UTC 2026-11-01 06:30:00)
...
```

The same local time yields two UTC answers an hour apart. `choose::earliest` takes the occurrence before the dial-back, still daylight-saving EDT, corresponding to UTC minus four; `choose::latest` takes the one after, now EST, corresponding to UTC minus five. A direct construction without `choose` will not flip a coin for you:

```text
不带 choose 的构造直接抛: 2026-11-01 01:30:00 is ambiguous.  It could be
2026-11-01 01:30:00 EDT == 2026-11-01 05:30:00 UTC or
2026-11-01 01:30:00 EST == 2026-11-01 06:30:00 UTC
```

The `ambiguous_local_time` exception text is considerate: it prints both candidates along with their UTC equivalents, fit to show a user without rewording. For taking sides, the silent route spells out `choose` explicitly and the loud route lets the exception fly — we accept both. The third kind, the one that never thought about the problem, is beyond anyone's rescue.

### The spring-forward night: 02:30 never existed

Now the spring-forward night. On March 8, 2026, New York dials EST up to EDT — 2 a.m. jumps straight to 3 a.m. — and the local 02:30 matches nothing at all:

```text
02:30 映射出去(不存在的时刻): 2026-03-08 03:00:00 EDT -0400  (对应 UTC 2026-03-08 07:00:00)
回看: 该 UTC 时刻的本地呈现是 2026-03-08 03:00:00 EDT → 原 02:30 回不来
不带 choose 的构造直接抛: 2026-03-08 02:30:00 is in a gap between
2026-03-08 02:00:00 EST and
2026-03-08 03:00:00 EDT which are both equivalent to
2026-03-08 07:00:00 UTC
```

With `choose::earliest`, this nonexistent time maps to 03:00, the first valid instant after the gap. The verification is to look again on the return trip: converting that UTC instant back to local yields 03:00, and the original 02:30 never comes back — that is the operational meaning of nonexistence. Construction without `choose` throws `nonexistent_local_time`, and the exception text spells out both boundary instants of the gap. Ambiguity and nonexistence each get their own exception type and text, so you can triage them right in the catch.

### China also observed daylight saving time

We do not need to borrow New York's material to discuss ambiguity and nonexistence. China observed daylight saving time from 1986 to 1991, and that history sits in Asia/Shanghai's tzdata; t3 dug out the 1988 records:

```cpp
local_seconds cn_ghost = local_days{1988y / April / 17} + 2h + 30min;
zoned_time cn = zoned_time("Asia/Shanghai", cn_ghost, choose::earliest);
```

```text
1988-04-17 02:30(拨快,不存在):    1988-04-17 03:00:00 CDT +0900  (对应 UTC 1988-04-16 18:00:00)
1988-04-17 03:30(存在,CDT +09):      1988-04-17 03:30:00 CDT +0900  (对应 UTC 1988-04-16 18:30:00)
1988-07-01 12:00(夏令时中段):     1988-07-01 12:00:00 CDT +0900  (对应 UTC 1988-07-01 03:00:00)
1988-01-15 12:00(冬令时):           1988-01-15 12:00:00 CST +0800  (对应 UTC 1988-01-15 04:00:00)
```

In the early hours of April 17, 1988, the clock jumped from two to three, so that day's 02:30 did not exist in China — behavior identical to New York's gap. Noon in July was CDT plus nine; noon in January was CST plus eight. Both abbreviations deserve a second look: CST here is China Standard Time, which collides with the abbreviation for US Central Standard Time, and CDT means China Daylight Time, likewise colliding with US Central Daylight Time — do not walk into the wrong door when reading logs. During those six years, summer China ran one hour ahead of now, and this history is more than trivia: any data dealing with Chinese local times from the late 1980s and early 1990s must send its shift boundaries through E3's checks.

## E4: Where tzdb Comes From and What It Costs

### First call 1.751 ms, second 50 ns

The previous two sections kept consulting tzdb, so we should measure what shape this time-zone database takes inside a process. t4 timed `get_tzdb` twice:

```text
首次 get_tzdb(): 1.751 ms(解析时区数据库,之后常驻)
再次 get_tzdb(): 0.000050 ms(同一份引用,指针是 同一个)
...
locate_zone("Asia/Shanghai"): 0.922 us → Asia/Shanghai
current_zone(): Asia/Shanghai(读 TZ 环境变量,未设则 /etc/localtime,机制同篇 1 E6)
locate_zone("Mars/Olympus_Mons"): 抛 std::chrono::tzdb: cannot locate zone: Mars/Olympus_Mons
```

The first call's 1.751 ms is the price of parsing the entire time-zone database; once parsed it stays resident, every later `get_tzdb` costs 50 ns and hands back the same reference — we verified the pointers, and it is indeed the same object. A single `locate_zone` runs under one microsecond. So inside our process, time-zone lookup costs a process-wide singleton plus one parse; use it freely on hot paths without worrying about repeated construction. The route `current_zone` takes to find the machine's zone follows the same environmental conventions as `localtime_r` finding `/etc/localtime` in the previous article's E6 — there it is libc that reads them, here it is libstdc++. The fate of a bad zone name is also on record: the exception text is pasted verbatim above, and the Mars time zone is not shipping just yet.

### strace shows which two OS files it reads

Where does its data actually come from? One strace gives the answer:

```text
$ strace -e trace=openat ./t4_tzdb_load 2>&1 | grep zoneinfo
openat(AT_FDCWD, "/usr/share/zoneinfo/tzdata.zi", O_RDONLY) = 3
openat(AT_FDCWD, "/usr/share/zoneinfo/leapseconds", O_RDONLY) = 4
```

The whole load performs exactly two `openat` calls: the rules and the leap-second table live in `tzdata.zi` and `leapseconds` respectively. Where E2's leap-second table came from, and the conversion data behind E1's four rulers — the sources are all accounted for here. libstdc++ ships no time-zone data of its own; it parses files from the distribution's tzdata package. The engineering implication weighs more than it looks: two machines with different tzdata versions can convert the same historical instant differently, and if some future revision changes the ruling on China's 1988 daylight saving, the absolute instants the two machines compute will fork. The vol3 chrono article warned that a slimmed container without tzdata throws; this article adds the affirmative evidence: container images should install tzdata, and the versions across your deployment matrix are worth reconciling — ours is 2026c with 341 zones.

## E5: Calendar Types Reconciled Against the Timeline

The main course on calendar types is in vol3 — how to write the literals and combine the types was covered in full there. Here we run just one round of reconciliation with the main thread: when a calendar value enters the timeline, who does the gatekeeping before entry? Straight to the first block of t5:

```text
2026-02-30 .ok()=0  2025-02-29 .ok()=0  2028-02-29 .ok()=1  2026-03-32 .ok()=0
2026-02-30 转 sys_days = 2026-03-02(折算: 2026-02-01 + 29 天)
不合法的值还能存(ok() 只是标记),转 sys_days 不报错也不跳变,年月也无效时结果未指明,
合法性要在转入时间轴之前自己把关
```

`ok()` is exactly the gate we want. Values like February 30, February 29 in a common year, or the 32nd of a month can all be constructed, and they store just fine — `ok()` merely flags them. Converting to `sys_days` neither blocks for you nor misbehaves: for values whose year and month are valid but whose day is out of range, the standard prescribes the folding formula of the first of the month plus day minus one, so 2026-02-30 lands on 2026-03-02 — no error, no jump — while a value whose year and month are also invalid leaves the result unspecified. So validity does not stop you at construction; it is something you self-check before entering the timeline. This is the same design flavor as E3's `local_time`: the type system puts "might be wrong" out in the open and leaves when to check to you.

Values that passed the gate become our reconciliation material once on the axis:

```text
2026 年 10 月的最后一个周日 = 2026-10-25(Sunday)
2026 年 10 月的第 1 个周一  = 2026-10-05
2026-10-04 的 weekday = Sunday(与 date 命令输出对表)
sys_days ↔ year_month_day 往返: 2026-10-04
2000-01-01 → 2026-10-04 共 9773 天(类型是 days,不是自己除 86400)
2026-10-04 → 年底之间的周日数 = 13(日历运算直接落在循环里)
```

The day the experiment was written was October 4, 2026 — a Sunday, as it happened — and `weekday`'s output agrees with the `date` command. Subtracting two dates yields the type `days`; there is not a single hand-written division behind the number 9773. While at it, we also counted how many Sundays remain until year's end, with the calendar types themselves inside the loop:

```cpp
int sundays = 0;
for (sys_days d = today; d <= sys_days{2026y / December / 31d}; d += days{1})
    if (weekday{d} == Sunday) ++sundays;   // 数出来是 13
```

Finally, `hh_mm_ss`:

```text
9025ms+234us → 00:00:09.025234(to_dur 回去还是同一个 duration)
负 duration -5min → is_negative=1,钟面 00:05:00(符号单列,域不掺假)
```

Take a negative duration: `is_negative` sets the sign apart in its own field, the three hour-minute-second fields stay untainted, and `to_dur` turns the unfolded clock face back into the very same duration — that is where it beats pasting a minus sign onto the front of a string.

## E6: One Log Line, Four Readings

With all four questions asked, we close by putting four readings into one log line. t6 simulates an event stream from connect to close; five events each print four columns, and each record stores just two time points:

```cpp
struct LogLine {
    ch::nanoseconds since_start;        // steady 计的相对时长
    ch::system_clock::time_point wall;  // 墙上的绝对时刻
    const char* event;
};
```

At print time, one record yields four readings:

```cpp
ch::nanoseconds elapsed = ch::steady_clock::now() - t0;          // 测量
auto wall    = ch::system_clock::now();                          // 持久化
auto utc_tp  = clock_cast<ch::utc_clock>(wall);                  // 审计
zoned_seconds local =
    zoned_seconds("Asia/Shanghai", floor<seconds>(wall));        // 呈现
```

The output, excerpted:

```text
elapsed    epoch(ns)                UTC(闰秒安全)              本地(Asia/Shanghai)
00:00:00.050596687 1791121305060672272      2026-10-04 13:41:45.060672272  2026-10-04 21:41:45 CST
00:00:00.103023497 1791121305113094954      2026-10-04 13:41:45.113094954  2026-10-04 21:41:45 CST
00:00:00.153700993 1791121305163772560      2026-10-04 13:41:45.163772560  2026-10-04 21:41:45 CST
...
```

The division of labor across the four columns, gathered one by one. The elapsed column comes from `steady_clock` and computes how long since work began; its monotonicity is underwritten by the previous article's E3 — two million consecutive reads, zero regressions — so measurement is its job. The epoch column comes from `system_clock` and stores a nanosecond integer — convertible, persistable, and reconcilable across machines; use it when writing to a database. The UTC column comes from `utc_clock`; its timeline counts leap seconds, it reads well for humans while surviving audits, and on the night 23:59:60 actually happens, this column will not swallow that second. The local column comes from `zoned_time`, its rules sourced from tzdb data and guarded by the full battery of E3 checks — it is for the person on duty to read.

The four columns are four readings of one instant; the 0.050, 0.103, 0.153 intervals line up neatly, every conversion walks through the type system, and no hand-written division is anywhere. When you design logs or event streams later, this line is your template: four jobs, four sources — do not make one clock work four shifts.

This article was about reading time. The next one switches to acting on time: the timer family formed by alarm, setitimer, timer_create, and timerfd — the semantics and survival of the four forms, plus the drift comparison between relative sleeping and absolute anchoring in periodic scheduling. We continue over there.
