---
chapter: 8
cpp_standard:
- 17
description: Understand how string_view works internally, how it compares against
  SSO, and where it can be constructed from
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
  source_hash: 2ce8ca1266c99e3ba559560771888bd4e747519c82f75dbe7dabd55cdaecb608
  translated_at: '2026-09-25T16:02:18+00:00'
  engine: anthropic
  token_count: 4800
---
# string_view Internals: A Non-Owning String View

Recently I was writing an IniParser project and got thoroughly sick of dealing with strings — split, trim, substr, operations flying everywhere. Every substring operation on a `std::string` means a heap allocation, and after parsing a single config file, the heap ended up more fragmented than my desk. Later I took a serious look at `std::string_view` and discovered that C++17 had this wonderfully handy tool ready for us. The precondition for using it well, though, is genuinely understanding its internal mechanics — otherwise it is easy to step right into a lifetime pit, which we will save for the pitfalls article coming next.

This article focuses on the internals of `string_view`: what it actually looks like, why it is so lightweight, where its essential difference from `std::string` lies, and which operations it offers.

## What Exactly Is string_view

`std::string_view` (C++17) is a lightweight, immutable "string view" type. The keyword is "view" — it does **not own** the character buffer; it stores only two things: a pointer to the start of the character sequence, and the length of that sequence. So as you can see, the name is completely literal: it is a view, an observation window, not an owner of the data.

> Reference: [cppreference -- std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)

### Internal Representation: Two Fields Do Everything

Although the C++ standard does not mandate a concrete internal layout, every mainstream implementation (libstdc++, libc++, MSVC STL) uses the same scheme — a simple two-field structure:

```cpp
template<class CharT, class Traits = std::char_traits<CharT>>
class basic_string_view {
    const CharT* _ptr;   // points to the underlying character sequence (not owned)
    size_t       _len;   // length (excluding '\0')
};
```

Just these two fields — one pointer, one length. Copying a `string_view` is just copying these two words — 16 bytes on a 64-bit system. No heap allocation, no reference counting, no destructor logic. That is the root cause of how lightweight it is.

### The Relationship with std::string: View vs. Ownership

The most crucial step in understanding `string_view` is getting clear on the difference between "viewing" and "owning". `std::string` is an owner: it allocates memory on the heap to store the characters and manages that memory's lifetime — construction, copying, moving, and the final release. You can liken it to "I bought this apartment, and the property deed has my name on it."

`string_view`, by contrast, is an observer: it allocates no memory; it just points at someone else's data and says "let me look at this." It is like a friend bought a house and you hold a key to drop by — you can use the living room and the kitchen, but the house is not yours, and the day your friend sells it (the underlying `string` is destroyed), the key in your hand turns worthless.

The immediate benefit of this design is that no "substring operation" ever needs to allocate new memory. `substr`, for example, just slides the pointer forward and shortens the length, at O(1) complexity. `std::string::substr` must allocate new memory and copy the characters, at O(n) complexity. In scenarios with frequent substring operations (parsers, protocol handling), this difference becomes very pronounced.

Let's compare in code how `string_view` and `std::string` behave differently for substring operations. `string_view::substr` is roughly equivalent to:

```cpp
string_view substr(size_t pos, size_t count) const {
    return string_view(_ptr + pos, min(count, _len - pos));
}
```

The window-narrowing and shifting over the same block of memory has been made into an animation — you can play it, pause it, or use the step button to advance frame by frame, and see every adjustment of the pointer and the length clearly:

<Anim id="string-view-window" />

No new memory is allocated at all — only the pointer and the length are adjusted. `std::string::substr` must go through the full allocate-and-copy routine. Suppose we are processing a 1MB config file and calling `substr` on every field — possibly thousands of calls. With `std::string` that is thousands of heap allocations; with `string_view` it is thousands of pointer adjustments. The gap speaks for itself.

Beyond `substr`, query operations such as `find`, `compare`, and `rfind` also walk the memory pointed to by `_ptr` directly (relying on `Traits::compare`), never creating new memory. `string_view`'s design philosophy can be summarized in one sentence: it is a lightweight facade that turns any character sequence into an "operable read-only string object", while never being responsible for memory. That is both its greatest strength and the root of all its risks — after all, if it is not responsible for cleanup, someone has to be, and that someone is you, the programmer.

### SSO: Small String Optimization

Speaking of `std::string`'s overhead, we have to mention SSO (Small String Optimization). Mainstream `std::string` implementations adopt the SSO strategy: when a string is short enough (typically 15-22 bytes, depending on the implementation), the character data is stored directly in a buffer inside the object itself, with no heap allocation. Only when the string exceeds that threshold does it switch to heap allocation mode.

SSO is a great optimization — copying short strings becomes cheap. But it does not eliminate all overhead. A `std::string` object itself is typically 24-32 bytes in size (implementation-dependent, holding the SSO buffer, the length, the capacity, and so on), and its copy semantics mean that even when SSO kicks in, the character data still has to be copied over byte by byte. By contrast, a `string_view` is only 16 bytes (on 64-bit systems), and a copy is always a two-word memcpy, no matter how long the string is.

This comparison is not saying `string_view` is better than `std::string` — they solve different problems. `std::string` manages ownership; `string_view` provides a read-only view. When you need to modify the string or hold a copy of it, `std::string` remains the only choice.

### The Fundamental Comparison with const char*

If we pull the camera back a bit further, `string_view`'s design is conceptually a wrapper around `const char*`. If `std::string` wraps `char[]` (with ownership), then `string_view` wraps `const char*` (without ownership, but with added length information). That "added length information" looks like a small change, but its practical impact is huge.

Getting the length of a `const char*` requires calling `strlen` — an O(n) walk. Worse, if your function uses the string length multiple times and does not cache it deliberately, you will call `strlen` over and over, and before you know it your performance profile has quietly become O(n^2). `string_view` stores the length right inside the object, so `size()` is O(1) — just a read of a member variable.

Another often-overlooked problem: `const char*` can only represent strings terminated with `\0`. That means it cannot correctly handle binary data containing zero bytes, and it cannot represent a substring without modifying the original data (because there is not necessarily a `\0` at the end of that substring). `string_view` solves both problems with an explicit length: it can point at any byte sequence (including ones with `\0` in the middle), and it can safely represent any sub-range.

| Feature | `std::string_view` | `const char*` |
|------|---------------------|---------------|
| Carries a length | Yes — `size()`, O(1) | No — needs `strlen`, O(n) |
| Safe substring representation | Fully supported (length is known) | Only by temporarily modifying `\0` or passing an extra length |
| Sequences containing zero bytes | Yes (length is independent) | No — relies on NUL termination |
| Rich interface (find, compare) | A rich set of member functions | Almost none — C functions only |
| Literal syntax | `"abc"sv` | `"abc"` |

The core difference in one sentence: `string_view = (pointer, length)`, while `const char* = pointer + an implicit '\0' terminator`. `string_view`'s explicit length is an enormous advantage, because in many scenarios `\0` is not part of our intent.

## Construction Sources: Where Does It Come From

Our lab environment today is as follows: a Linux system, GCC 13 or Clang 17 or newer, compiled with `-std=c++17 -O2`. All code examples can be compiled and run directly.

A `string_view` can be constructed from a variety of sources. The three most common ones are:

The first is construction from a C-style string literal. String literals have static storage (usually placed in the executable's .rodata section), so a `string_view` pointing at one is safe, with a lifetime covering the entire program run:

```cpp
std::string_view sv = "hello, world";
// sv points to a string literal in static storage — valid forever
```

The second is construction from a `std::string`. `std::string` provides an implicit conversion operator to `string_view`, so you can pass it directly:

```cpp
std::string str = "hello";
std::string_view sv = str;  // implicit conversion
// sv points to str's internal buffer — safe as long as str is alive
```

Here lies a classic trap: if `str` is a temporary, `sv` ends up pointing at destroyed memory — a dangling reference. For example, `std::string_view sv = std::string("temp");` is undefined behavior. We will discuss this in detail in the pitfalls article.

The third is construction from a specified range — you pass in the pointer and the length by hand:

```cpp
const char* buf = "hello, world";
std::string_view sv(buf, 5);  // only the first 5 characters: "hello"
```

This is the most flexible form, and it is the construction method many parsers use internally. You can even point at some middle slice of a buffer that contains `\0` — because `string_view` delimits itself by length, not by a `\0` terminator.

C++17 also provides the literal suffix `sv`, so you can write `"hello"sv` to get a `std::string_view`. This suffix is defined in the `std::literals::string_view_literals` namespace:

```cpp
using namespace std::literals::string_view_literals;
auto sv = "hello"sv;  // std::string_view
```

## The Difference from const std::string& Parameters

Many tutorials will tell you to "replace `const std::string&` function parameters with `string_view`". That advice is broadly correct, but we need to understand the concrete differences between the two to make the right choice in the right scenario.

With a `const std::string&` parameter, the caller must provide a `std::string` object. If the caller has only a `const char*` or a string literal at hand, the compiler implicitly constructs a temporary `std::string` — which involves a possible heap allocation plus a copy. With a `string_view` parameter, a `std::string`, a `const char*`, or a string literal can all construct the `string_view` directly, at the cost of copying one pointer and one length.

```cpp
// Approach 1: const string& parameter
void process_old(const std::string& s);

process_old(std::string("temp"));  // construct string → pass reference
process_old("literal");            // implicitly construct temporary string → pass reference → temporary destroyed
process_old(some_c_string);        // implicitly construct temporary string → strlen + possible allocation

// Approach 2: string_view parameter
void process_new(std::string_view sv);

process_new(std::string("temp"));  // implicitly construct view from string → no extra allocation
process_new("literal");            // construct view directly → zero allocation
process_new(some_c_string);        // construct view directly → requires strlen (O(n)), but no heap allocation
```

As you can see, the `string_view` version avoids constructing unnecessary temporary `std::string`s. In hot-path functions called frequently, this difference accumulates into a sizable performance gain. There is one difference running the other way, though: `const std::string&` guarantees the data ends with `\0` (because the source is always a `std::string`), while `string_view` makes no such guarantee. If your function internally calls a C API (say `printf("%s", ...)`), `string_view` may actually dig a pit for you.

## Core Member Functions at a Glance

Now that we understand the internals, let's see what operations `string_view` provides.

### Element Access

`operator[]` and `at()` access characters by index. `operator[]` performs no bounds checking (in release mode), while `at()` does check and throws `std::out_of_range` when out of bounds. `data()` returns a pointer to the underlying character sequence. `size()` and `length()` return the character count, and `empty()` reports whether the view is empty.

```cpp
std::string_view sv = "hello";

char c = sv[1];         // 'e', no bounds check
char d = sv.at(1);      // 'e', bounds-checked
const char* p = sv.data();  // pointer to 'h'
std::size_t n = sv.size();  // 5
bool e = sv.empty();        // false
```

The value returned by `data()` is **not** guaranteed to end with `\0`. If the `string_view` was produced by `substr` or `remove_suffix`, the end of the buffer `data()` points to very likely has no `\0`. Passing `data()` straight into a C API that requires NUL termination is a common bug source. If you truly need a NUL-terminated string, you must explicitly construct `std::string(sv)`.

### Modifying the View Itself

`string_view` provides three operations that modify itself — note, what gets modified is the "view" itself (that is, the pointer and the length), not the underlying data. All of them are O(1), because they merely adjust two fields:

```cpp
std::string_view sv = "hello, world";

// remove_prefix: advance the view's starting position by n characters
sv.remove_prefix(7);   // sv becomes "world"

// remove_suffix: pull the view's end back by n characters
std::string_view sv2 = "hello, world";
sv2.remove_suffix(7);  // sv2 becomes "hello"

// swap: exchange the contents of two string_views
std::string_view a = "first";
std::string_view b = "second";
a.swap(b);  // a -> "second", b -> "first"
```

`remove_prefix` and `remove_suffix` are especially useful in parsers. For example, to skip a fixed prefix or strip a trailing delimiter, you just call these two functions — no need to create a new `string_view` object.

Let's look at a slightly more complete parsing scenario: extracting the key and the value from a `"key=value"` format string. This is extremely common in config-file parsing and HTTP header parsing.

```cpp
#include <string_view>
#include <iostream>
#include <optional>
#include <utility>

/// @brief Extract a key-value pair from a "key=value" format string
/// @param entry the input string view, e.g. "host=localhost"
/// @return the (key, value) pair on success, std::nullopt on failure
std::optional<std::pair<std::string_view, std::string_view>>
parse_kv(std::string_view entry) {
    auto pos = entry.find('=');
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    auto key = entry.substr(0, pos);
    auto value = entry.substr(pos + 1);
    // trim leading and trailing whitespace
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
    // split on ';' manually, parsing key-value pairs one by one
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

Output:

```text
key=[host] value=[localhost]
key=[port] value=[8080]
```

Note the key operations here: we use `remove_prefix` to consume the input string segment by segment, `substr` to extract the delimiter-free slices, and `remove_prefix` / `remove_suffix` to do the trimming. The whole process makes zero copies of the original data — `string_view` just keeps adjusting the pointer and the length. On a parser's hot path, this pattern can noticeably reduce the number of memory allocations.

But pay attention to this as well: in this example `raw` is a `const char*` literal whose lifetime covers the entire program. If `raw` came from a local `std::string`, then after the function returns, every `string_view` would dangle. This is exactly what I keep stressing — understanding lifetimes is the number-one rule of using `string_view`.

## Hands-On: A Simple Token Splitter

That was a lot of theory, so let's get a feel for `string_view` usage with a practical example. Below is a function that splits a string on a delimiter:

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

Output:

```text
[name=Alice]
[age=30]
[city=Beijing]
```

Look at the logic inside the `split` function: we repeatedly call `remove_prefix` to advance the view's starting position, and use `substr` to carve out each token. There are no heap allocations at all in the process (aside from the `vector`'s own growth) — every operation is an O(1) pointer adjustment. Implementing this with `std::string` would allocate new memory on every `substr` — for a simple INI file parser, that overhead is entirely unnecessary.

The returned vector of `string_view`s points into the original `line`'s internal buffer. If `line` is destroyed, all those `string_view`s dangle. In a real project, you might need to copy these tokens into `std::string`s, or clearly document the lifetime constraints on the return value.

## Embedded in Practice: Command Parsing

`string_view` is just as useful in embedded scenarios. Many embedded systems receive text commands over a serial port (AT command sets, custom debug commands, and so on), and parsing those commands with `string_view` avoids unnecessary string copies — which is especially valuable on MCUs with constrained heap memory.

```cpp
#include <string_view>
#include <cstring>

/// @brief A simple serial-port command parser
/// @param cmd the input command view, e.g. "LED ON" or "PWM 128"
void handle_command(std::string_view cmd) {
    // strip trailing newline characters
    while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == '\n')) {
        cmd.remove_suffix(1);
    }

    // split the command and its argument on a space
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
        // convert the string_view to an integer
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

This example shows the typical embedded use of `string_view`: receive a command slice carved out of the serial buffer, strip the newline with `remove_suffix`, split the verb and the argument on a space, then do simple string matching. Zero heap allocations throughout — every operation is just an adjustment of pointer and length. For an MCU with only a few dozen KB of RAM, this "zero-allocation" style of string handling is practically the only viable option.

## Run It Online

Run the string_view examples online and experience zero-copy string operations:

<OnlineCompilerDemo
  title="string_view: Zero-Copy String Splitting and Parsing"
  source-path="code/examples/vol2/12_string_view.cpp"
  description="Run it online and observe the zero-copy behavior of string_view's split and key-value parsing."
  allow-run
/>

## References

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)
- [cppreference: basic_string_view constructors](https://en.cppreference.com/w/cpp/string/basic_string_view/basic_string_view.html)
- [cppreference: data() notes (NUL not guaranteed)](https://en.cppreference.com/w/cpp/string/basic_string_view/data.html)
- [cppreference: operator""sv](https://en.cppreference.com/w/cpp/string/basic_string_view/operator%22%22sv.html)
- [cppreference: remove_prefix](https://en.cppreference.com/w/cpp/string/basic_string_view/remove_prefix.html)
