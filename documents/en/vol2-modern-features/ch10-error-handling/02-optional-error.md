---
chapter: 10
cpp_standard:
- 17
- 23
description: Using `std::optional` to represent 'operations that may fail', replacing
  error codes and exceptions
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 10: 错误处理的演进'
- 'Chapter 4: std::optional'
reading_time_minutes: 10
related:
- std::expected
tags:
- host
- cpp-modern
- intermediate
- optional
- 类型安全
title: '`optional` for Error Handling'
translation:
  source: documents/vol2-modern-features/ch10-error-handling/02-optional-error.md
  source_hash: 9446af5edec00db3be70e78168f9629150e5b832abd351e8482847eb97a5bc93
  translated_at: '2026-06-16T03:59:17.117844+00:00'
  engine: anthropic
  token_count: 2711
---
# Using optional for Error Handling

In the previous post, we reviewed the evolution of C++ error handling and mentioned that `std::optional` can be used to express operations that "may fail." In this post, we will take a deep dive into whether `std::optional` is actually good for error handling scenarios, how to use it, and when not to use it.

To cut to the chase: `std::optional` is a precise scalpel, not a Swiss Army knife. It is extremely handy in specific scenarios, but if you use it as a general-purpose error handling tool, you will find yourself constantly guessing, "Why did it return nullopt?"

------

## The Semantics of optional: Success or No Value

The semantics of `std::optional` are very straightforward—it either holds a value of type `T` or is empty (`std::nullopt`). Using it for error handling means "return a value on success, return empty on failure":

```cpp
std::optional<int> parse_int(std::string_view str);
```

The biggest advantage of this approach is that **the semantics are in the type**. The function signature `std::optional<int> parse_int(...)` already tells the caller, "this function might not return a value." You don't need to check documentation or remember conventions—the type itself is the documentation. After receiving the return value, the first thing the caller does is naturally check if a value exists:

```cpp
if (auto value = parse_int("42"); value.has_value()) {
    // Use *value or value.value()
}
```

------

## Scenarios Suitable for optional

The scenarios where `std::optional` shines share a common characteristic: **failure is a normal part of the flow, and the caller doesn't need to know the specific reason for the failure**.

### Scenario 1: Lookup Operations

Lookup is the most classic `std::optional` scenario. Searching for an element in a container—finding nothing isn't an "error," it's just "not found"—this distinction is crucial. You don't need to tell the caller "why it wasn't found," because there is only one reason: it doesn't exist.

```cpp
std::optional<User> find_user(UserId id);
```

### Scenario 2: Parsing Operations

Parsing information from external input (configuration files, user input, network data) fails all the time. If the caller only needs to know "did parsing succeed?", `std::optional` is sufficient:

```cpp
std::optional<Config> parse_config(std::string_view content);
```

### Scenario 3: Scenarios with Default Values

When you have a reasonable default value upon failure, `std::optional`'s `value_or` can make the code very concise:

```cpp
int timeout = get_timeout_config().value_or(3000); // Default to 3000ms
```

### Scenario 4: Cache Lookup

Cache hit returns the value, cache miss returns empty—no error information is needed:

```cpp
std::optional<Image> load_image(const std::string& path);
```

------

## Scenarios Not Suitable for optional

The fatal limitation of `std::optional` is that **it carries no error information**. When the caller needs to know "why it failed," `std::optional` isn't enough.

### Need to Distinguish Multiple Error Types

```cpp
// Bad: Caller can't tell if it was a network error or a parsing error
std::optional<Data> fetch_data(const std::string& url);
```

In this case, you should use `std::expected` or a return struct that carries error information.

### Need Error Propagation Chains

When you need to chain multiple operations that might fail and know exactly which step failed at the end of the chain, `std::optional` makes debugging very painful. Every failure turns into `std::nullopt`. In the end, you only know "something failed somewhere," but not where.

------

## C++23 Monadic Operations

C++23 adds three monadic member functions to `std::optional`: `and_then`, `transform`, and `or_else`. These three operations make chaining `std::optional` much more elegant.

### and_then: Chaining Operations That Might Fail

`and_then` takes a function that accepts the value inside the `std::optional` and returns a new `std::optional`. If the original `std::optional` is empty, it directly returns empty without calling the function:

```cpp
auto result = find_user(id)
    .and_then([](const User& user) { return get_avatar(user); })
    .and_then([](const Avatar& avatar) { return save_to_disk(avatar); });
```

Compare this to the version without monadic operations:

```cpp
auto user_opt = find_user(id);
if (!user_opt) return std::nullopt;
auto avatar_opt = get_avatar(*user_opt);
if (!avatar_opt) return std::nullopt;
return save_to_disk(*avatar_opt);
```

The monadic version puts the "happy path" on a single chain. Each step clearly expresses "what to do after getting the data." Error propagation is automatic—if any step returns empty, all subsequent steps are skipped.

### transform: Transforming the Value

The difference between `transform` and `and_then` is that the function passed to `transform` returns a normal value (not an `std::optional`), and `transform` automatically wraps the result back into an `std::optional`:

```cpp
auto size = find_user(id)
    .transform([](const User& u) { return u.avatar_url; })
    .transform([](const std::string& url) { return url.length(); });
```

To put it simply: `and_then` is for "next step might fail" operations (function returns `std::optional`), while `transform` is for "next step will succeed" transformations (function returns a normal value).

### or_else: Providing a Fallback

`or_else` calls the passed function when the `std::optional` is empty, usually used to provide a fallback or log a message:

```cpp
auto result = get_cached_data().or_else([]{
    log_warning("Cache miss, fetching from remote...");
    return fetch_remote_data();
});
```

------

## Comparison with Rust's Option

Friends who have used Rust might feel that C++'s `std::optional` is a bit "weak." That is indeed the case, mainly in two aspects:

Rust's `Option` has compiler `#[must_use]` checks—if you ignore an `Option` return value, the compiler will warn you. C++'s `std::optional` doesn't have this guarantee. Although you can use `[[nodiscard]]` to annotate the return type, the standard library doesn't do this.

Rust's `Option` has a powerful `?` operator for error propagation. Writing `func()?` in a function means if `func()` returns `None`, the function immediately returns `None`. C++ doesn't have such elegant syntax; you need to check manually or use macros to simulate it (like the `TRY` macro mentioned earlier).

However, C++23's monadic operations have largely closed this gap—while chaining isn't as concise as the `?` operator, it is already quite usable.

------

## Comprehensive Example

Finally, let's look at a complete example—configuration file parsing—to show how `std::optional` is used in real-world scenarios:

```cpp
std::optional<int> parse_field(const json& obj, std::string_view key) {
    if (!obj.contains(key)) return std::nullopt; // Field doesn't exist
    return obj[key].get_int(); // Returns nullopt on type mismatch
}

void load_config(const json& config) {
    // Use value_or to provide defaults
    int timeout = parse_field(config, "timeout").value_or(3000);

    // Critical field: use check
    if (auto port = parse_field(config, "port"); port.has_value()) {
        start_server(*port);
    } else {
        log_error("Missing required field: port");
    }
}
```

This example demonstrates the typical usage of `std::optional`: using `std::optional` to indicate "might not exist" when looking up fields, and "might fail" when parsing numbers, and using `value_or` to provide defaults. The code is clear, and the happy path and failure path are distinct at a glance.

------

## Summary

`std::optional` has a clear position in the field of error handling: it is suitable for simple scenarios where "failure needs no reason"—lookups, parsing, caching, default values. If the scenario requires distinguishing error types, needs error propagation chains, or requires diagnosing issues at the end of the chain, you should switch to `std::expected` or other heavier solutions.

C++23's monadic operations (`and_then`, `transform`, `or_else`) make chaining `std::optional` elegant, greatly reducing nested `if` code. If your project is still on C++17, writing a few helper functions can achieve a similar effect.

In the next post, we will look at `std::expected`—when you need "value + error information," how does it handle it?

## Reference Resources

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [Monadic operations for std::optional (C++23)](https://en.cppreference.com/w/cpp/utility/optional)
- [P0798R8 - Monadic operations for std::expected](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2505r1.html)
