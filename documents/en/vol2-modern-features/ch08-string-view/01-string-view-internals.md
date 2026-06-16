---
chapter: 8
cpp_standard:
- 17
description: Understanding the implementation mechanism of `string_view`, its comparison
  with SSO, and its construction sources
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 0: 右值引用'
reading_time_minutes: 18
related:
- string_view 性能分析
- string_view 陷阱与最佳实践
tags:
- host
- cpp-modern
- intermediate
title: 'Internal Principles of `string_view`: Non-owning String View'
translation:
  source: documents/vol2-modern-features/ch08-string-view/01-string-view-internals.md
  source_hash: a42c6cd426510f24a6086f27cf5b83b7bc11664d0756b35032dd0f07269161e8
  translated_at: '2026-06-16T03:58:50.102597+00:00'
  engine: anthropic
  token_count: 3341
---
# string_view Internals: Non-owning String View

While working on an IniParser project recently, I dealt with strings so much I almost got sick of it—split, trim, substr, operations flying everywhere. Every substring operation using `std::string` meant a heap allocation. After parsing a single configuration file, the heap was more fragmented than my desk. Later, when I seriously studied `std::string_view`, I realized that C++17 gave us such a handy tool. However, using it well requires truly understanding its internal mechanism; otherwise, it is easy to fall into traps regarding lifecycles—we will discuss this in detail in the next article on pitfalls.

In this article, we focus on the internal principles of `std::string_view`: what it looks like, why it is so lightweight, the essential differences from `std::string`, and the operations it provides.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Understand the internal representation of `std::string_view` (pointer + length)
> - [ ] Distinguish between "view" and "ownership" semantics
> - [ ] Master the construction sources and core member functions of `std::string_view`
> - [ ] Understand the essential differences from `const std::string&` parameters

## What exactly is string_view

`std::string_view` (C++17) is a lightweight, immutable "string view" type. The keyword is "view"—it **does not own** the character buffer; it only holds two things: a pointer to the start of the character sequence and the length of that sequence. As you can see, the name is very straightforward: it is just a "view," an observation window, not the owner of the data.

> Reference: [cppreference -- std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)

### Internal Representation: Two fields handle everything

Although the C++ standard does not mandate a specific internal structure, all mainstream implementations (libstdc++, libc++, MSVC STL) use the same scheme—a simple structure of two fields:

```cpp
template <typename CharT>
class basic_string_view {
    const CharT* _data;   // Pointer to the start of the character sequence
    size_t _size;         // Length of the sequence
};
```

Just these two fields: one pointer, one length. Copying a `std::string_view` is just copying these two words—16 bytes on a 64-bit system. No heap allocation, no reference counting, no destructor logic. This is the fundamental reason why it is lightweight.

### Relationship with std::string: View vs. Ownership

The most critical step in understanding `std::string_view` is grasping the difference between "view" and "ownership." `std::string` is an owner: it allocates memory on the heap to store characters, manages the lifecycle of that memory, including construction, copying, moving, and ultimately freeing it. You can think of it as "I bought this house, and my name is on the deed."

`std::string_view`, on the other hand, is an observer: it does not allocate any memory; it just points to someone else's data and says "I'm looking at this." It is like a friend buying a house and you holding the key to visit—you can use the living room and kitchen, but the house isn't yours. If one day the friend sells the house (the underlying `std::string` is destroyed), the key in your hand becomes useless.

The direct benefit of this design is that any "substring operation" does not require allocating new memory. For example, `remove_prefix` just moves the pointer forward and shortens the length, with a complexity of O(1). In contrast, `std::string::substr` needs to allocate new memory and copy characters, with a complexity of O(n). This difference is very significant in scenarios that involve frequent substring operations, such as parsers and protocol handling.

Let's use code to visually compare the behavioral difference between `std::string_view` and `std::string` in substring operations. The implementation of `std::string_view::remove_prefix` is roughly equivalent to:

```cpp
void remove_prefix(size_t n) {
    _data += n;  // Move pointer forward
    _size -= n;  // Decrease length
}
```

It allocates absolutely no new memory, only adjusting the pointer and length. Meanwhile, `std::string::substr` must go through a full allocate-and-copy process. Suppose we need to process a 1MB configuration file and perform `substr` on every field—thousands of calls. Using `std::string` means thousands of heap allocations, while using `std::string_view` means thousands of pointer adjustments. The difference speaks for itself.

Besides `remove_prefix`, query operations like `find`, `compare`, and `starts_with` also directly traverse the memory pointed to by `data()` (relying on `size()`), without involving new memory creation. The design philosophy of `std::string_view` can be summarized in one sentence: it is a lightweight facade that turns any character sequence into an "operable read-only string object," but never takes responsibility for the memory. This is its greatest advantage, and the source of all risks—since it doesn't clean up, someone else has to, and that person is you, the programmer.

### SSO: Small String Optimization

Speaking of `std::string` overhead, we must mention SSO (Small String Optimization). Mainstream `std::string` implementations adopt the SSO strategy: when the string is short enough (usually 15-22 bytes, depending on the implementation), the character data is stored directly in an internal buffer within the object, requiring no heap allocation. Only when the string exceeds this threshold does it switch to heap allocation mode.

SSO is a great optimization—copying short strings becomes cheap. But it doesn't eliminate all overhead. A `std::string` object itself is typically 24-32 bytes in size (implementation-dependent, including SSO buffer, length, capacity, etc.), and its copy semantics mean that even if SSO is triggered, the character data must be copied byte-by-byte. In comparison, `std::string_view` is only 16 bytes (on 64-bit systems), and copying is always just a `memcpy` of two words, regardless of the string length.

This comparison isn't to say `std::string_view` is better than `std::string`—they solve different problems. `std::string` manages ownership; `std::string_view` provides a read-only view. In scenarios where you need to modify a string or hold a copy of the string, `std::string` remains the only choice.

### Essential comparison with const char*

If we zoom out a bit, the design of `std::string_view` is conceptually a wrapper around `const char*`. If `std::string` wraps `char*` (with ownership), then `std::string_view` wraps `const char*` (without ownership, but with added length information). This "added length information" looks like a small change, but it has a huge impact.

Getting the length of a `const char*` requires calling `strlen`, which is an O(n) traversal. Worse, if your function uses the string length multiple times and doesn't actively cache it, you end up calling `strlen` repeatedly, unknowingly turning into an O(n^2) performance pattern. `std::string_view` stores the length directly in the object, so `size()` is O(1)—just a member variable read.

Another often overlooked issue is that `const char*` can only represent strings terminated by a `'\0'`. This means it cannot correctly handle binary data containing null bytes, nor can it represent substrings without modifying the original data (because the end of a substring might not have a `'\0'`). `std::string_view` solves both problems with an explicit length: it can point to any byte sequence (including those with `'\0'` in the middle) and safely represent any sub-range.

| Feature | `std::string_view` | `const char*` |
|---------|-------------------|---------------|
| Contains length? | Has `size()`, O(1) | No, needs `strlen`, O(n) |
| Safe to represent substrings? | Fully supported (has length) | Only by temporarily modifying `'\0'` or passing extra length |
| Supports sequences with null chars? | Yes (length is independent) | No, relies on NUL termination |
| Advanced interfaces (find, compare) | Rich member functions | Almost none, only C functions |
| Literal syntax | `"text"sv` | `"text"` |

The core difference can be summarized in one sentence: `std::string_view` is a "fat pointer" (pointer + length), `const char*` is a "thin pointer" (pointer only). The explicit length of `std::string_view` is a huge advantage, because in many scenarios, NUL termination is not our intent.

## Construction Sources: Where does it come from

Our experimental environment today is: Linux system, GCC 13 or Clang 17 or later, compiler flag `-std=c++17`. All code examples can be compiled and run directly.

`std::string_view` can be constructed from multiple sources. The most common ones are these three:

The first is from C-style string literals. The storage for string literals is static (usually placed in the .rodata section of the executable), so `std::string_view` pointing to it is safe, and the lifetime covers the entire program run:

```cpp
// 1. From string literal
std::string_view sv1 = "Hello, world";
```

The second is from `std::string`. `std::string` provides a conversion operator to `std::string_view`, so you can pass it directly:

```cpp
// 2. From std::string
std::string s = "Hello";
std::string_view sv2 = s; // Implicit conversion
```

⚠️ Here is a classic trap: if `s` is a temporary object, then `sv2` will point to destroyed memory—a dangling reference. For example, `func(std::string_view("tmp"))` is undefined behavior. We will discuss this issue in detail in the pitfalls article.

The third is from a specified range, manually passing a pointer and length:

```cpp
// 3. From pointer and length
char buffer[] = "Data\0WithNull"; // Contains '\0' in the middle
std::string_view sv3(buffer, 14); // Explicitly specify length to include '\0'
```

This method offers the highest flexibility and is the construction method used inside many parsers. You can even point to a segment in the middle of a buffer containing `'\0'`—because `std::string_view` uses length to define boundaries, it doesn't rely on a `'\0'` ending.

C++17 also provides the literal suffix `""sv`, allowing you to write `"text"sv` to get a `std::string_view`. This suffix is defined in the `std::string_view_literals` namespace:

```cpp
using namespace std::string_view_literals;
std::string_view sv4 = "Hello, world"sv; // Literal suffix
```

## Difference from const std::string& parameters

Many tutorials will tell you to "use `std::string_view` instead of `const std::string&` for function parameters." This is mostly correct, but we need to understand the specific differences between the two to make the right choice in the right scenario.

When using `const std::string&` as a parameter, the caller must provide a `std::string` object. If the caller only has a `const char*` or a string literal, the compiler will implicitly construct a temporary `std::string`—involving a possible heap allocation and copy. When using `std::string_view` as a parameter, whether it is `std::string`, `const char*`, or a string literal, `std::string_view` can be constructed directly, at the cost of just copying a pointer and a length.

```cpp
// Old way: const std::string&
void print_string(const std::string& str) {
    std::cout << str << std::endl;
}

// New way: std::string_view
void print_view(std::string_view sv) {
    std::cout << sv << std::endl;
}

int main() {
    const char* cstr = "Hello";

    print_string(cstr); // (1) Constructs a temporary std::string
    print_view(cstr);   // (2) No heap allocation, just pointer + length
}
```

You will find that the `std::string_view` version avoids unnecessary temporary `std::string` construction. In frequently called hot-path functions, this difference accumulates into significant performance gains. However, there is a counter-difference: `const std::string&` guarantees the data is terminated by `'\0'` (because the source must be `std::string`), while `std::string_view` does not. If your function needs to call a C API internally (like `strtol`), then `std::string_view` might actually dig a hole for you.

## Overview of Core Member Functions

Now that we understand the principles, let's look at the operations `std::string_view` provides.

### Element Access

`operator[]` and `at()` are used to access characters by index. `operator[]` performs no bounds checking (in release mode), while `at()` performs bounds checking and throws `std::out_of_range` on overflow. `data()` returns a pointer to the underlying character sequence. `size()` and `length()` return the character count, and `empty()` checks if it is empty.

```cpp
std::string_view sv = "Hello";
char c1 = sv[0];        // 'H', no bounds check
char c2 = sv.at(0);     // 'H', with bounds check
const char* ptr = sv.data(); // Pointer to 'H'
std::cout << sv.size(); // 5
```

⚠️ The return value of `data()` is **not guaranteed** to be terminated by `'\0'`. If `sv` was generated via `substr(pointer, length)` or constructed from a non-null-terminated buffer, the end of the buffer pointed to by `data()` likely lacks a `'\0'`. Passing `data()` directly to a C API requiring NUL termination is a common source of bugs. If you truly need a NUL-terminated string, you must explicitly construct a `std::string`.

### Modifying the View Itself

`std::string_view` provides three operations to modify itself—note that it modifies the "view" itself (i.e., the pointer and length), not the underlying data. These operations are all O(1) because they just adjust two fields:

```cpp
sv.remove_prefix(1); // Remove first character
sv.remove_suffix(1); // Remove last character
```

`remove_prefix` and `remove_suffix` are particularly useful in parsers. For example, if you want to skip a fixed prefix or remove a trailing separator, just call these functions; there is no need to create a new `std::string_view` object.

Let's look at a slightly more complete parsing scenario: extracting key and value from a string in `key=value` format. This is very common in configuration file parsing and HTTP header parsing.

```cpp
#include <iostream>
#include <string_view>

void parse_kv(std::string_view input) {
    size_t pos = input.find('=');
    if (pos != std::string_view::npos) {
        auto key = input.substr(0, pos);
        auto value = input.substr(pos + 1);

        // Simple trim (remove spaces)
        key.remove_prefix(std::min(key.find_first_not_of(" "), key.size()));
        value.remove_suffix(std::min(value.size() - value.find_last_not_of(" ") - 1, value.size()));

        std::cout << "Key: [" << key << "], Value: [" << value << "]" << std::endl;
    }
}

int main() {
    parse_kv("  username  =  admin  ");
    return 0;
}
```

Result:

```text
Key: [username], Value: [admin]
```

Note the key operations here: we use `find` to consume the input string segment by segment, use `substr` to extract fragments without separators, and use `remove_prefix` / `remove_suffix` to trim. The entire process is zero-copy on the original data—`std::string_view` just repeatedly adjusts pointers and lengths. On the hot path of a parser, this pattern can significantly reduce the number of memory allocations.

But again, note: in this example, the input is a `std::string_view` literal whose lifetime covers the entire program. If the input came from a `std::string` local variable, all views would dangle after the function returns. This is what I emphasize repeatedly—understanding lifecycles is the first priority of using `std::string_view`.

## Practice: Write a simple token splitter manually

Having talked about so many principles, let's use a practical example to experience the usage of `std::string_view`. Below is a function that splits a string by a delimiter:

```cpp
#include <iostream>
#include <string_view>
#include <vector>

std::vector<std::string_view> split(std::string_view text, char delim) {
    std::vector<std::string_view> tokens;
    size_t start = 0;
    size_t end = 0;

    while ((end = text.find(delim, start)) != std::string_view::npos) {
        tokens.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    // Add the last segment
    tokens.push_back(text.substr(start));

    return tokens;
}

int main() {
    std::string_view text = "one,two,three";
    auto tokens = split(text, ',');

    for (auto t : tokens) {
        std::cout << "[" << t << "]" << std::endl;
    }
    return 0;
}
```

Result:

```text
[one]
[two]
[three]
```

Pay attention to the logic inside the `split` function: we repeatedly call `find` to advance the starting position of the view, and use `substr` to extract each token. Throughout the process, there is no heap allocation (except for the growth of the `vector` itself), and all operations are O(1) pointer adjustments. If implemented with `std::string`, every `substr` would allocate new memory—for a simple INI file parser, this overhead is completely unnecessary.

⚠️ The returned `std::vector<std::string_view>` points to the internal buffer of the original `text`. If `text` is destroyed, all these views will dangle. In an actual project, you might need to use `std::string` to copy these tokens, or clearly document the lifetime constraints of the return value.

## Embedded Practice: Command Parsing

`std::string_view` is equally useful in embedded scenarios. Many embedded systems need to receive text commands via serial port (such as AT command sets, custom debug commands). Using `std::string_view` to parse these commands can avoid unnecessary string copies, which is especially valuable on MCUs with limited heap memory.

```cpp
#include <iostream>
#include <string_view>
#include <array>

// Simulate receiving data from a serial buffer
std::array<char, 128> uart_rx_buffer;
size_t uart_rx_len = 0;

void process_command(std::string_view cmd) {
    // Remove trailing newline characters
    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) {
        cmd.remove_suffix(1);
    }

    // Find space to separate verb and arguments
    size_t space_pos = cmd.find(' ');
    std::string_view verb = (space_pos != std::string_view::npos)
                             ? cmd.substr(0, space_pos)
                             : cmd;
    std::string_view args = (space_pos != std::string_view::npos)
                             ? cmd.substr(space_pos + 1)
                             : "";

    if (verb == "LED") {
        if (args == "ON") {
            std::cout << "Turning LED ON" << std::endl;
        } else if (args == "OFF") {
            std::cout << "Turning LED OFF" << std::endl;
        }
    } else if (verb == "RESET") {
        std::cout << "System Reset" << std::endl;
    }
}

int main() {
    // Simulate receiving "LED ON\n"
    uart_rx_buffer = {'L', 'E', 'D', ' ', 'O', 'N', '\n'};
    uart_rx_len = 7;

    // Parse directly from buffer, zero copy
    process_command(std::string_view(uart_rx_buffer.data(), uart_rx_len));

    return 0;
}
```

This example demonstrates the typical usage of `std::string_view` in embedded scenarios: receiving a command segment cut from a serial buffer, using `remove_suffix` to strip newlines, splitting verbs and arguments by spaces, and then performing simple string matching. The entire process is zero heap allocation—all operations are pointer and length adjustments. For an MCU with only a few dozen KB of RAM, this "zero-allocation" string processing method is almost the only viable choice.

## Run Online

Run the string_view example online to experience zero-copy string operations:

<OnlineCompilerDemo
  title="string_view: Zero-copy String Splitting and Parsing"
  source-path="code/examples/vol2/12_string_view.cpp"
  description="Run online and observe the zero-copy characteristics of string_view split and key-value parsing."
  allow-run
/>

## Summary

The essence of `std::string_view` is a "pointer + length" non-owning view. It allocates no memory, has a very low copy cost (16 bytes), and substring operations are all O(1). It can be constructed from `std::string`, `const char*`, literals, and other sources, making it an ideal choice for function parameters. However, it does not guarantee NUL termination and does not manage data lifecycles—these "irresponsible" aspects are exactly what we need to be extra careful about when using it.

Once you understand these internal principles, in the next article we will look at the actual performance benefits of `std::string_view` using benchmark data.

## References

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)
- [cppreference: basic_string_view constructor](https://en.cppreference.com/w/cpp/string/basic_string_view/basic_string_view.html)
- [cppreference: data() description (no NUL guarantee)](https://en.cppreference.com/w/cpp/string/basic_string_view/data.html)
- [cppreference: operator""sv](https://en.cppreference.com/w/cpp/string/basic_string_view/operator%22%22sv.html)
- [cppreference: remove_prefix](https://en.cppreference.com/w/cpp/string/basic_string_view/remove_prefix.html)
