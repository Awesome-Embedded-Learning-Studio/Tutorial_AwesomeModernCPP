---
chapter: 7
cpp_standard:
- 11
- 17
description: A thorough walkthrough of the std::regex trio, iterator-based tokenization,
  and capture groups, plus real benchmarks exposing its order-of-magnitude slowdown
  against string::find (worse still when constructed in a loop), ending with guidance
  on when to use which tool and when to switch to a third-party library
difficulty: intermediate
order: 54
platform: host
prerequisites:
- 'Deep Dive into string: SSO, COW, and resize_and_overwrite'
- 'Algorithm Overview (Part 1): Non-Modifying, Modifying, and Searching — How to Pick the Right Algorithm for a Problem'
reading_time_minutes: 16
related:
- 'Container Selection Guide: Picking the Right One by Operations, Memory, and Invalidation Rules'
- 'Iterator Adapters: Reverse, Insertion, and Stream — Teaching Old Iterators New
  Tricks'
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'regex: The Heaviest Text Tool in the Standard Library and Its Cost'
translation:
  source: documents/vol3-standard-library/strings/54-regex.md
  source_hash: 631bf7c89e1bfa6b03fc114f11b7120c7cf2528400d3c5034a348ac5f16bc6a0
  translated_at: '2026-09-26T00:03:56+00:00'
  engine: anthropic
  token_count: 16000
---
# regex: The Heaviest Text Tool in the Standard Library and Its Cost

We've now worked through the three big blocks of containers, iterators, and algorithms, leaning all along on "read the name and you know how to use it" interfaces like `string::find` and `find_first_of`. This article changes the mood completely—we're taking on the heaviest text tool in the standard library: `<regex>`.

Why does it deserve an article of its own? Very practical reason: `std::regex` is one of the rare standard library components **powerful enough to write production-grade patterns directly, yet slow enough to drag down an entire hot path**. It supports capture groups, backreferences, named groups, zero-width assertions, four syntax flavors, case-insensitive matching... basically everything you reach for in day-to-day regex work. The price is that it runs more than an order of magnitude slower than a hand-written string search. Plenty of newcomers don't know this, and only find out when their CPU gets hammered after going live. So this article doesn't just cover how to use it—more importantly, it lays out with real data where the "weight" and the "slowness" actually sit, so you know when to reach for it and when to steer around it.

We'll start with the basic trio and get the common usage flowing, then devote a whole section to a performance comparison—that's the core value of this article.

## The Trio: match / search / replace

The three top-level functions of `<regex>` map exactly onto the three most common text-processing needs:

- `std::regex_match` — the **entire string** must match the pattern (the whole string, from head to tail, has to line up);
- `std::regex_search` — **searches** the string for any matching substring (returns once found; no whole-string requirement);
- `std::regex_replace` — replaces all (or some) matches with other content.

The three names look alike, but their semantics differ a lot, and `match` versus `search` is exactly where beginners faceplant. `match` demands a **whole-string** match—even one character off and it fails; `search` wins as long as some stretch of the string fits. Let's run that distinction first:

```cpp
// Standard: C++17
#include <iostream>
#include <regex>
#include <string>

int main()
{
    std::string email = "charlie@example.com";
    std::regex email_re(R"(^\w+@\w+\.\w+$)");

    // regex_match: the entire string must fully match
    std::cout << "regex_match 邮箱: "
              << std::boolalpha << std::regex_match(email, email_re) << '\n';
    // whole string fits -> true

    // same pattern, but a string with "other text around it": match goes straight to false
    std::cout << "regex_match 带垃圾: "
              << std::regex_match(std::string("联系 charlie@example.com 谢谢"), email_re) << '\n';

    // regex_search: searches for a substring, no whole-string requirement
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

Run with `g++ -std=c++17 -O2` (local GCC 16.1.1):

```text
regex_match 邮箱: true
regex_match 带垃圾: false
search 找到第一段数字: 12345 (位置 8)
```

Two details to note. First, we wrote the pattern as `R"(...)"`—a C++11 **raw string literal**. Inside it, backslashes don't get swallowed by the C++ compiler first. Regex is carpeted with backslashes—`\d`, `\w`, `\.`—and without a raw string you'd be writing eye-glazing double-backslash forms like `"\\d+"`; `R"()"` is the standard idiom for regex work. Second, `regex_search` returns only the **first** match. To collect everything after it you need an iterator, which is exactly the next section's topic.

### smatch: How to Get Capture Groups

The `std::smatch` used above (an alias for `match_results<std::string::const_iterator>`) is not just a boolean result—it stores the entire match plus all **sub-matches**. Each pair of parentheses in the pattern is one capture group: `m[0]` is the whole match, and `m[1]`, `m[2]`, ... are the parenthesized groups in order. Let's demonstrate with a timestamped log line:

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

Parentheses are numbered left to right: `(\d{4})` is group 1, the year, and so on down the line. `ts[0]` is always "the entire matched text". Capture groups are where regex genuinely earns its keep in engineering—extracting structured fields, parsing protocol headers, rewriting templates, all of it rides on them.

### regex_replace: Replacing the Matches

The third function handles rewriting. By default it replaces **all** matches; pass the `format_first_only` flag to replace only the first one:

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

One trap to flag up front: **never write a bare `$` in the `regex_replace` replacement string**. In ECMAScript syntax, `$1`, `$&`, and friends are backreferences (`$1` stands for capture group 1, `$&` for the entire match). If all you want is to swap digits for a literal `$`, be careful it doesn't get read as a special symbol. For simple cases, replacing with a plain string as above does the job.

## Iterators: Iterate All Matches + Tokenize

The trio covers most read-modify-write needs, but two jobs are beyond it: walking **all** matches in a string (`regex_search` hands you only the first) and **tokenizing** by pattern. The standard library ships two iterators for exactly these jobs.

`std::regex_iterator` turns "each `++` yields the next match" into an iterator, so walking all matches becomes a one-line range `for`:

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

Note the default-constructed `end`—it's the "sentinel" meaning "matches exhausted". You saw this move in the previous article on stream iterators (`istream_iterator`'s EOF sentinel); same routine: you don't need to know in advance how many matches the string holds, the iterator stops itself at the end. Dereferencing `*it` yields an `smatch`, so `(*it)[1]` grabs a capture group directly.

The other one, `std::regex_token_iterator`, does tokenization full-time. Its key argument is the last one: pass `-1` to ask for what lies **between** matches (treat the matches as delimiters and collect the remaining fields); pass `0` or a non-negative number to ask for the match itself or the Nth capture group:

```cpp
// Standard: C++17
std::string csv = "alpha,beta,,gamma,delta";   // deliberately leave one empty field
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

Five tokens, and the empty field between the consecutive commas is preserved verbatim as `[2] ''`. This point matters—unlike many hand-written `split`s, `regex_token_iterator` does not merge consecutive delimiters; the empty string between them still counts as a segment, and the `[2] ''` above is the evidence.

::: warning Don't hand-roll tokenization into a manual loop
Faced with "split this string by a delimiter", many people's first reflex is to hand-write a find-the-delimiter-and-slice loop. First ask whether the behavior you want is to **keep empty fields**—a hand-rolled loop easily treats consecutive delimiters as one and silently drops fields. `regex_token_iterator` has crisp semantics (empty fields kept) and predictable behavior. Of course, if your requirement is "I actually want to drop empty fields", that's another matter—but then it's a **conscious decision**, not a bug.
:::

## The Default Syntax: ECMAScript, Much Like What You Write in JS/Python

By default `std::regex` uses **ECMAScript** syntax—yes, the JavaScript one. That means the `\d`, `\w`, `\s`, `{n,m}`, `(?:...)`, `(?=...)` you're used to writing in JS carry over essentially unchanged. A roundup of the metacharacters and character classes you'll actually use:

| Syntax | Meaning |
|---|---|
| `.` | Any single character (newline excluded by default) |
| `\d` `\D` | Digit / non-digit |
| `\w` `\W` | Word character (alphanumeric plus underscore) / non-word |
| `\s` `\S` | Whitespace / non-whitespace |
| `*` `+` `?` | 0+ / 1+ / 0 or 1 |
| `{n}` `{n,m}` | Exactly n times / n to m times |
| `[abc]` `[^abc]` | Character set / negated set |
| `^` `$` | Start of line / end of line |
| `(...)` `(?:...)` | Capture group / non-capturing group |
| `(?=...)` `(?!...)` | Lookahead (positive/negative) |
| `\1` | Backreference to group 1 |

Constructing a `std::regex` can take syntax flags from `std::regex_constants` to switch to another grammar (`extended`, `grep`, `awk`, and other POSIX family members), or layer on `icase` for case-insensitive matching. Here's a genuinely easy trap: **character-class support differs across grammars**. `\d` is an ECMAScript shorthand; move to the `extended` (POSIX) grammar and it's no longer recognized—the POSIX way to spell digits is `[0-9]`:

```cpp
// Standard: C++17
using namespace std::regex_constants;
std::string s = "abc 123 XYZ";

std::regex def_re(R"(\w+\s\d+)");                 // default ECMAScript: \w and \d both recognized
std::smatch m;
if (std::regex_search(s, m, def_re)) std::cout << "默认 ECMAScript: '" << m[0] << "'\n";

std::regex ext_re(R"([0-9]+)", extended);         // POSIX extended: \d not recognized, use [0-9]
if (std::regex_search(s, m, ext_re)) std::cout << "POSIX extended [0-9]+: '" << m[0] << "'\n";

std::regex icase_re("hello", icase);              // layer on icase for case-insensitive matching
std::cout << "icase 匹配 'HELLO': " << std::boolalpha
          << std::regex_search(std::string("say HELLO world"), icase_re) << '\n';
```

```text
默认 ECMAScript: 'abc 123'
POSIX extended [0-9]+: '123'
icase 匹配 'HELLO': true
```

In real-world engineering, over ninety percent of scenarios run on default ECMAScript, so don't memorize much here—knowing "to switch grammar or add case-insensitivity, go through `regex_constants`" is enough.

## Measured: regex Really Is an Order of Magnitude Slower

Usage covered—now for the most important part of this article. Just saying "regex is slow" is an unsubstantiated claim, so let's put it next to several alternatives and run a real benchmark.

The scenario is plain: process 100,000 log lines and, for each line, decide "does it contain 4 or more consecutive digits". A classic "scan a pile of text on a hot path, one match per line" workload. We compare five approaches:

1. `std::regex` precompiled, constructed once outside the loop, `regex_search` per line;
2. `std::regex` **reconstructed inside the loop every line** (the counterexample);
3. `string::find_first_of("0123456789")` to find the first digit character;
4. a hand-written character state machine: count consecutive digit characters, hit at 4;
5. `std::any_of` + `std::isdigit`, stopping at the first digit found.

All approaches produce the same hit count (74,810 lines), guaranteeing we're comparing speed, not semantics. Best-of-N, `-O2`:

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

The data is blunt. Several takeaways:

- Even with the `regex` object **precompiled**—constructed exactly once outside the loop—it's still **16x slower** than `find_first_of` and nearly **60x slower** than the hand-written state machine.
- If your itchy fingers put `std::regex re(pattern)` inside the loop, recompiling the NFA for every line, you're at **77x**—one regex construction costs 1–2 microseconds (see the last line), and across 100,000 loop iterations, construction alone burns through most of the time.
- Flip it around: code tailored to this one specific need, like the hand-written state machine and `any_of+isdigit`, gets down to a third of `find` at best. That's the cost gap between general-purpose tools and dedicated ones.

An honest footnote: the absolute microseconds drift with machine and load (across our extra runs, the precompiled version floated between 16x and 18x), but the **order-of-magnitude conclusion is robust**—`std::regex` is an order of magnitude slower than hand-written string search, and that holds on every mainstream implementation (libstdc++ / libc++ / MSVC), not just GCC's. The reasons get unpacked in the next section.

::: warning Don't construct regex objects inside loops
This is the number-one `<regex>` trap. The `std::regex` constructor has to do **syntax analysis + NFA compilation**, which is not a cheap operation to begin with (measured above at 1–2us per construction, longer for complex patterns). Putting it in a hot loop means recompiling the state machine every iteration, and performance collapses outright. The correct move: if the pattern is fixed, hoist the `std::regex` object **out of the loop** and construct it once (make it `static const` even), and call only `regex_search` inside the loop. With that one change, the 77x above drops straight back to 16x.
:::

## Why It Is So Slow: The Price of a Backtracking NFA

Data in hand, the mechanism still needs a proper explanation—otherwise this ends up as a "remember, regex is slow" verdict with no reasoning behind it.

Under the hood, `std::regex` (ECMAScript grammar) is a **backtracking NFA** (nondeterministic finite automaton). It does not work by "pre-compiling the pattern into a state machine that answers in one scan"; instead it "scans the input while trying every possible path through the pattern, backtracking to try another whenever one dead-ends". The upside of this machinery is power—capture groups, backreferences, zero-width assertions and the other fancy tricks come naturally to a backtracking NFA, and those features fundamentally cannot be expressed by a pure DFA in theory. The price: **the worst case is exponential**.

Let's make this visible with a classic counterexample: the pattern `(a+)+b`, fed a long run of `a`s with the trailing `b` deliberately withheld. This pattern forces the backtracking NFA to retry every possible grouping combination over the same batch of `a`s—each bit of extra input multiplies the time several-fold:

```cpp
// Standard: C++17
std::regex bad_re(R"((a+)+b)");
for (int n : {16, 20, 24, 28}) {
    std::string s(static_cast<std::size_t>(n), 'a');   // n a's, no trailing b
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

Want to watch the exponential blow-up live? Open the online demo below (n=28 takes 22 seconds and would time out, so the online version stops at n=24—already enough to see each extra 4 characters multiply the time by roughly 20x):

<OnlineCompilerDemo
  title="Catastrophic backtracking: the (a+)+b exponential blow-up"
  source-path="code/examples/vol3/54_regex_backtracking.cpp"
  description="The pattern (a+)+b fed 16/20/24 a's shows exponentially growing runtime; n=28 measured at about 22 seconds, omitted online—this is the essence of why a backtracking NFA offers no linear-time guarantee"
  allow-run
/>

Look at that growth: input length going from 16 to 28 (a mere 12 extra characters) blows the time from 5 milliseconds up to **22 seconds**. Every 4 additional characters multiply the time by roughly 18—textbook-grade **catastrophic backtracking**. Once such a pattern lands on a code path that accepts external input (say, user-submitted strings run through this regex), it's a ready-made DoS vulnerability. This isn't GCC having a bad implementation; it's the nature of a backtracking NFA: linear time is **not guaranteed**—complexity is decided by the pattern's structure.

And this point is the root cause behind the later question of "when to switch to a third-party library".

## When to Use It, and When to Steer Clear

With the costs laid bare, the decision actually gets clear-cut. Here's our rubric:

**Scenarios where `std::regex` fits**—where the pattern's complexity is precisely what justifies the performance price:

- Structured field parsing: emails, phone numbers, URLs, ISO timestamps, protocol headers with capture groups. Writing these with `find` is long-winded and error-prone; regex does it in a line or two and crushes it on readability.
- Nested/optional structures: patterns with optional parts, alternation `(|)`, repeating groups—a hand-written state machine for these turns into spaghetti.
- One-off scripts, cold-start config parsing, rarely-hit call paths: for places that run a few times a year, writing it correctly and quickly beats writing it to run quickly.

**Scenarios to steer around `std::regex`**—where performance or controllability matters more:

- **Simple literal matching**: locating one fixed string? Use `string::find`. Don't swat a mosquito with a regex cannon.
- **Simple character-set checks**: "any digits?", "any whitespace?"—use `find_first_of` or `any_of+isdigit`, one to two orders of magnitude faster than regex.
- **Hot paths, bulk data**: server-side parsing of tens to hundreds of thousands of text records per second—`std::regex`'s constant factor and worst-case exponential degradation are both hazards.
- **Patterns taken from external input**: a user-supplied string used directly as the pattern carries catastrophic-backtracking risk (see the 22 seconds in the previous section).

When steering around it, there are three destinations—pick by scenario:

- **Simple literals/character sets** — the standard library's own `find` / `find_first_of` / `any_of` / `search` (covered in this volume's algorithm articles and the `string` article): zero extra dependencies, fastest.
- **Need regex's expressive power plus a linear-time guarantee** — Google's **RE2**. Built on automata theory, it **guarantees matching time linear in input length** and never backtracks catastrophically. The cost: no backreferences, and none of the fancier zero-width-assertion tricks (precisely the features that keep the standard library's regex from being linearizable). The first choice for server-side parsing of external input.
- **Pattern known at compile time, extreme performance wanted** — **CTRE** (Compile-Time Regular Expressions, since C++17). The pattern is a compile-time constant; it compiles the pattern into a state machine during compilation, so at runtime you're at zero-overhead, hand-written-state-machine level. An excellent fit for fixed patterns in performance-sensitive settings.

We won't unfold these libraries here (this volume stays focused on the standard library), but know this: **their speed fundamentally comes from routing around the "backtracking NFA" road**—RE2 trades automata for linear time, CTRE uses compile-time evaluation to erase the runtime compilation cost. The standard library's `<regex>` is slow precisely because it took the road with the fullest feature set, the biggest constant factors, and no linear-time guarantee.

## A Few Pitfalls You'll Actually Hit

A roundup of the rakes encountered along the way, each backed by the measurements or mechanisms above:

::: warning Constructing regex inside the loop
The number-one trap. `std::regex` construction = syntax analysis + NFA compilation, starting at 1–2us a pop. Putting it in a hot loop = recompiling every iteration. If the pattern is fixed, hoist it out of the loop (or make it `static const`) and call only `regex_search` inside. In the benchmark above, this difference was 4–5x.
:::

::: warning Don't mix up match and search
`regex_match` demands a **whole-string** match; `regex_search` is satisfied with any match inside the string. A beginner writing email validation with `regex_match` is doing it right (the whole string must be an email), but writing `regex_search` lets junk-laden strings like `"联系 abc@x.com 谢谢"` through. To validate "this entire string is exactly some format", use `match`; to "extract a segment from the string", use `search`.
:::

::: warning Catastrophic backtracking is a DoS vulnerability
Patterns like `(a+)+`, `(a|a)*`, and nested quantifiers degrade exponentially on untrusted input (the 28-a run took 22 seconds above). Server-side code that takes external patterns or runs external input through regex should either switch to RE2 (linear-time guarantee) or cap the input length. Don't let a user wedge your thread with a single string.
:::

::: warning Escaping backslashes—use raw strings
Regex is carpeted with backslashes, and in C++ string literals `\` is an escape character. Either write `"\\d+"` (double backslashes, hard to read) or use `R"(\d+)"` (raw string, what-you-see-is-what-you-get). Standardize on `R"()"` for regex work and many bugs never happen.
:::

::: warning A bad pattern throws std::regex_error at construction
With a malformed pattern (unbalanced parentheses, illegal quantifier nesting, and so on), the `std::regex` constructor throws `std::regex_error`, carrying a `code()` and a `what()`. Code that accepts external patterns must wrap this in try/catch, or one illegal pattern takes your process down outright. A precompiled fixed pattern is constructed once, and errors surface at compile/startup time, where they do little harm.
:::

## Summary

`std::regex` is the standard library's **most feature-complete and heaviest** text tool. Treat it as the "can do anything, but don't overuse it" character, and remember these points:

- The trio each mind their own shop: `regex_match` (whole-string match, for validation), `regex_search` (substring search, for extraction), `regex_replace` (rewriting). `smatch` for capture groups: `m[0]` the whole match, `m[1]` group 1.
- Two iterators: `regex_iterator` walks all matches, `regex_token_iterator` tokenizes by pattern (`-1` takes the fields between delimiters, keeping empty fields).
- Default ECMAScript syntax—`\d \w \s` and friends line up with JS; to switch syntax or add case-insensitivity, use `std::regex_constants`.
- **The real cost**: a precompiled `regex` is about **16x slower** than `find_first_of` and nearly 60x slower than a hand-written state machine; constructing it in the loop drags that to **77x**. The order-of-magnitude conclusion is robust; absolute values drift by machine.
- The root cause of the slowness is the **backtracking NFA**: powerful (supports capture groups, backreferences, zero-width assertions) but **exponential** in the worst case—`(a+)+` fed 28 a's runs for 22 seconds, a latent DoS vulnerability.
- The decision: complex patterns (emails/phones/timestamps/nested structures) use it; simple literals use `find`; performance-sensitive or external-input paths go to RE2 (linear guarantee) / CTRE (compile-time evaluation).

In the next article we leave text tools behind and move into input/output and the filesystem—starting with the most basic and most-mocked-for-being-"slow" `<iostream>`, looking at where exactly its slowness comes from and how to use it right.

## References

- [cppreference: `<regex>` header overview](https://en.cppreference.com/w/cpp/regex) — the entry point to the entire regex library
- [cppreference: std::regex_match](https://en.cppreference.com/w/cpp/regex/regex_match) — whole-string match semantics
- [cppreference: std::regex_search](https://en.cppreference.com/w/cpp/regex/regex_search) — substring search semantics
- [cppreference: std::regex_iterator](https://en.cppreference.com/w/cpp/regex/regex_iterator) — iterating all matches
- [cppreference: std::regex_token_iterator](https://en.cppreference.com/w/cpp/regex/regex_token_iterator) — the tokenizing iterator
- [cppreference: std::regex_constants::syntax_option_type](https://en.cppreference.com/w/cpp/regex/syntax_option_type) — syntax flags such as ECMAScript / extended / icase
- [RE2 project](https://github.com/google/re2) — Google's linear-time regex engine, the first-choice replacement for server-side handling of external input
- [CTRE project](https://github.com/hanickadot/compile-time-regular-expressions) — compile-time regular expressions, since C++17, near hand-written-state-machine performance for fixed patterns
