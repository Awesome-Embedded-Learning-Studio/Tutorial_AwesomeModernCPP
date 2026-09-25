---
chapter: 10
cpp_standard:
- 17
- 23
description: Using std::optional to represent 'operations that may fail', replacing error codes and exceptions
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 10: Evolution of Error Handling: From Error Codes to Type Safety'
- 'Chapter 4: std::optional: Elegantly Expressing ''A Value May Be Absent'''
reading_time_minutes: 10
related:
- 'std::expected<T, E>: Type-Safe Error Propagation'
tags:
- host
- cpp-modern
- intermediate
- optional
- 类型安全
title: optional for Error Handling
translation:
  source: documents/vol2-modern-features/ch10-error-handling/02-optional-error.md
  source_hash: ad2583043fe3299a11a075b61a9dc1294d4cdf4f15e58fa501e39ab8f96caa11
  translated_at: '2026-09-25T16:45:25+00:00'
  engine: anthropic
  token_count: 5200
---
# optional for Error Handling: Lightweight, but No Room for Error Details

In the previous article we traced the evolution of C++ error handling, and it ended with the note that `std::optional` can express an "operation that may fail". In this article we take a close look at how well `optional` actually serves error handling, how to use it, and when you should reach for something else.

The conclusion up front: `std::optional` is a precision scalpel, not a Swiss Army knife. It is excellent in the right scenarios, but if you press it into service as a general-purpose error handling tool, you will find yourself all over the codebase guessing "why did this come back nullopt?"

------

## The Semantics of optional: Success or No Value

The semantics of `std::optional<T>` are dead simple — it either holds a value of type `T`, or it is empty (`std::nullopt`). Applied to error handling, that reads as "return a value on success, return empty on failure":

```cpp
#include <optional>
#include <string>

/// Try to parse a string as an integer; return empty on failure
std::optional<int> parse_int(const std::string& s) {
    try {
        std::size_t pos = 0;
        int value = std::stoi(s, &pos);
        if (pos != s.size()) {
            return std::nullopt;  // Extra characters remain; the parse is incomplete
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}
```

The biggest win of this style is that **the semantics live in the type**. The signature `std::optional<int>` already tells the caller "this function may not return a value" — no need to consult documentation, no conventions to memorize; the type itself is the documentation. Once the caller holds the return value, the natural first move is to check whether there is a value:

```cpp
auto result = parse_int("42");
if (result) {
    std::cout << "Got: " << *result << "\n";
} else {
    std::cout << "Parse failed\n";
}
```

------

## Scenarios Where optional Fits

The scenarios `optional` fits best share one trait: **failure is part of the normal course of things, and the caller does not need to know the specific cause of the failure**.

### Scenario 1: Lookup Operations

Lookup is the classic `optional` scenario. Searching a container for an element and not finding it is not an "error" — it is simply "not found", and that distinction matters. You don't need to tell the caller "why it wasn't found", because there is only one possible reason: it doesn't exist.

```cpp
#include <unordered_map>
#include <optional>
#include <string>

struct User {
    std::string name;
    int age;
};

class UserRegistry {
public:
    std::optional<User> find(int id) const {
        auto it = users_.find(id);
        if (it != users_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void add(int id, User user) {
        users_[id] = std::move(user);
    }

private:
    std::unordered_map<int, User> users_;
};

// Usage
UserRegistry registry;
registry.add(1, User{"Alice", 30});

auto user = registry.find(1);
if (user) {
    std::cout << user->name << "\n";  // Alice
}

auto missing = registry.find(99);
// missing is nullopt, but this is a normal outcome, not an error
```

Here is a diagram of find's two return paths and the caller's three ways of accessing the result:

![The two branches of find returning an optional, and how the caller accesses them](./02-optional-error-flow.drawio)

### Scenario 2: Parsing Operations

Parsing information out of external input (config files, user input, network data) fails all the time. If the caller only needs to know "did the parse succeed", `optional` is enough:

```cpp
#include <optional>
#include <string>
#include <charconv>
#include <system_error>

/// Parse a double from a string view
std::optional<double> parse_double(std::string_view sv) {
    double value = 0.0;
    auto [ptr, ec] = std::from_chars(
        sv.data(), sv.data() + sv.size(), value);
    if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
        return value;
    }
    return std::nullopt;
}

// Usage
auto v1 = parse_double("3.14");     // optional(3.14)
auto v2 = parse_double("hello");    // nullopt
auto v3 = parse_double("3.14abc");  // nullopt (extra characters)
```

### Scenario 3: When a Default Value Is Available

When you have a reasonable default for the case where the operation fails, `optional`'s `value_or` keeps the code extremely tidy:

```cpp
#include <optional>
#include <string>
#include <cstdlib>

std::optional<std::string> get_env(const std::string& key) {
    const char* val = std::getenv(key.c_str());
    if (val) return std::string(val);
    return std::nullopt;
}

// Use value_or to provide default values
std::string log_level = get_env("LOG_LEVEL").value_or("INFO");
int max_threads = parse_int(get_env("MAX_THREADS").value_or("4")).value_or(4);
```

### Scenario 4: Cache Lookup

A cache hit returns the value, a miss returns empty — no error information needed whatsoever:

```cpp
template <typename Key, typename Value>
class SimpleCache {
public:
    std::optional<Value> get(const Key& key) const {
        auto it = cache_.find(key);
        if (it != cache_.end() && !it->second.expired) {
            return it->second.data;
        }
        return std::nullopt;
    }

    void put(const Key& key, Value value) {
        cache_[key] = {std::move(value), false};
    }

private:
    struct Entry {
        Value data;
        bool expired = false;
    };
    std::unordered_map<Key, Entry> cache_;
};
```

------

## Scenarios Where optional Does Not Fit

The fatal limitation of `optional` is that **it carries no error information**. Once the caller needs to know "why it failed", `optional` is no longer enough.

### When You Need to Distinguish Multiple Error Types

```cpp
// Bad: three different failure causes squashed into one nullopt
std::optional<Config> load_config(const std::string& path) {
    auto f = open_file(path);
    if (!f) return std::nullopt;          // File missing? Permissions too low?

    auto content = read_content(f);
    if (content.empty()) return std::nullopt;  // Empty file? Read error?

    return parse_config(content);          // A parse failure is also nullopt
}

auto cfg = load_config("app.cfg");
if (!cfg) {
    // What do I do now? A missing file means create it, a bad format means report it,
    // insufficient permissions means escalate —
    // but all I know is "it failed", and I can't tell any of them apart
}
```

Situations like this call for `std::expected<Config, ConfigError>`, or a return struct that carries error information.

### When You Need an Error Propagation Chain

When you need to chain several fallible operations together and still know, at the end of the chain, which step failed, `optional` makes debugging genuinely painful. Every failed step collapses into `nullopt`, so at the end all you know is "something failed somewhere" — not where.

------

## C++23 Monadic Operations

C++23 adds three monadic member functions to `std::optional`: `and_then`, `transform`, and `or_else`. These three operations make chained handling of `optional` far more elegant.

### and_then: Chaining Operations That May Fail

`and_then` takes a function that receives the value inside the `optional` and returns a new `optional`. If the original `optional` is empty, an empty optional is returned immediately and the function is never called:

```cpp
#include <optional>
#include <string>
#include <iostream>

struct UserProfile {
    std::string name;
    int age;
};

std::optional<UserProfile> fetch_from_cache(int user_id) {
    // Simulated: ID 1 is in the cache
    if (user_id == 1) return UserProfile{"Alice", 30};
    return std::nullopt;
}

std::optional<UserProfile> fetch_from_server(int user_id) {
    // Simulated: IDs 1 and 2 are on the server
    if (user_id == 1 || user_id == 2) return UserProfile{"Bob", 25};
    return std::nullopt;
}

std::optional<int> extract_age(const UserProfile& profile) {
    if (profile.age > 0) return profile.age;
    return std::nullopt;
}

int main() {
    int user_id = 1;

    // C++23 monadic chain
    auto age_next = fetch_from_cache(user_id)
        .or_else([user_id]() { return fetch_from_server(user_id); })
        .and_then(extract_age)
        .transform([](int age) { return age + 1; });

    if (age_next) {
        std::cout << "Next year age: " << *age_next << "\n";
    }
}
```

For comparison, here is the same logic written without the monadic operations:

```cpp
// C++20 style: nested if/else
auto profile = fetch_from_cache(user_id);
if (!profile) {
    profile = fetch_from_server(user_id);
}

std::optional<int> age_next;
if (profile) {
    auto age = extract_age(*profile);
    if (age) {
        age_next = *age + 1;
    }
}
```

The monadic version puts the normal path on a single chain, and every step states clearly what to do with the data once it arrives. Error propagation is automatic — the moment any step returns empty, all subsequent steps are skipped.

### transform: Transforming the Value

The difference between `transform` and `and_then` is that the function passed to `transform` returns a plain value (not an `optional`), and `transform` wraps that result back into an `optional` automatically:

```cpp
// transform: the return value is wrapped into an optional automatically
auto upper_name = fetch_from_cache(1)
    .transform([](const UserProfile& p) -> std::string {
        std::string s = p.name;
        for (auto& c : s) c = std::toupper(c);
        return s;
    });
// The type of upper_name is std::optional<std::string>
```

One line to tell them apart: `and_then` is for operations where the next step may fail (the function returns an `optional`); `transform` is for transformations that are guaranteed to succeed (the function returns a plain value).

### or_else: Providing a Fallback

`or_else` invokes the function you pass in when the `optional` is empty, and it is typically used to offer a fallback or log the miss:

```cpp
auto result = fetch_from_cache(user_id)
    .or_else([user_id]() {
        std::cerr << "Cache miss for user " << user_id << "\n";
        return fetch_from_server(user_id);
    })
    .or_else([]() {
        std::cerr << "Server also failed, using default\n";
        return std::optional<UserProfile>(UserProfile{"Default", 0});
    });
```

------

## Comparison with Rust's Option

If you come from Rust, C++'s `optional` may feel a bit underpowered. It is, in two main ways:

Rust's `Option<T>` has the compiler's `#[must_use]` check behind it — ignore an `Option` return value and the compiler warns you. C++'s `std::optional` offers no such guarantee; you can annotate the return type with `[[nodiscard]]`, but the standard library itself does not do so.

Rust's `Option<T>` also has the powerful `?` operator for error propagation. Write `let val = might_fail()?;` in a function, and if `might_fail` returns `None`, the function returns `None` immediately. C++ has no syntax this elegant — you check by hand, or simulate it with a macro (the `TRY` macro mentioned earlier).

That said, C++23's monadic operations have closed much of this gap — chained calls are not as terse as the `?` operator, but they are already perfectly usable.

------

## A Comprehensive Example

Finally, a fairly complete example — configuration file parsing — showing `optional` at work in a realistic setting:

```cpp
#include <optional>
#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <iostream>
#include <charconv>

struct ServerConfig {
    std::string host;
    int port;
    int timeout_ms;
};

class ConfigParser {
public:
    std::optional<ServerConfig> parse(std::string_view content) {
        ServerConfig cfg;

        cfg.host = extract_field(content, "host")
            .value_or("localhost");

        auto port_str = extract_field(content, "port");
        if (port_str) {
            auto p = parse_int(*port_str);
            if (!p || *p < 1 || *p > 65535) {
                return std::nullopt;  // Invalid port
            }
            cfg.port = *p;
        } else {
            cfg.port = 8080;
        }

        auto timeout_str = extract_field(content, "timeout_ms");
        if (timeout_str) {
            auto t = parse_int(*timeout_str);
            if (!t || *t < 0) {
                return std::nullopt;
            }
            cfg.timeout_ms = *t;
        } else {
            cfg.timeout_ms = 5000;
        }

        return cfg;
    }

private:
    static std::optional<std::string> extract_field(
        std::string_view content, std::string_view key) {
        std::string search = std::string(key) + "=";
        auto pos = content.find(search);
        if (pos == std::string_view::npos) return std::nullopt;

        auto start = pos + search.size();
        auto end = content.find('\n', start);
        if (end == std::string_view::npos) end = content.size();

        return std::string(content.substr(start, end - start));
    }

    static std::optional<int> parse_int(std::string_view sv) {
        int value = 0;
        auto [ptr, ec] = std::from_chars(
            sv.data(), sv.data() + sv.size(), value);
        if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
            return value;
        }
        return std::nullopt;
    }
};

int main() {
    std::string config_text = "host=192.168.1.1\nport=3000\ntimeout_ms=10000\n";

    ConfigParser parser;
    auto cfg = parser.parse(config_text);

    if (cfg) {
        std::cout << "Host: " << cfg->host
                  << ", Port: " << cfg->port
                  << ", Timeout: " << cfg->timeout_ms << "ms\n";
    } else {
        std::cout << "Failed to parse config\n";
    }
}
```

This example shows the typical use of `optional`: when looking up a field, the `optional` says "may not exist"; when parsing a number, it says "may fail"; and `value_or` provides the defaults. The code is clear, with the normal path and the failure path visible at a glance.

------

## References

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [Monadic operations for std::optional (C++23)](https://en.cppreference.com/w/cpp/utility/optional)
- [P0798R8 - Monadic operations for std::expected](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2505r1.html)
