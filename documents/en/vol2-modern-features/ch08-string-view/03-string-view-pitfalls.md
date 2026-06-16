---
chapter: 8
cpp_standard:
- 17
description: Dangling references, null termination, implicit conversions — common
  pitfalls of `string_view` and how to avoid them
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 8: string_view 内部原理'
reading_time_minutes: 14
related:
- string_view 性能分析
tags:
- host
- cpp-modern
- intermediate
title: '`string_view` Pitfalls and Best Practices'
translation:
  source: documents/vol2-modern-features/ch08-string-view/03-string-view-pitfalls.md
  source_hash: a049100811fd3d54a3245ad281dc87dbc31389ca64b767f940c80463fd89d3a6
  translated_at: '2026-06-16T03:59:10.847798+00:00'
  engine: anthropic
  token_count: 2546
---
# `string_view` Pitfalls and Best Practices

In the previous two articles, we discussed the internal mechanics and performance benefits of `std::string_view`. It seems like a perfect tool—lightweight, fast, and zero-allocation. But I must pour some cold water on the situation here: `std::string_view` is one of the easiest C++ features to use when introducing undefined behavior (UB). The reason is simple: it doesn't own data. The moment you forget this, dangling references, wild pointers, garbled output, and even security vulnerabilities may await you.

In this article, we will focus specifically on the "gotchas" of `std::string_view`. I will compile the pitfalls I have encountered myself, seen others fall into, and those that static analysis tools can help you catch. Finally, I will provide a best practices cheat sheet.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Identify all common patterns of `std::string_view` dangling references.
> - [ ] Understand the null termination issue and its impact on C API interoperability.
> - [ ] Master the safe usage boundaries of `std::string_view`.
> - [ ] Learn about the future of `std::string_view` in C++23.

## Pitfall 1: Dangling References—The Number One Killer

`std::string_view` does not own the underlying data and does not extend the lifetime of any object. This is its most fundamental characteristic and the root cause of the vast majority of bugs. Dangling references occur more often than you might think.

### Returning a view pointing to a temporary string

This is the most classic pitfall pattern that almost every beginner encounters once:

```cpp
// BAD: Returning a view to a local string
std::string_view get_view() {
    std::string temp = "Hello, world";
    return temp; // Implicit conversion to string_view
}

void usage() {
    auto sv = get_view();
    std::cout << sv << std::endl; // UB: dangling reference!
}
```

When the `get_view` function ends, the local variable `temp` is destroyed, and its internal character buffer is released. However, `sv` foolishly still points to that memory. This is a typical use-after-free scenario and constitutes undefined behavior—it might coincidentally work, might output garbage, might work in debug builds but crash in release. The scariest part is "coincidentally working," because it means the bug can lie dormant for a long time before surfacing.

### Implicit temporary objects are more insidious

The example above at least involved you actively creating a local `std::string`, which makes troubleshooting relatively easy. More insidious are temporary objects created for you by the compiler:

```cpp
// BAD: sv points to a temporary std::string
std::string_view sv = std::string("Hello") + ", " + "World";
// The temporary string is destroyed at the end of this line.
```

This line of code looks like it is assigning a value to `sv`, but actually `std::string("Hello") + ", " + "World"` is a temporary object that is destroyed at the end of this statement. `sv` points to freed memory from the moment it is born.

Let's look at a slightly more indirect version:

```cpp
// BAD: Passing a temporary string to a function returning string_view
std::string_view first_n(std::string_view s, size_t n) {
    return s.substr(0, n); // Logic is fine
}

void caller() {
    auto sv = first_n(std::string("temporary"), 5); // BUG!
    std::cout << sv << std::endl; // UB
}
```

The problem with this example is: the logic of the `first_n` function itself is correct—it accepts a `std::string_view` parameter and returns a `std::string_view`, which is completely fine. The problem lies at the call site: a temporary `std::string` is passed. If the caller passed a string literal (`"literal"`), it would be safe because the lifetime of a literal is the entire program. But if a temporary `std::string` is passed, the returned `std::string_view` is left dangling.

⚠️ **The characteristic of this type of bug:** It might work normally in debug builds (because the debugger's memory padding might coincidentally allow the dangling view to read correct data), but suddenly crash in release builds. I once spent an entire afternoon tracking this kind of bug, only to find it was a three-line utility function where the caller passed a temporary `std::string`.

### Indirect reference chains

Sometimes dangling references don't happen directly, but occur indirectly through an intermediate layer:

```cpp
// BAD: Storing string_view in a container with longer lifetime
class ConfigManager {
    std::unordered_map<std::string, std::string> data;
    std::vector<std::pair<std::string_view, std::string_view>> cache; // Danger!
public:
    void add(std::string_view key, std::string_view value) {
        cache.emplace_back(key, value); // Storing views to temporary strings
    }
};

void usage() {
    ConfigManager mgr;
    mgr.add("timeout", std::to_string(1000)); // std::string temporary destroyed here
    // mgr.cache now contains dangling views
}
```

The problem with this `ConfigManager` class is that the value type of `cache` is `std::string_view`. `add` is safe when called with literals, but if you write this:

```cpp
// BAD: The temporary string created by std::to_string is destroyed
mgr.add("timeout", std::to_string(1000));
```

The insidious nature of this bug is that the `ConfigManager` interface looks normal, and the caller's code looks normal, but the combination creates a problem. The root cause is that `std::string_view` is stored in a container intended to hold long-lived data, but the underlying data is destroyed before the container.

## Pitfall 2: The Null Termination Issue

`std::string_view` does not guarantee that the underlying data ends with `\0`. We mentioned this in the principles article, but its practical impact is much greater than you might think.

### The deadly combination of `data()` and C APIs

```cpp
// DANGEROUS: Passing string_view to a C API expecting null termination
void legacy_log(const char* msg); // Expects null-terminated string

void log_message(std::string_view sv) {
    legacy_log(sv.data()); // UB if sv is not null-terminated!
}
```

An even more dangerous scenario: when the buffer following `sv.data()` is not followed by `\0`, but by other data:

```cpp
// DANGEROUS: Buffer overrun
char buffer[100] = "HelloWorld"; // No null terminator in the middle
std::string_view sv(buffer, 5); // Points to "Hello"

printf("%s\n", sv.data()); // Prints "HelloWorld" or crashes!
```

`printf` will read until it encounters a `\0`, so it outputs the entire `buffer` instead of the first 5 characters of `sv`. This is the "good case"—if there is no `\0` in the memory following `sv`, `printf` will read out of bounds, potentially crashing or leaking sensitive information in memory.

### Correct approach requiring NUL termination

If your function needs to call a C API (`strlen`, `printf`, system calls, etc.) and the data source is `std::string_view`, the safest approach is to explicitly construct a `std::string`:

```cpp
// SAFE: Explicitly construct std::string to ensure null termination
void legacy_log_safe(std::string_view sv) {
    std::string s(sv); // One copy, ensures null termination
    legacy_log(s.c_str());
}
```

This introduces a copy, but it is the correct cost. If you use `std::string_view` for performance, then "admitting defeat" and doing a copy where NUL termination is truly needed is far better than writing a UB.

### Safety of `std::string` constructor

Conversely, constructing a `std::string` from `std::string_view` is safe—the `std::string` constructor correctly handles input without NUL termination (because it has length information):

```cpp
std::string_view sv("hello\0world", 11); // Contains embedded null
std::string s(sv); // s correctly contains "hello\0world"
```

## Pitfall 3: Implicit Conversion Traps

The implicit conversion from `std::string` to `std::string_view` is one-way and easy. This is good—it allows you to seamlessly pass a `std::string` to a function accepting `std::string_view`. But the reverse conversion requires explicit operations, and sometimes "implicit" itself is a trap.

### `string` to `string_view`: Too easy

```cpp
// BAD: Accidentally passing a temporary string
void process(std::string_view sv);

void caller() {
    process(std::string("temporary") + " data"); // Temporary destroyed, sv dangles
}
```

The "convenience" of implicit conversion makes you let your guard down. During code review, it can be hard to notice that a `std::string_view` parameter was passed a temporary `std::string`—because syntactically it is completely legal, and the compiler won't warn you.

### `string_view` to `string`: Must be explicit

`std::string_view` cannot be implicitly converted to `std::string`; you must construct it explicitly:

```cpp
std::string_view sv = "hello";
// std::string s = sv; // Error: no implicit conversion
std::string s(sv);    // OK: explicit construction
```

This design is intentional—the conversion from `std::string_view` to `std::string` involves heap allocation and character copying, and the compiler doesn't want to perform such a heavy operation without your knowledge.

## Pitfall 4: Functions Returning `string_view`

Returning `std::string_view` from a function is not a problem per se—provided the data pointed to by the returned view lives long enough. Here are safe patterns:

```cpp
// SAFE: Returning a view of a parameter
std::string_view get_extension(std::string_view filename) {
    auto pos = filename.find_last_of('.');
    if (pos == std::string_view::npos) return "";
    return filename.substr(pos);
}

// SAFE: Returning a view of static storage
std::string_view get_greeting() {
    return "Hello, world"; // Static storage, lives forever
}
```

Unsafe patterns:

```cpp
// BAD: Returning a view of a local variable
std::string_view get_bad_view() {
    std::string local = "temp";
    return local; // Dangling!
}
```

A useful rule of thumb is: if a function returns `std::string_view`, it must be an observer of some data that "lives longer." Either it points to the parameter's data (valid during the call), or to static storage (valid forever), or to a member variable (valid during the object's lifetime). If you find a function creating a new `std::string` internally and returning its view—that is 100% a bug.

## Pitfall 5: Storing `string_view` as a Member Variable

Using `std::string_view` as a class member variable requires extreme caution. The lifetime of a class is usually much longer than a function, while the data pointed to by `std::string_view` might be long gone.

```cpp
// BAD: string_view member variable
struct Person {
    std::string_view name; // Dangerous!
    Person(std::string_view n) : name(n) {}
};

void usage() {
    Person p(std::string("Alice")); // Temporary string destroyed
    // p.name is now dangling
}
```

If someone calls it like this:

```cpp
// BAD: Constructing with a temporary
Person p(std::string("Alice"));
```

A better approach is to let the class hold the data itself:

```cpp
// GOOD: std::string member variable
struct Person {
    std::string name;
    Person(std::string_view n) : name(n) {} // Explicit copy
};
```

While this introduces a copy, it eliminates an entire class of lifetime bugs. In most scenarios, this performance cost is worth it.

## Best Practices Cheat Sheet

We have compiled all the pitfalls and corresponding avoidance methods into a table:

| Scenario | Risk | Recommended Practice |
|----------|------|----------------------|
| Function parameters (read-only) | Low | Pass `std::string_view` by value |
| Function return value | High | Do not return a view pointing to local/temporary data |
| Class member variables | High | Use `std::string` to hold data; use `std::string_view` only for short-term observation |
| Container keys (`std::map`) | High | Ensure the underlying string outlives the container, or use `std::string` as the key |
| Calling C APIs | High | Explicitly construct `std::string`, use `c_str()` |
| Storing `std::string_view` in containers | High | Only store views pointing to static data, or use `std::string` |
| Async/delayed execution | High | Capture `std::string` into the lambda; ensure data lives long enough |
| Signal/callback registration | High | `std::string_view` in callbacks may execute later; use `std::string` instead |

There is only one core principle: **`std::string_view` is only for short-term, synchronous, read-only access scenarios.** If data needs to "live longer than the current function call," use `std::string`.

Let me add a few more lessons learned from my actual projects. First, focus heavily on all `std::string_view` member variables during code review—if there are any, ask "when will the data it points to be released?" Second, for all functions accepting `std::string_view` parameters, explicitly document in the docs that "the parameter must be valid during the function call." Third, if your project enables AddressSanitizer (ASan), be sure to run tests under ASan—it can precisely capture `std::string_view` use-after-free issues 100 times faster than you can troubleshoot yourself. Enabling it is simple: add `-fsanitize=address` at compile time and `-fsanitize=address` at link time.

```bash
# Example of enabling ASan with GCC/Clang
g++ -fsanitize=address -g main.cpp -o main
./main
```

## Looking Ahead: C++26 `std::zstring_view` (Proposal P3655)

The C++ community has also recognized the shortcomings of `std::string_view` regarding NUL termination. Proposal P3655 suggests introducing `std::zstring_view` (or `std::cstring_view`), aiming to provide a `std::string_view` variant that guarantees NUL termination. This proposal is currently targeting the C++26 standard and has not yet been officially released.

The design philosophy of `std::zstring_view` is to add a NUL termination guarantee to the basis of `std::string_view`, making it safe to pass to C APIs. It is still non-owning, so lifetime issues remain, but it at least solves the NUL termination half of the pain point.

Before `std::zstring_view` officially enters the standard, if you need similar functionality, you can wrap a lightweight `ZStringView` class yourself—the core idea is: inherit from (or compose) `std::string_view`, check for NUL termination during construction, and have the `data()` method return a pointer guaranteed to be NUL-terminated. However, honestly, in most projects, directly using `std::string` is sufficient.

## Summary

`std::string_view` is a double-edged sword. Its performance benefits are real and significant, but its lifetime risks are also real and serious. My summary of usage principles is: feel free to use `std::string_view` for function parameters (read-only, short-term use); use it cautiously for return values (ensure the pointed-to data lives long enough); try to avoid it for member variables and container storage (unless you are very clear about the data's lifetime); and when calling C APIs, remember to explicitly convert to a NUL-terminated `std::string`.

The key to using `std::string_view` well is not memorizing a bunch of rules, but developing an intuition: every time you write `std::string_view`, automatically ask yourself a question in your mind—"Is the data it points to still there?"

## Reference Resources

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)
- [cppreference: data() explanation (no NUL guarantee)](https://en.cppreference.com/w/cpp/string/basic_string_view/data.html)
- [PVS-Studio: C++ programmer's guide to undefined behavior - string_view](https://pvs-studio.com/en/blog/posts/cpp/1149/)
- [StackOverflow: Using string_view with C API expecting null-terminated strings](https://stackoverflow.com/questions/41286898/using-stdstring-view-with-api-that-expects-null-terminated-string)
- [WG21 P3655R0: zstring_view proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3655r0.html)
- [ISO C++ discussion: string_view design considerations](https://groups.google.com/a/isocpp.org/g/std-discussion/c/Gj5gt5E-po8)
