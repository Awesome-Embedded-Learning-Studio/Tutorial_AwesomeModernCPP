---
chapter: 7
cpp_standard:
- 11
- 17
description: We dive into the core trio of `std::regex`, iterator-based tokenization,
  and capture groups. We use real-world benchmarks to reveal that it is an order of
  magnitude slower than `string::find`, and even slower when constructed inside a
  loop. Finally, we provide guidelines on when to use which tool and when to switch
  to a third-party library.
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
title: 'regex: The Heaviest Text Tool in the Standard Library and Its Cost'
translation:
  source: documents/vol3-standard-library/strings/54-regex.md
  source_hash: 631bf7c89e1bfa6b03fc114f11b7120c7cf2528400d3c5034a348ac5f16bc6a0
  translated_at: '2026-06-24T02:32:14.528597+00:00'
  engine: anthropic
  token_count: 3449
---
# regex: The Heaviest Text Tool in the Standard Library and Its Cost

Previously, we covered containers, iterators, and algorithms, relying on "self-explanatory" interfaces like `string::find` and `find_first_of`. Now, let's change gears and look at the heaviest text tool in the standard library: `<regex>`.

Why dedicate a whole article to this? The reason is practical: `std::regex` is one of the few components in the standard library that is powerful enough to write production-grade patterns directly, yet slow enough to drag down an entire hot path. It supports capture groups, backreferences, named groups, zero-width assertions, four syntax flavors, case-insensitivity... basically everything you need for daily regex tasks. The cost is that it runs at least an order of magnitude slower than hand-written string searches. Many beginners don't realize this until they go live and their CPU gets hammered. So, in this article, we won't just cover how to use it; more importantly, we'll use real data to show you exactly where the "weight" and the "slowness" come from, so you know when to embrace it and when to steer clear.

We'll start with the basic trio, cover common usage, and then dedicate a specific section to performance comparison—that is the core value of this article.

## The Trio: match / search / replace

The three top-level functions in `<regex>` correspond exactly to the three most common text processing needs:

- `std::regex_match` —— The **entire string** must match the pattern completely (from start to finish);
- `std::regex_search` —— **Searches** for any matching substring within the string (returns on the first find, no full string requirement);
- `std::regex_replace` —— Replaces all (or some) matches with different content.

These names look similar, but their semantics differ significantly. Beginners are most likely to trip up on `match` versus `search`. `match` requires the **entire string** to match; even one character off fails. `search` wins as long as some segment within the string fits. Let's demonstrate this distinction first:

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

Run with `g++ -std=c++17 -O2` (native GCC 16.1.1):

```text
regex_match 邮箱: true
regex_match 带垃圾: false
search 找到第一段数字: 12345 (位置 8)
```

Pay attention to two details. First, we wrote the pattern string as `R"(...)"`. This is a C++11 **raw string literal**, which prevents the C++ compiler from consuming backslashes. Since regex patterns are full of backslashes like `\d`, `\w`, and `\.`, using a regular string would require the visually cluttered double-escape syntax like `"\\d+"`. Using `R"()"` is the standard practice for regex. Second, `regex_search` only returns the **first** match. To get all subsequent matches, we need to use an iterator, which is exactly what the next section covers.

### smatch: How to Extract Capture Groups

The `std::smatch` we used above (an alias for `match_results<std::string::const_iterator>`) is not just a boolean result—it stores the entire match and all **sub-matches**. Each pair of parentheses in the pattern represents a capture group. `m[0]` is the full match, while `m[1]`, `m[2]`, and so on correspond to the respective parentheses. Let's demonstrate this with a log string containing timestamps:

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

Parentheses are numbered from left to right, where `(\d{4})` is capture group 1 corresponding to the year, and so on. `ts[0]` is always the "entire matched text". Capture groups are where regex becomes truly useful in engineering—extracting structured fields, parsing protocol headers, and rewriting templates all rely on them.

### regex_replace: Replacing Matches

The third function handles rewriting. By default, it **replaces all** matches, but we can pass the `format_first_only` flag to replace only the first occurrence:

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

We need to highlight a potential pitfall first: **do not write bare `$` characters** in the replacement string of `regex_replace`. In ECMAScript syntax, `$1`, `$&`, and similar sequences are backreferences (`$1` represents the first capture group, and `$&` represents the entire match). If you simply want to replace a number with a literal `$`, you must be careful to prevent it from being interpreted as a special character. For simple scenarios, replacing it with a normal string as shown above works fine.

## Iterators: Traversing All Matches + Tokenization

The trio of functions covers most reading, writing, and modifying needs, but there are two things they cannot do: traversing **all** matches in a string (`regex_search` only gives the first one) and **tokenizing** based on a pattern. The standard library provides two iterators for these purposes.

`std::regex_iterator` turns "giving the next match on every `++`" into an iterator, so traversing all matches becomes a one-liner range-based `for` loop:

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

Note that the default-constructed `end` acts as a **sentinel**, indicating "reached the end of matches." We saw this pattern in the previous article on stream iterators (the EOF sentinel for `istream_iterator`); the logic is identical: you don't need to know the number of matches in advance, as the iterator stops automatically at the end. Dereferencing `*it` yields a `smatch`, so `(*it)[1]` directly accesses the capture group.

The other iterator is `std::regex_token_iterator`, designed specifically for tokenization. Its key parameter is the last one: passing `-1` means "get the content **between** matches" (treating matches as delimiters and returning the remaining fields); passing `0` or a non-negative number means "get the match itself or the Nth capture group":

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

Five segments, and the empty field between the consecutive commas was preserved as-is as `[2] ''`. This is a crucial detail—unlike many manual `split` implementations, `regex_token_iterator` does not merge consecutive delimiters. Empty strings between consecutive delimiters are still counted as a segment, and the `[2] ''` above is the proof.

::: warning Don't roll your own tokenizer
When faced with "splitting a string by a delimiter," many developers' first instinct is to write a manual loop to find delimiters and extract substrings. First, consider if your intended behavior is to **preserve empty fields**—manual loops often treat consecutive delimiters as a single one, silently dropping fields. `regex_token_iterator` has clear semantics (preserves empty fields) and predictable behavior. Of course, if your requirement is explicitly "to drop empty fields," that's a different story, but that should be a **conscious decision**, not a bug.
:::

## Default Syntax: ECMAScript, Similar to What You Write in JS/Python

`std::regex` uses **ECMAScript** syntax by default—yes, the same one used in JavaScript. This means that the `\d`, `\w`, `\s`, `{n,m}`, `(?:...)`, and `(?=...)` patterns you are used to writing in JS can be copied over almost verbatim. Here is a quick reference for the commonly used metacharacters and character classes:

| Syntax | Meaning |
|---|---|
| `.` | Any single character (excluding newline by default) |
| `\d` `\D` | Digit / Non-digit |
| `\w` `\W` | Word character (alphanumeric and underscore) / Non-word |
| `\s` `\S` | Whitespace / Non-whitespace |
| `*` `+` `?` | 0 or more / 1 or more / 0 or 1 |
| `{n}` `{n,m}` | Exactly n times / n to m times |
| `[abc]` `[^abc]` | Character set / Negated set |
| `^` `$` | Start of line / End of line |
| `(...)` `(?:...)` | Capturing group / Non-capturing group |
| `(?=...)` `(?!...)` | Lookahead (positive/negative) |
| `\1` | Backreference to group 1 |

When constructing a `std::regex`, you can pass syntax flags from `std::regex_constants` to switch to other syntaxes (like `extended`, `grep`, or `awk` from the POSIX family), or stack `icase` to enable case-insensitive matching. Here is a real pitfall to watch out for: **different syntaxes support different character classes**. For example, `\d` is a shorthand in ECMAScript, but it is not recognized in `extended` (POSIX) syntax—in POSIX, you must write digits as `[0-9]`:

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

In real-world projects, over 90% of the time you will stick with the default ECMAScript syntax. You don't need to memorize much here; just knowing that "if you need to change syntax or enable case-insensitive matching, rely on `regex_constants`" is sufficient.

## Real-World Benchmark: Is regex Really an Order of Magnitude Slower?

Now that we've covered the usage, we've reached the most critical part of this article. Simply claiming "regex is slow" is an empty assertion, so we will run a real benchmark comparing it against several alternatives.

The scenario is straightforward: process 100,000 lines of logs, checking each line to see if it contains "four or more consecutive digits." This represents a typical requirement: "scanning a large volume of text in a hot path and performing a match on every line." We compare five different implementations:

1. `std::regex`: Pre-compiled, constructed once outside the loop, using `regex_search` for each line;
2. `std::regex`: **Re-constructed inside the loop for every line** (the anti-pattern);
3. `string::find_first_of("0123456789")`: Finds the first numeric character;
4. Hand-written character state machine: Counts consecutive numeric characters and triggers a match upon reaching four;
5. `std::any_of` + `std::isdigit`: Stops as soon as the first digit is found.

All methods yielded the same number of matches (74,810 lines), ensuring we are comparing speed rather than semantic differences. Best-of-N, compiled with `-O2`:

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

The data is straightforward. Here are a few conclusions:

- Even when the `regex` object is **pre-compiled** and constructed only once outside the loop, it is still **16 times slower** than `find_first_of` and nearly **60 times slower** than a hand-written state machine.
- If we mistakenly write `std::regex re(pattern)` inside the loop, recompiling the NFA for every single line, it slows down by a factor of **77**. Constructing a regex takes 1–2 microseconds (see the last row); looping 100,000 times means construction burns through the majority of the time.
- Conversely, hand-written state machines and code like `any_of + isdigit` that is "tailor-written for one specific requirement" can be pushed down to one-third the time of `find`. This represents the performance cost between general-purpose tools and specialized tools.

To be honest, the absolute microsecond values fluctuate with machine load and hardware (we ran several rounds, and the pre-compiled version drifted between 16x and 18x), but the **order-of-magnitude conclusion is robust**. `std::regex` is an order of magnitude slower than hand-written string searching across all mainstream implementations (libstdc++, libc++, MSVC); this isn't a GCC-specific issue. We will explain why in the next section.

::: warning Do not construct regex objects inside loops
This is the number one pitfall of `<regex>`. The `std::regex` constructor performs **parsing + NFA compilation**, which is not a cheap operation (measured above at 1–2 µs, and longer for complex patterns). Putting it in a hot loop means recompiling the state machine every iteration, causing performance to collapse. The correct approach is: if the pattern is fixed, construct the `std::regex` object **outside the loop** (or even make it `static const`), and only call `regex_search` inside the loop. With this single change, the 77x overhead drops back down to 16x.
:::

## Why is it so slow: The cost of backtracking NFAs

We have the data, but we need to explain the mechanism; otherwise, we are left with just "remember regex is slow" without understanding the cause.

`std::regex` (using ECMAScript grammar) is implemented as a **backtracking NFA** (Nondeterministic Finite Automaton). Its working method is not "compile the pattern into a state machine that produces results in a single pass," but rather "scan the input while trying all possible paths through the pattern; if a path fails, backtrack and try another." The benefit of this mechanism is power—it naturally supports features like capture groups, backreferences, zero-width assertions, and other tricks, which theoretically cannot be expressed by a pure DFA. The代价 is **worst-case exponential time**.

Let's use a classic counter-example to visualize this: the pattern `(a+)+b` fed a long string of `a`s, intentionally omitting the trailing `b`. This pattern forces the backtracking NFA to retry various grouping combinations on the same batch of `a`s. As the input grows, the time skyrockets:

```text
Input Length   Time (us)
10             1
20             4
30             16
...
```

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

Want to see exponential explosion in action? Check out this online demo (n=28 takes 22 seconds and times out, so the online version only runs up to n=24, which is enough to show the time multiplying by ~20 for every 4 characters added):

<OnlineCompilerDemo
  title="Catastrophic Backtracking: Exponential Explosion of (a+)+b"
  source-path="code/examples/vol3/54_regex_backtracking.cpp"
  description="Feeding pattern (a+)+b with 16/20/24 a's shows exponential time growth; n=28 takes ~22s (measured), omitted online—this is the essence of backtracking NFAs not guaranteeing linear time"
  allow-run
/>

Look at this growth: input length goes from 16 to 28 (just 12 more characters), and time explodes from 5 ms to **22 seconds**. Every 4 characters adds roughly an 18x multiplier—this is a textbook example of **catastrophic backtracking**. Once this pattern lands in a code path accepting external input (like a regex filtering user-submitted strings), it becomes a ready-made DoS vulnerability. This isn't because GCC's implementation is bad; it's the nature of backtracking NFAs: they **do not guarantee** linear time, and complexity is determined by the pattern structure.

This point is the root cause of the "when to switch to a third-party library" discussion coming up next.

## When to Use It, When to Bypass It

Now that we've thoroughly covered the cost, the decision becomes clearer. Here is a framework for judgment:

**Scenarios where `std::regex` is appropriate**—the complexity of the pattern justifies the performance cost:

- **Structured field parsing**: Email addresses, phone numbers, URLs, ISO timestamps, protocol headers with capture groups. Writing these with `find` is verbose, lengthy, and error-prone, whereas regex handles them in one or two lines with vastly superior readability.
- **Nested/Optional structures**: Patterns with optional parts, alternation `(|)`, or repeated groups. Hand-writing state machines for these turns into spaghetti code.
- **One-off scripts, cold-start config parsing, low-frequency paths**: Code that runs a few times a year. Being correct and writing it fast matters more than it running fast.

**Scenarios where `std::regex` should be bypassed**—performance or controllability is more important:

- **Simple literal matching**: Just finding a fixed string? Use `string::find`. Don't use a regex cannon to kill a mosquito.
- **Simple character set checks**: Checking "has digits" or "has whitespace"? Use `find_first_of` or `any_of` + `isdigit`. It's orders of magnitude faster than regex.
- **Hot paths, batch data**: Server-side parsing processing tens to hundreds of thousands of text lines per second. `std::regex`'s constant factor and worst-case exponential degradation are both risks.
- **Patterns accepting external input**: If the pattern comes from the user, or the regex processes untrusted input, the risk of catastrophic backtracking (see the 22 seconds above) is real.

When bypassing, there are three alternatives, depending on the scenario:

- **Simple literals/character sets** — Standard library's `find` / `find_first_of` / `any_of` / `search` (covered in the Algorithms and `string` chapters of this volume). Zero extra dependencies, fastest.
- **Need regex expressiveness but guaranteed linear time** — Google's **RE2**. It is based on automata theory and **guarantees matching time is linear with respect to input length**, preventing catastrophic backtracking. The cost is lack of support for backreferences and some zero-width assertions (precisely the features that prevent linearization in standard regex). The top choice for server-side parsing of external input.
- **Compile-time known patterns, extreme performance** — **CTRE** (Compile-Time Regular Expressions, C++17+). The pattern is a compile-time constant; it compiles the pattern into a state machine at compile time, resulting in zero-overhead, hand-written state machine performance at runtime. Perfect for fixed patterns in performance-sensitive scenarios.

We won't expand on these libraries here (this volume focuses on the standard library), but you should know: **their speed essentially comes from bypassing the "backtracking NFA" route**—RE2 uses automata to trade for linear time, CTRE uses compile-time evaluation to eliminate runtime compilation overhead. The reason standard library `<regex>` is slow is that it chose the route with the most features, but the highest constant factor and no linear guarantees.

## Common Pitfalls in Practice

Let's collect the pitfalls from this journey; each is backed by the benchmarks or mechanisms above:

::: warning Constructing regex in a loop
The number one pitfall. `std::regex` construction = parsing + compiling NFA, starting at 1~2us a pop. Putting it in a hot loop = recompiling every iteration. If the pattern is fixed, move it outside the loop (or make it `static const`), and only call `regex_search` inside. The benchmark above showed a 4~5x difference for this.
:::

::: warning Don't mix up match and search
`regex_match` requires the **entire string** to match, while `regex_search` only requires a match somewhere in the string. A newbie writing email validation with `regex_match` is correct (wanting the whole string to be an email), but writing `regex_search` would let through strings like `"contact abc@x.com thanks"` containing garbage. Use `match` to validate "the whole string is X format", use `search` to "extract a part of a string".
:::

::: warning Catastrophic backtracking is a DoS vulnerability
Patterns like `(a+)+`, `(a|a)*`, and nested quantifiers degrade exponentially when receiving untrusted input (28 a's took 22 seconds above). Server-side code accepting external patterns or external input via regex should either switch to RE2 (linear time guarantee) or impose an upper limit on input length. Don't let a user hang your thread with a single string.
:::

::: warning Escape backslashes, use raw strings
Regex is full of backslashes, and `\` is an escape character in C++ string literals. Either write `"\\d+"` (double backslash, hard to read) or use `R"(\d+)"` (raw string, WYSIWYG). Uniformly use `R"()"` for regex to avoid many bugs.
:::

::: warning Pattern string exceptions throw std::regex_error at construction
If the pattern is invalid (unbalanced parentheses, illegal quantifier nesting, etc.), the `std::regex` constructor throws `std::regex_error`, carrying a `code()` and `what()`. Code accepting external patterns must try/catch, otherwise an illegal pattern crashes your process immediately. Pre-compiled fixed patterns are constructed once; errors are found at compile/startup time, which is less harmful.
:::

## Summary

`std::regex` is the **most feature-rich and heaviest** text tool in the standard library. Treat it as a "can do anything but don't abuse it" tool, and remember these points:

- The big three have their roles: `regex_match` (full string match, for validation), `regex_search` (search substring, for extraction), `regex_replace` (rewrite). Use `smatch` to get capture groups: `m[0]` for the whole match, `m[1]` for the first group.
- Two iterators: `regex_iterator` iterates all matches, `regex_token_iterator` tokenizes by pattern (`-1` takes fields between delimiters, preserving empty fields).
- Default ECMAScript syntax; `\d \w \s` match JS semantics. To change syntax or add case-insensitivity, use `std::regex_constants`.
- **Real cost**: A pre-compiled `regex` is about **16x slower** than `find_first_of`, and nearly **60x slower** than a hand-written state machine; constructing inside a loop slows it down to **77x**. The order of magnitude conclusion is robust, absolute values vary by machine.
- The root cause of slowness is the **backtracking NFA**: Powerful features (capture groups, backreferences, zero-width assertions) but worst-case **exponential** time. `(a+)+` fed with 28 a's runs for 22 seconds, a potential DoS vulnerability.
- Decision: Use it for complex patterns (email/phone/timestamp/nested structures). Use `find` for simple literals. For performance-sensitive or external input processing, switch to RE2 (linear guarantee) / CTRE (compile-time evaluation).

In the next article, we leave text tools behind and enter input/output and the filesystem—starting with the most basic, and most often criticized "slow" `<iostream>`, to see exactly why it's slow and how to use it correctly.

## Reference Resources

- [cppreference: `<regex>` header overview](https://en.cppreference.com/w/cpp/regex) — Entry point for the entire regex library
- [cppreference: std::regex_match](https://en.cppreference.com/w/cpp/regex/regex_match) — Full string match semantics
- [cppreference: std::regex_search](https://en.cppreference.com/w/cpp/regex/regex_search) — Substring search semantics
- [cppreference: std::regex_iterator](https://en.cppreference.com/w/cpp/regex/regex_iterator) — Iterate all matches
- [cppreference: std::regex_token_iterator](https://en.cppreference.com/w/cpp/regex/regex_token_iterator) — Tokenization iterator
- [cppreference: std::regex_constants::syntax_option_type](https://en.cppreference.com/w/cpp/regex/syntax_option_type) — Syntax flags like ECMAScript / extended / icase
- [RE2 Project](https://github.com/google/re2) — Google's linear-time regex engine, the preferred alternative for server-side processing of external input
- [CTRE Project](https://github.com/hanickadot/compile-time-regular-expressions) — Compile-time regex, C++17+, approaches hand-written state machine performance when patterns are fixed
