---
chapter: 8
cpp_standard:
- 17
description: Understand how string_view works internally, how it compares with SSO, and where its views can come from
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 0: Rvalue References: From Copy to Move'
reading_time_minutes: 18
related:
- string_view Performance Analysis
- string_view Pitfalls and Best Practices
tags:
- host
- cpp-modern
- intermediate
title: 'string_view Internals: A Non-Owning String View'
translation:
  source: documents/vol2-modern-features/ch08-string-view/01-string-view-internals.md
  source_hash: c0ae21e4e245ec5e0a73829583c94399eb32bfc481d43e7a6a139feffd59f011
  translated_at: '2026-09-27T05:10:15+00:00'
  engine: anthropic
  token_count: 9000
---
# string_view Internals: A Non-Owning String View

While writing an IniParser project recently, we got thoroughly sick of dealing with strings — split, trim, substr, operations flying everywhere. Every substring operation on a `std::string` means a heap allocation, and after parsing one config file, the heap was more fragmented than our desk. Then we took a serious look at `std::string_view` and found out what a handy tool C++17 had prepared for us. But the precondition for using it well is genuinely understanding its internal mechanics — otherwise it is easy to step right into a lifetime pit, which we will dissect in detail in the next article, the pitfalls one.

In this article we focus on the internals of `string_view`: what it actually looks like, why it is so lightweight, where the essential difference from `std::string` lies, and which operations it provides.

## What Exactly Is string_view

`std::string_view` (C++17) is a lightweight, immutable "string view" type. The keyword is "view" — it does **not own** the character buffer; it stores just two things: a pointer to the start of the character sequence, and the length of that sequence. So as you can see, the name is completely literal: it is a "view", an observation window, not an owner of the data.

> Reference: [cppreference -- std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)

### Internal Representation: Two Fields Do Everything

Although the C++ standard does not mandate a concrete internal structure, all mainstream implementations (libstdc++, libc++, MSVC STL) use the same scheme — a simple two-field struct:

```cpp
template<class CharT, class Traits = std::char_traits<CharT>>
class basic_string_view {
    const CharT* _ptr;   // points to the underlying character sequence (not owned)
    size_t       _len;   // length (excluding '\0')
};
```

Just these two fields, one pointer and one length. Copying a `string_view` is just copying these two words — 16 bytes on a 64-bit system. No heap allocation, no reference counting, no destructor logic. That is the fundamental reason it is lightweight.

### The Relationship with std::string: View vs Ownership

The key step to understanding `string_view` is getting clear on the difference between "view" and "own". `std::string` is an owner: it allocates memory on the heap to store the characters and manages the lifetime of that memory, covering construction, copying, moving, and the final release. You can think of it as "I bought this apartment, and the deed has my name on it".

A `string_view`, on the other hand, is an observer: it allocates no memory, it just points at someone else's data and says "let me have a look". It is like a friend buying a house and you holding a key to drop by — you can use the living room and the kitchen, but the house is not yours; the day your friend sells the house (the underlying `string` is destroyed), the key in your hand turns into scrap.

The direct benefit of this design is that no "substring operation" needs to allocate new memory. For example, `substr` just moves the pointer forward and shortens the length, with O(1) complexity. `std::string::substr` has to allocate new memory and copy the characters, with O(n) complexity. This difference becomes very visible in scenarios with frequent substring operations (parsers, protocol handling, and the like).

Let us compare concretely in code how `string_view` and `std::string` behave differently for substring operations. `string_view::substr` is roughly equivalent to:

```cpp
string_view substr(size_t pos, size_t count) const {
    return string_view(_ptr + pos, min(count, _len - pos));
}
```

The narrowing and shifting of the window over that same block of memory has been turned into an animation — you can play it, pause it, or single-step it with the step button, and watch every adjustment of the pointer and the length clearly:

<Anim id="string-view-window" />

No new memory is opened up at all; only the pointer and the length are adjusted. `std::string::substr`, by contrast, must go through the full allocate-and-copy routine. Suppose we are processing a 1MB config file and calling `substr` on each of its fields — possibly thousands of calls. With `std::string` that is thousands of heap allocations; with `string_view` it is thousands of pointer adjustments. The gap speaks for itself.

Beyond `substr`, query operations such as `find`, `compare`, and `rfind` also traverse the memory `_ptr` points to directly (relying on `Traits::compare`), with no new memory created. The design philosophy of `string_view` can be summed up in one sentence: it is a lightweight facade that turns an arbitrary character sequence into an "operable read-only string object", but never takes responsibility for memory. This is both its greatest advantage and the root of all its risks — after all, if nobody is responsible for cleanup, somebody has to be, and that somebody is you, the programmer.

### SSO: Small String Optimization

When talking about the overhead of `std::string`, we have to bring up SSO (Small String Optimization). Mainstream `std::string` implementations all adopt an SSO strategy: when a string is short enough (usually 15-22 bytes, depending on the implementation), the character data is stored directly in a buffer inside the object, with no heap allocation. Only when the string grows past this threshold does it switch to heap allocation mode.

SSO is a great optimization — copying short strings becomes cheap. But it does not eliminate all overhead. A `std::string` object itself is usually 24-32 bytes in size (implementation-dependent, covering the SSO buffer, the length, the capacity, and so on), and its copy semantics mean that even when SSO kicks in, the character data still has to be replicated byte by byte. By comparison, `string_view` is only 16 bytes (on a 64-bit system), and a copy is always a memcpy of two words, no matter how long the string is.

This comparison is not saying that `string_view` is better than `std::string` — they solve different problems. `std::string` manages ownership; `string_view` provides a read-only view. In scenarios where you need to modify the string or hold a copy of it, `std::string` remains the only choice.

### The Essential Comparison with const char*

If we pull the perspective back a bit further, the design of `string_view` is conceptually a wrapper around `const char*`. If `std::string` wraps `char[]` (with ownership), then `string_view` wraps `const char*` (without ownership, but with length information added). This "extra length information" looks like a small change, but its practical impact is huge.

Getting the length of a `const char*` requires calling `strlen`, an O(n) traversal. Worse, if your function uses the string length multiple times internally and never caches it proactively, you end up calling `strlen` over and over, quietly sliding into an O(n^2) performance pattern. `string_view` stores the length directly in the object, so `size()` is O(1) — just a read of a member variable.

Another frequently overlooked problem is that `const char*` can only represent strings terminated with `\0`. This means it cannot correctly handle binary data containing zero bytes, nor represent a substring without modifying the original data (because the end of the substring does not necessarily have a `\0`). `string_view` solves both problems with an explicit length: it can point at an arbitrary byte sequence (including ones with `\0` in the middle) and can safely represent any sub-range.

| Feature | `std::string_view` | `const char*` |
|------|---------------------|---------------|
| Carries a length | Yes, `size()`, O(1) | No — needs `strlen`, O(n) |
| Safe to represent substrings | Fully supported (length is known) | Only by temporarily modifying `\0` or passing an extra length |
| Supports sequences with embedded zero bytes | Yes (length is independent) | No — depends on NUL termination |
| Rich interface (find, compare) | Rich member functions | Almost none — only C functions |
| Literal syntax | `"abc"sv` | `"abc"` |

The core difference in one sentence: `string_view = (pointer, length)`, `const char* = pointer + implied '\0' termination`. The explicit length of `string_view` is an enormous advantage, because in many scenarios `\0` is not what we intend.

## Construction Sources: Where Do Views Come From

Our test environment for today: Linux, GCC 13 or Clang 17 or above, compile options `-std=c++17 -O2`. All code examples can be compiled and run directly.

A `string_view` can be constructed from many kinds of sources. The three most common ones:

A `string_view` can be constructed from many kinds of sources. The three most common ones:

The first is constructing from a C-style string literal. String literals have static storage (usually placed in the executable's .rodata section), so a `string_view` pointing at one is safe, with a lifetime covering the entire run of the program:

```cpp
std::string_view sv = "hello, world";
// sv points at the string literal in static storage, valid forever
```

The second is constructing from a `std::string`. `std::string` provides an implicit conversion operator to `string_view`, so you can pass it directly:

```cpp
std::string str = "hello";
std::string_view sv = str;  // implicit conversion
// sv points at str's internal buffer; safe as long as str is alive
```

There is a classic trap here: if `str` is a temporary object, then `sv` points at destroyed memory — that is, a dangling reference. For example, `std::string_view sv = std::string("temp");` is undefined behavior. We will discuss this problem in detail in the pitfalls article.

The third is constructing from a specified range, passing in the pointer and the length by hand:

```cpp
const char* buf = "hello, world";
std::string_view sv(buf, 5);  // view only the first 5 characters: "hello"
```

This approach offers the most flexibility, and it is the construction method used inside many parsers. You can even point at some segment in the middle of a buffer that contains `\0` — because `string_view` delimits its bounds with the length, not with a `\0` terminator.

C++17 also provides the literal suffix `sv`, so you can write `"hello"sv` to get a `std::string_view`. This suffix is defined in the `std::literals::string_view_literals` namespace:

```cpp
using namespace std::literals::string_view_literals;
auto sv = "hello"sv;  // std::string_view
```

## Differences from a const std::string& Parameter

Many tutorials will tell you to "replace `const std::string&` function parameters with `string_view`". That is broadly correct, but we need to understand the concrete differences between the two to make the right choice in the right scenario.

With a `const std::string&` parameter, the caller must provide a `std::string` object. If the caller has only a `const char*` or a string literal at hand, the compiler implicitly constructs a temporary `std::string` — which involves a possible heap allocation and a copy. With a `string_view` parameter, a `std::string`, a `const char*`, or a string literal can all construct the `string_view` directly, at the cost of copying one pointer and one length.

```cpp
// Approach 1: const string& parameter
void process_old(const std::string& s);

process_old(std::string("temp"));  // construct string → pass reference
process_old("literal");            // implicitly construct temporary string → pass reference → temporary destroyed
process_old(some_c_string);        // implicitly construct temporary string → strlen + possible allocation

// Approach 2: string_view parameter
void process_new(std::string_view sv);

process_new(std::string("temp"));  // implicit view from string → no extra allocation
process_new("literal");            // construct view directly → zero allocation
process_new(some_c_string);        // construct view directly → needs strlen (O(n)), but no heap allocation
```

You can see that the `string_view` version avoids constructing unnecessary temporary `std::string`s. In hot-path functions that are called frequently, this difference accumulates into a considerable performance gain. There is, however, one difference in the reverse direction: `const std::string&` guarantees the data is `\0`-terminated (because the source is always a `std::string`), while `string_view` does not. If your function needs to call a C API internally (say `printf("%s", ...)`), `string_view` may actually dig a pit for you.

## A Tour of the Core Member Functions

With the principles out of the way, let us see which operations `string_view` provides.

### Element Access

`operator[]` and `at()` access characters by index. `operator[]` does no bounds checking (in release mode); `at()` does check bounds and throws `std::out_of_range` when out of range. `data()` returns a pointer to the underlying character sequence. `size()` and `length()` return the number of characters; `empty()` reports whether the view is empty.

```cpp
std::string_view sv = "hello";

char c = sv[1];         // 'e', no bounds check
char d = sv.at(1);      // 'e', with bounds check
const char* p = sv.data();  // pointer to 'h'
std::size_t n = sv.size();  // 5
bool e = sv.empty();        // false
```

The return value of `data()` is **not guaranteed** to be `\0`-terminated. If the `string_view` was produced by `substr` or `remove_suffix`, the end of the buffer `data()` points at very likely has no `\0`. Passing `data()` straight into a C API that requires NUL termination is a common source of bugs. If you truly need a NUL-terminated string, you must explicitly construct `std::string(sv)`.

### Modifying the View Itself

`string_view` provides three operations that modify itself — note that what is modified is the "view" itself (that is, the pointer and the length), not the underlying data. All of these operations are O(1), because they merely adjust two fields:

```cpp
std::string_view sv = "hello, world";

// remove_prefix: move the start of the view forward by n characters
sv.remove_prefix(7);   // sv becomes "world"

// remove_suffix: shorten the end of the view by n characters
std::string_view sv2 = "hello, world";
sv2.remove_suffix(7);  // sv2 becomes "hello"

// swap: exchange the contents of two string_views
std::string_view a = "first";
std::string_view b = "second";
a.swap(b);  // a -> "second", b -> "first"
```

`remove_prefix` and `remove_suffix` are particularly useful in parsers. For example, to skip a fixed prefix or strip a trailing delimiter, just call these two functions — no need to create a new `string_view` object.

Let us look at a slightly more complete parsing scenario: extracting the key and the value from a string in `"key=value"` format. This is very common in config-file parsing and HTTP header parsing.

```cpp
#include <string_view>
#include <iostream>
#include <optional>
#include <utility>

/// @brief Extract a key-value pair from a string in "key=value" format
/// @param entry the input string view, e.g. "host=localhost"
/// @return a (key, value) pair on success, std::nullopt on failure
std::optional<std::pair<std::string_view, std::string_view>>
parse_kv(std::string_view entry) {
    auto pos = entry.find('=');
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    auto key = entry.substr(0, pos);
    auto value = entry.substr(pos + 1);
    // Trim leading and trailing whitespace
    while (!key.empty() && key.front() == ' ') {
        key.remove_prefix(1);
    }
    while (!key.empty() && key.back() == ' ') {
        key.remove_suffix(1);
    }
    while (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
    }
    while (!value.empty() && value.back() == ' ') {
        value.remove_suffix(1);
    }
    if (key.empty()) {
        return std::nullopt;
    }
    return std::make_pair(key, value);
}

int main() {
    const char* raw = "  host = localhost ; port = 8080 ";
    std::string_view input(raw);
    // Manually split on ';' and parse the key-value pairs one by one
    while (!input.empty()) {
        auto semi = input.find(';');
        auto segment = (semi == std::string_view::npos)
                           ? input
                           : input.substr(0, semi);
        auto result = parse_kv(segment);
        if (result) {
            std::cout << "key=[" << result->first << "] "
                      << "value=[" << result->second << "]\n";
        }
        if (semi == std::string_view::npos) {
            break;
        }
        input.remove_prefix(semi + 1);
    }
    return 0;
}
```

This parsing program is right below — click "Try it yourself" to run it directly:

<OnlineCompilerDemo
  title="Hands-On: Zero-Copy key=value Parsing"
  source-path="code/examples/vol2/41_parse_kv.cpp"
  description="Parse a config string with whitespace and semicolons online: remove_prefix consumes the input segment by segment, zero copies the whole way, and the key/value come out cleanly sliced."
  run-options="-std=c++17"
  allow-run
/>

Note the key operations here: we use `remove_prefix` to consume the input string segment by segment, `substr` to extract the fragment without the delimiter, and `remove_prefix` / `remove_suffix` to do the trimming. The whole process makes zero copies of the original data — `string_view` just keeps adjusting the pointer and the length. On a parser's hot path, this pattern can noticeably reduce the number of memory allocations.

But watch the same caveat: in this example `raw` is a `const char*` literal whose lifetime covers the entire program. If `raw` came from a local `std::string` variable, every `string_view` would dangle after the function returns. This is what we keep stressing — understanding lifetimes is the number one rule of using `string_view`.

## In Practice: Hand-Writing a Simple Token Splitter

After all that theory, let us get a feel for how `string_view` is used with a real example. Below is a function that splits a string by a delimiter:

```cpp
#include <string_view>
#include <vector>
#include <iostream>

std::vector<std::string_view> split(std::string_view input, char delim) {
    std::vector<std::string_view> tokens;
    while (true) {
        auto pos = input.find(delim);
        if (pos == std::string_view::npos) {
            if (!input.empty()) {
                tokens.push_back(input);
            }
            break;
        }
        tokens.push_back(input.substr(0, pos));
        input.remove_prefix(pos + 1);  // skip the delimiter
    }
    return tokens;
}

int main() {
    std::string line = "name=Alice;age=30;city=Beijing";
    auto tokens = split(line, ';');
    for (auto tk : tokens) {
        std::cout << "[" << tk << "]\n";
    }
    return 0;
}
```

This splitter is right below — click "Try it yourself" to run it directly:

<OnlineCompilerDemo
  title="Hands-On: A Token Splitter"
  source-path="code/examples/vol2/42_token_split.cpp"
  description="Run the hand-written splitter online: three segments cut at semicolons, with no heap allocation at all beyond the vector's own growth."
  run-options="-std=c++17"
  allow-run
/>

Look closely at the logic inside the `split` function: we repeatedly call `remove_prefix` to advance the start of the view, and `substr` to extract each token. There are no heap allocations in the whole process (apart from the `vector`'s own growth); every operation is an O(1) pointer adjustment. Implemented with `std::string`, every `substr` would allocate new memory — for a simple INI file parser, that overhead is entirely unnecessary.

The returned vector of `string_view`s points into the internal buffer of the original `line`. If `line` is destroyed, all of those `string_view`s dangle. In a real project, you may need to copy these tokens with `std::string`, or clearly document the lifetime constraints on the return value.

## Embedded in Practice: Command Parsing

`string_view` is just as useful in embedded scenarios. Many embedded systems receive text commands over a serial port (an AT command set, or custom debug commands, for example), and parsing those commands with `string_view` avoids unnecessary string copies — which is especially valuable on MCUs with constrained heap memory.

```cpp
#include <string_view>
#include <cstring>

/// @brief A simple serial command parser
/// @param cmd the input command view, e.g. "LED ON" or "PWM 128"
void handle_command(std::string_view cmd) {
    // Strip the trailing newline characters
    while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == '\n')) {
        cmd.remove_suffix(1);
    }

    // Split the command and its argument at the space
    auto space = cmd.find(' ');
    auto verb = (space == std::string_view::npos) ? cmd : cmd.substr(0, space);

    if (verb == "LED") {
        auto arg = (space == std::string_view::npos)
                       ? std::string_view{}
                       : cmd.substr(space + 1);
        if (arg == "ON") {
            hal_gpio_write(kLedPin, true);
        } else if (arg == "OFF") {
            hal_gpio_write(kLedPin, false);
        }
    } else if (verb == "PWM") {
        auto arg = cmd.substr(space + 1);
        // Convert the string_view to an integer
        int value = 0;
        for (char c : arg) {
            if (c >= '0' && c <= '9') {
                value = value * 10 + (c - '0');
            }
        }
        hal_pwm_set_duty(value);
    }
}
```

This example shows the typical use of `string_view` in an embedded setting: receive a command fragment sliced out of the serial buffer, strip the newline with `remove_suffix`, split the verb and the argument at the space, then do simple string matching. Zero heap allocations throughout — every operation is an adjustment of the pointer and the length. For an MCU with only a few dozen KB of RAM, this "zero-allocation" style of string handling is practically the only viable option.

## Run It Online

Run the string_view examples online and get a feel for zero-copy string operations:

<OnlineCompilerDemo
  title="string_view: Zero-Copy String Splitting and Parsing"
  source-path="code/examples/vol2/12_string_view.cpp"
  description="Run it online and observe the zero-copy behavior of string_view's split and key-value parsing."
  allow-run
/>

## References

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)
- [cppreference: basic_string_view constructors](https://en.cppreference.com/w/cpp/string/basic_string_view/basic_string_view.html)
- [cppreference: data() notes (NUL termination not guaranteed)](https://en.cppreference.com/w/cpp/string/basic_string_view/data.html)
- [cppreference: operator""sv](https://en.cppreference.com/w/cpp/string/basic_string_view/operator%22%22sv.html)
- [cppreference: remove_prefix](https://en.cppreference.com/w/cpp/string/basic_string_view/remove_prefix.html)
