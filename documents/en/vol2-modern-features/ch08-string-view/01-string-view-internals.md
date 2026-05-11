# string_view Internals: A Non-Owning String View

While working on an IniParser project recently, I dealt with strings so much I nearly got sick of it—split, trim, substr, operations flying everywhere. Every substring operation using `std::string` meant a heap allocation, and after parsing a single config file, the heap fragmentation was messier than my desk. Later, I dug into `std::string_view` and realized that C++17 gave us such a handy tool. But using it well requires truly understanding its internal mechanisms—otherwise, it is easy to fall into lifetime traps, which we will save for the next article on pitfalls.

This article focuses on the internals of `std::string_view`: what it actually looks like, why it is so lightweight, what the fundamental difference is from `std::string`, and what operations it provides.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Understand the internal representation of `std::string_view` (pointer + length)
> - [ ] Distinguish between "view" and "owning" semantics
> - [ ] Master the construction sources and core member functions of `std::string_view`
> - [ ] Understand the essential differences from `const std::string&` parameters

## What Exactly Is string_view

`std::string_view` (C++17) is a lightweight, immutable "string view" type. The keyword here is "view"—it **does not own** the character buffer; it only holds two things: a pointer to the start of the character sequence, and the length of that sequence. So you see, the name is very straightforward: it is a "view," an observation window, not the owner of the data.

> Reference: [cppreference -- std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)

### Internal Representation: Two Fields Handle Everything

Although the C++ standard does not mandate a specific internal structure, all mainstream implementations (libstdc++, libc++, MSVC STL) use the same approach—a simple structure with two fields:

```cpp
template<class CharT, class Traits = std::char_traits<CharT>>
class basic_string_view {
    const CharT* _ptr;   // 指向底层字符序列（不拥有）
    size_t       _len;   // 长度（不含 '\0'）
};
```

Just these two fields: one pointer, one length. Copying a `std::string_view` simply copies these two words—16 bytes on a 64-bit system. No heap allocation, no reference counting, no destruction logic. This is the fundamental reason it is lightweight.

### Relationship with std::string: View vs. Ownership

The most crucial step in understanding `std::string_view` is grasping the difference between "view" and "ownership." `std::string` is an owner: it allocates memory on the heap to store characters, manages the lifetime of that memory, including construction, copying, moving, and eventual deallocation. You can think of it as "I bought this house, and my name is on the deed."

`std::string_view`, on the other hand, is an observer: it does not allocate any memory; it simply points to someone else's data and says "I'll take a look at this." It is like a friend buying a house and you visiting with a key—you can use the living room and kitchen, but the house is not yours. If your friend sells the house one day (the underlying `std::string` is destroyed), the key in your hand becomes useless.

The direct benefit of this design is that any "substring operation" requires no new memory. For example, `substr` simply advances the pointer and shortens the length, with O(1) complexity. In contrast, `std::string::substr` needs to allocate new memory and copy characters, resulting in O(n) complexity. This difference becomes very noticeable in scenarios with frequent substring operations, such as parsers and protocol handlers.

Let us compare the behavioral differences between `std::string_view::substr` and `std::string::substr` in substring operations with some code. The implementation of `std::string_view::substr` is roughly equivalent to:

```cpp
string_view substr(size_t pos, size_t count) const {
    return string_view(_ptr + pos, min(count, _len - pos));
}
```

Absolutely no new memory is allocated; only the pointer and length are adjusted. `std::string::substr`, however, must go through a full allocate-and-copy process. Suppose we need to process a 1MB config file and perform `substr` on each field—there could be thousands of calls. Using `std::string` means thousands of heap allocations, while using `std::string_view` means thousands of pointer adjustments. The difference speaks for itself.

Besides `substr`, query operations like `find`, `rfind`, `compare`, and `starts_with` also directly traverse the memory pointed to by the `data` pointer (relying on `traits_type::compare`), without creating new memory. The design philosophy of `std::string_view` can be summed up in one sentence: it is a lightweight facade that turns any character sequence into an "operable read-only string object," but it never takes responsibility for memory. This is both its greatest advantage and the root of all risks—after all, if it does not clean up, someone else must, and that someone is you, the programmer.

### SSO: Small String Optimization

When discussing the overhead of `std::string`, we have to mention SSO (Small String Optimization). Mainstream `std::string` implementations all use the SSO strategy: when a string is short enough (typically 15–22 bytes, depending on the implementation), the character data is stored directly in an internal buffer within the object, requiring no heap allocation. Only when the string exceeds this threshold does it switch to heap-allocated mode.

SSO is a great optimization—copying short strings becomes very cheap. But it does not eliminate all overhead. A `std::string` object itself is usually 24–32 bytes in size (implementation-dependent, including the SSO buffer, length, capacity, etc.), and its copy semantics mean that even when SSO is triggered, the character data must be copied byte by byte. In contrast, `std::string_view` is only 16 bytes (on 64-bit systems), and copying is always a two-word `memcpy`, regardless of string length.

This comparison is not to say that `std::string_view` is better than `std::string`—they solve different problems. `std::string` manages ownership, while `std::string_view` provides a read-only view. In scenarios where you need to modify a string or hold a copy of it, `std::string` remains the only choice.

### Essential Comparison with const char*

If we zoom out a bit further, the design of `std::string_view` is conceptually a wrapper around `const char*`. If `std::string` wraps `const char*` (with ownership), then `std::string_view` wraps `const char*` (without ownership, but with added length information). This "added length information" might seem like a small change, but its practical impact is enormous.

Getting the length of a `const char*` requires calling `strlen`, which is an O(n) traversal. Worse, if your function uses the string length multiple times internally without actively caching it, you end up calling `strlen` repeatedly, unknowingly slipping into an O(n^2) performance pattern. `std::string_view`, on the other hand, stores the length directly in the object, making `size()` O(1)—it is simply reading a member variable.

Another often-overlooked issue is that `const char*` can only represent NUL-terminated strings. This means it cannot correctly handle binary data containing zero bytes, nor can it represent a substring without modifying the original data (because the end of the substring is not guaranteed to have a NUL terminator). `std::string_view` solves both problems with an explicit length: it can point to arbitrary byte sequences (including those with NUL bytes in the middle) and can safely represent any sub-range.

| Feature | `std::string_view` | `const char*` |
|------|---------------------|---------------|
| Includes length | Has `size()`, O(1) | No, requires `strlen`, O(n) |
| Safe to represent substrings | Fully supported (has length) | Only by temporarily modifying the NUL terminator or passing an extra length |
| Supports sequences with embedded NULs | Yes (length is independent) | No, relies on NUL termination |
| Advanced interfaces (find, compare) | Rich set of member functions | Almost none, limited to C functions |
| Literal syntax | `"abc"sv` | `"abc"` |

The core difference can be summarized in one sentence: `string_view = (指针, 长度)`, `const char* = 指针 + 隐含以 '\0' 终止`. The explicit length of `std::string_view` is a huge advantage, because in many scenarios, NUL termination is not our intent.

## Construction Sources: Where It Comes From

Our test environment for today is as follows: Linux system, GCC 13 or Clang 17 and above, with the compiler flag ``-std=c++17 -O2``. All code examples can be compiled and run directly.

`std::string_view` can be constructed from multiple sources. The three most common are:

`std::string_view` can be constructed from multiple sources. The three most common are:

The first is construction from a C-style string literal. String literals are stored statically (usually in the `.rodata` section of the executable), so it is safe for `std::string_view` to point to them—their lifetime covers the entire program's execution:

```cpp
std::string_view sv = "hello, world";
// sv 指向静态存储区的字符串字面量，永远有效
```

The second is construction from `std::string`. `std::string` provides an implicit conversion operator to `std::string_view`, so you can pass it directly:

```cpp
std::string str = "hello";
std::string_view sv = str;  // 隐式转换
// sv 指向 str 的内部缓冲区，只要 str 还活着就安全
```

⚠️ There is a classic trap here: if the `std::string` is a temporary object, the `std::string_view` will point to destroyed memory—a dangling reference. For example, ``std::string_view sv = std::string("temp");`` is undefined behavior. We will discuss this problem in detail in the pitfalls article.

The third is construction from a specified range, manually passing in a pointer and length:

```cpp
const char* buf = "hello, world";
std::string_view sv(buf, 5);  // 只看前 5 个字符："hello"
```

This approach offers the highest flexibility and is the construction method used internally by many parsers. You can even point to a segment in the middle of a buffer containing NUL bytes—because `std::string_view` uses length to define boundaries, it does not rely on NUL termination.

C++17 also provides the literal suffix ``sv``, allowing you to write ``"hello"sv`` directly to get a `std::string_view`. This suffix is defined in the ``std::literals::string_view_literals`` namespace:

```cpp
using namespace std::literals::string_view_literals;
auto sv = "hello"sv;  // std::string_view
```

## Differences from const std::string& Parameters

Many tutorials will tell you to "use `std::string_view` instead of `const std::string&` for function parameters." This is generally correct, but we need to understand the specific differences between the two to make the right choice in the right scenario.

When using `const std::string&` as a parameter, the caller must provide a `std::string` object. If the caller only has a `const char*` or a string literal, the compiler will implicitly construct a temporary `std::string`—which involves a potential heap allocation and copy. When using `std::string_view` as a parameter, whether the source is `std::string`, `const char*`, or a string literal, a `std::string_view` can be constructed directly, at the cost of only copying a pointer and a length.

```cpp
// 方式一：const string& 参数
void process_old(const std::string& s);

process_old(std::string("temp"));  // 构造 string → 传引用
process_old("literal");            // 隐式构造临时 string → 传引用 → 临时对象析构
process_old(some_c_string);        // 隐式构造临时 string → strlen + 可能的分配

// 方式二：string_view 参数
void process_new(std::string_view sv);

process_new(std::string("temp"));  // 从 string 隐式构造 view → 无额外分配
process_new("literal");            // 直接构造 view → 零分配
process_new(some_c_string);        // 直接构造 view → 需要 strlen (O(n))，但不分配堆内存
```

You will notice that the `std::string_view` version avoids unnecessary temporary `std::string` construction. In frequently called hot-path functions, this difference accumulates into noticeable performance gains. However, there is a counter-difference: `const std::string&` guarantees that the data is NUL-terminated (because the source must be a `std::string`), while `std::string_view` does not. If your function internally needs to call a C API (like ``printf("%s", ...)``), using `std::string_view` might actually dig a pit for you.

## Core Member Functions Overview

Now that we understand the principles, let us look at what operations `std::string_view` provides.

### Element Access

``operator[]`` and ``at()`` are used to access characters by index. ``operator[]`` performs no bounds checking (in release mode), while ``at()`` performs bounds checking and throws ``std::out_of_range`` on out-of-bounds access. ``data()`` returns a pointer to the underlying character sequence. ``size()`` and ``length()`` return the character count, and ``empty()`` checks whether it is empty.

```cpp
std::string_view sv = "hello";

char c = sv[1];         // 'e'，无边界检查
char d = sv.at(1);      // 'e'，有边界检查
const char* p = sv.data();  // 指向 'h' 的指针
std::size_t n = sv.size();  // 5
bool e = sv.empty();        // false
```

⚠️ The return value of ``data()`` is **not guaranteed** to be NUL-terminated. If the `std::string_view` was generated via `substr` or by specifying a pointer and length, the end of the buffer pointed to by ``data()`` likely has no NUL terminator. Passing ``data()`` directly to a C API that requires NUL termination is a common source of bugs. If you truly need a NUL-terminated string, you must explicitly construct a `std::string`.

### Modifying the View Itself

`std::string_view` provides three operations that modify itself—note that what is modified is the "view" itself (i.e., the pointer and length), not the underlying data. These operations are all O(1) because they simply adjust two fields:

```cpp
std::string_view sv = "hello, world";

// remove_prefix：把视图的起始位置向后移动 n 个字符
sv.remove_prefix(7);   // sv 变成 "world"

// remove_suffix：把视图的末尾向前缩短 n 个字符
std::string_view sv2 = "hello, world";
sv2.remove_suffix(7);  // sv2 变成 "hello"

// swap：交换两个 string_view 的内容
std::string_view a = "first";
std::string_view b = "second";
a.swap(b);  // a -> "second", b -> "first"
```

``remove_prefix`` and ``remove_suffix`` are particularly useful in parsers. For example, if you need to skip a fixed prefix or remove a trailing delimiter, you can simply call these two functions without creating a new `std::string_view` object.

Let us look at a slightly more complete parsing scenario: extracting a key and value from a string in ``"key=value"`` format. This is very common in config file parsing and HTTP header parsing.

```cpp
#include <string_view>
#include <iostream>
#include <optional>
#include <utility>

/// @brief 从 "key=value" 格式的字符串中提取键值对
/// @param entry 输入字符串视图，如 "host=localhost"
/// @return 成功返回 (key, value) pair，失败返回 std::nullopt
std::optional<std::pair<std::string_view, std::string_view>>
parse_kv(std::string_view entry) {
    auto pos = entry.find('=');
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    auto key = entry.substr(0, pos);
    auto value = entry.substr(pos + 1);
    // 去掉前后空白
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
    // 手动按 ';' 分割，逐个解析键值对
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

Note the key operations here: we use ``remove_prefix`` to consume the input string segment by segment, use ``substr`` to extract fragments that do not contain the delimiter, and use ``remove_prefix`` / ``remove_suffix`` for trimming. The entire process is zero-copy on the original data—`std::string_view` simply repeatedly adjusts the pointer and length. On a parser's hot path, this pattern can significantly reduce the number of memory allocations.

But again, note that in this example, ``raw`` is a ``const char*`` literal whose lifetime covers the entire program. If ``raw`` came from a local `std::string` variable, all `std::string_view`s would dangle after the function returned. This is what I keep emphasizing—understanding lifetimes is the cardinal rule of using `std::string_view`.

## Hands-On: Writing a Simple Token Splitter

After discussing all these principles, let us use a practical example to experience how `std::string_view` is used. Below is a function that splits a string by a delimiter:

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
        input.remove_prefix(pos + 1);  // 跳过分隔符
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

Notice the logic inside the ``split`` function: we repeatedly call ``remove_prefix`` to advance the starting position of the view, and use ``substr`` to extract each token. Throughout this process, there is no heap allocation (aside from the growth of ``vector`` itself), and all operations are O(1) pointer adjustments. If implemented with `std::string`, each ``substr`` would allocate new memory—for a simple INI file parser, this overhead is completely unnecessary.

⚠️ The returned `std::string_view` vector points to the internal buffer of the original ``line``. If ``line`` is destroyed, all these `std::string_view`s become dangling. In a real project, you might need to use `std::string` to copy these tokens, or clearly document the lifetime constraints of the return values in your documentation.

## Embedded in Practice: Command Parsing

`std::string_view` is equally useful in embedded scenarios. Many embedded systems need to receive text commands via a serial port (such as the AT command set or custom debug commands). Using `std::string_view` to parse these commands avoids unnecessary string copies, which is especially valuable on MCUs with limited heap memory.

```cpp
#include <string_view>
#include <cstring>

/// @brief 简单的串口命令解析器
/// @param cmd 输入命令视图，如 "LED ON" 或 "PWM 128"
void handle_command(std::string_view cmd) {
    // 去掉末尾的换行符
    while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == '\n')) {
        cmd.remove_suffix(1);
    }

    // 按空格分割命令和参数
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
        // 将 string_view 转为整数
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

This example demonstrates the typical usage of `std::string_view` in embedded scenarios: receiving a command fragment sliced from a serial buffer, using ``remove_suffix`` to strip newline characters, splitting the verb and arguments by spaces, and then performing simple string matching. The entire process is zero heap allocation—all operations are pointer and length adjustments. For an MCU with only a few dozen KB of RAM, this "zero-allocation" string processing approach is often the only viable option.

## Summary

The essence of `std::string_view` is a non-owning view of "pointer + length." It does not allocate memory, has extremely low copy cost (16 bytes), and all substring operations are O(1). It can be constructed from `std::string`, `const char*`, literals, and other sources, making it an ideal choice for function parameters. But it does not guarantee NUL termination and does not manage data lifetimes—these "non-responsibilities" are exactly what we need to be extra careful about when using it.

With these internals understood, the next article will look at the actual performance benefits of `std::string_view`, letting benchmark data do the talking.

## References

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)
- [cppreference: basic_string_view constructor](https://en.cppreference.com/w/cpp/string/basic_string_view/basic_string_view.html)
- [cppreference: data() description (no NUL guarantee)](https://en.cppreference.com/w/cpp/string/basic_string_view/data.html)
- [cppreference: operator""sv](https://en.cppreference.com/w/cpp/string/basic_string_view/operator%22%22sv.html)
- [cppreference: remove_prefix](https://en.cppreference.com/w/cpp/string/basic_string_view/remove_prefix.html)
