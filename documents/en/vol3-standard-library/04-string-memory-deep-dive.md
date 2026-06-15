---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 23
description: A deep dive into the history and entanglement of `std::string`'s SSO
  and COW, why C++11 forbids COW, SSO threshold implementation details, and buffer
  reuse in C++23's `resize_and_overwrite`.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 卷一：std::string 基础用法
reading_time_minutes: 9
tags:
- host
- cpp-modern
- intermediate
- 内存管理
title: 'Deep Dive into std::string: SSO, COW, and resize_and_overwrite'
translation:
  source: documents/vol3-standard-library/04-string-memory-deep-dive.md
  source_hash: 8887bdd7d4e968834210afa3b7772627cb52ccefe9b91e5f1add4ac46722cc51
  translated_at: '2026-06-14T00:19:45.074884+00:00'
  engine: anthropic
  token_count: 1659
---
# Deep Dive into string: SSO, COW, and resize_and_overwrite

`std::string` is likely the most heavily used type in the standard library, yet it is often the least understood. We happily write `std::string` code all day long, but when pressed with questions like—"Why is `sizeof(std::string)` 32 on my machine?", "Why do two strings in old code share the same buffer?", or "What exactly does C++23's `resize_and_overwrite` save?"—most of us are stumped. The root of these issues lies in the memory model and history of `std::string`.

In this article, we will focus on the memory and buffer story of `std::string`: the historical entanglement of SSO and COW, implementation thresholds for SSO, and the buffer reuse API `resize_and_overwrite` introduced in C++23. (C++20's `std::u8string` is a separate topic; see Volume 3 [char8_t and UTF-8 Strings](./30-char8-t-utf8.md).)

------

## SSO and COW: An ABI History

To understand why `std::string` looks the way it does today, we need to turn the clock back to C++03. Back then, there was a particularly attractive implementation approach—**Copy-On-Write (COW)**: when you copied a `std::string`, it didn't actually copy the characters. Instead, it let the source and destination share a single read-only buffer, maintaining only an extra reference count. Only when one side needed to write would it perform a deep copy. In scenarios with heavy copying of read-only strings, this saved significant memory and time, and early libstdc++ (GCC's C++ Standard Library) was a staunch proponent of COW.

```cpp
// COW era: Copying is cheap (just a pointer and ref count increment)
std::string s1 = "Hello, World!";
std::string s2 = s1; // No character copy happens here
```

However, the C++11 standard effectively ruled COW "illegal." Proposal **N2668**, "Concurrency Modifications to Basic String," rewrote the invalidation rules for `std::string` and the semantics of `data()`/`c_str()`. The text stated unequivocally: *"This change effectively disallows copy-on-write implementations."* What was the legal root cause? I must remind you: many assume it's "thread safety" or "reference counting," but those are merely side issues that amplified the conflict. The real criteria are these three rules combined:

- **Invalidation Rules**: The standard specifies that calling element access methods like `at()`, `front()`, `back()`, `operator[]`, and iterators, as well as `data()` itself, must not invalidate existing references and iterators.
- **Contiguous Null-Termination of `data()`/`c_str()`**: They must return a pointer to a contiguous, null-terminated array within the object's buffer.
- **Non-const Access Requires a Writable Pointer**: Once you use `operator[]` or `data()` to get a non-const pointer, COW is forced to *unshare* (deep copy) the shared buffer to provide you with an exclusive, contiguous, writable pointer.

```cpp
// C++11 requires non-const access to return a pointer to the *actual* buffer
std::string s = "hello";
char* p = &s[0]; // COW must unshare here to satisfy C++11 guarantees
p[0] = 'H';      // Must modify 's' directly, not a shared copy
```

As you can see, COW trying to embrace "sharing," "non-invalidating references," "O(1)," and "contiguous null-termination" simultaneously is a contradiction. The standard decisively chose the latter three, making COW non-conforming. In reality, the transition was turbulent: due to ABI compatibility baggage, libstdc++ dragged its feet until **GCC 5 (2015)** to switch to a non-COW implementation via the `_GLIBCXX_USE_CXX11_ABI` switch (the new inline symbols are `std::__cxx11::string`); libc++ and MSVC's Dinkumware implementation, however, used SSO from the start, avoiding this historical debt entirely.

## SSO Thresholds: Why is sizeof 32?

With COW retired, mainstream implementations shifted uniformly to **SSO (Small String Optimization)**: reserving a small inline buffer inside the `std::string` object. Strings short enough to fit in this buffer avoid heap allocation and are stored directly within the object itself. This also answers "Why `sizeof(std::string)` is 32"—the object must simultaneously hold the inline buffer, a heap pointer, size, and capacity fields. Mainstream implementations stuff all of this into approximately 32 bytes.

I should mention: the SSO threshold is an **implementation detail; the standard never specifies it** (it falls under QoI, Quality of Implementation). In mainstream implementations, libstdc++, libc++, and MSVC STL all have thresholds around 15 bytes (libc++ also has a layout variant with 22 bytes). These numbers are not promises and may change across implementations or versions—so, mark my words—**don't use these thresholds as hard assumptions in your code**. It might be 15 today, but it might not be tomorrow with a different compiler.

## resize_and_overwrite: C++23 Finally Lets You Use string as a Buffer

C++23 added a quite handy member to `std::string`: `resize_and_overwrite`, proposed in **P1072R10** "basic_string::resize_and_overwrite". Its most typical use case is treating `std::string` as a writable buffer to interface with C APIs that "write some data, then tell you how much" (like `snprintf()`, `std::strftime()`, `getcwd()`).

The signature looks like this: `void resize_and_overwrite(size_t n, Operation op)`. It first expands the string capacity to at least `n`, then passes a pointer `p` (pointing to the first character of contiguous storage) and that `n` to the callback `op`. `op` writes the actual content in-place and then **returns an integer r as the new length** (requiring `r <= n`). What's the benefit? Unlike `resize()`, it **does not** value-initialize (zero out) the new region, saving an extra write operation. You only write the bytes you actually need in the callback, then report the actual length.

Freedom comes with a price; `resize_and_overwrite` has several UB red lines to watch out for: `op` must return an integer within `[0, n]`; going out of bounds is undefined behavior. `op` throwing an exception is UB (so `op` is usually marked `noexcept`). `op` cannot modify the `p` or `n` parameters themselves. Finally, every character in the preserved range `[0, r)` must be a determinate value written by `op`; indeterminate values are not allowed. Also, easily overlooked—whether this call triggers reallocation or not, it invalidates all iterators, pointers, and references. To detect support, check `__cpp_lib_string_resize_and_overwrite` (C++23, value `202110L`).

------

## Let's Run It

First, let's look at SSO. Print `sizeof(std::string)` and check the `data()` address of short and long strings to see if they land inside the object.

```cpp
#include <iostream>
#include <string>
#include <vector>

void observe_sso() {
    std::cout << "sizeof(std::string) = " << sizeof(std::string) << std::endl;

    std::string short_str = "short";
    std::string long_str = "This is a very long string that definitely exceeds the small string optimization buffer...";

    std::cout << "Short string (" << short_str << ") data addr: " << static_cast<void*>(short_str.data()) << std::endl;
    std::cout << "Long string (" << long_str.substr(0, 20) << "... ) data addr: " << static_cast<void*>(long_str.data()) << std::endl;

    // A rough check: if the address is far from the stack address of the string object, it's likely on the heap
    std::cout << "Address of short_str object: " << static_cast<void*>(&short_str) << std::endl;
    std::cout << "Address of long_str object:  " << static_cast<void*>(&long_str) << std::endl;
}
```

Now let's compare `resize_and_overwrite` with the old `resize` approach. I've crafted a "mock C API" here—it writes fixed content to a buffer and returns the actual bytes written—to make the difference between the two methods obvious.

```cpp
#include <string>
#include <iostream>
#include <cstring>

// Mock C API: Writes "Hello" into the buffer and returns length 5
size_t mock_c_api_write(char* buffer, size_t buffer_size) {
    const char* msg = "Hello";
    size_t len = strlen(msg);
    if (len > buffer_size) len = buffer_size;
    memcpy(buffer, msg, len);
    return len;
}

void test_resize_and_overwrite() {
    std::string s;

    // Old way (C++20): resize() initializes memory (wasteful)
    s.resize(32); // Reserves space and zero-fills 32 bytes
    size_t written = mock_c_api_write(s.data(), s.size());
    s.resize(written); // Trim to actual size
    std::cout << "Old resize result: " << s << std::endl;

    // New way (C++23): resize_and_overwrite() avoids initialization
    s.clear();
    s.resize_and_overwrite(32, [](char* p, size_t n) {
        // p points to raw storage, n is 32. No zero-filling happened.
        return mock_c_api_write(p, n);
    });
    std::cout << "New resize_and_overwrite result: " << s << std::endl;
}
```

<OnlineCompilerDemo
  title="Deep Dive into string Memory: SSO Observation and resize_and_overwrite"
  source-path="code/examples/vol3/04_string_memory.cpp"
  description="Observe sizeof(std::string) and SSO behavior, compare buffer reuse between resize() and C++23 resize_and_overwrite"
  run-options="-std=c++23"
  allow-run
  allow-x86-asm
/>

------

## References

- [std::basic_string — cppreference](https://en.cppreference.com/w/cpp/string/basic_string)
- [basic_string::data — cppreference](https://en.cppreference.com/w/cpp/string/basic_string/data)
- [basic_string::resize_and_overwrite — cppreference](https://en.cppreference.com/w/cpp/string/basic_string/resize_and_overwrite)
- [N2668 Concurrency Modifications to Basic String](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2668.htm)
- [P1072R10 basic_string::resize_and_overwrite](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p1072r10.html)
