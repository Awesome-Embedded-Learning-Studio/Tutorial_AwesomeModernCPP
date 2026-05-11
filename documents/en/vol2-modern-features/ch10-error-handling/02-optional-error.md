# Using optional for Error Handling

In the previous article, we traced the evolution of C++ error handling, and finally mentioned that `std::optional` can be used to express "operations that might fail." In this article, we take a deep dive into whether `optional` is actually good for error handling, how to use it, and when you shouldn't.

Let's start with the conclusion: `std::optional` is a precise scalpel, not a Swiss Army knife. It works wonderfully in specific scenarios, but if you use it as a general-purpose error handling tool, you'll find yourself constantly guessing "why did it return nullopt?"

------

## The Semantics of optional: Success or No Value

The semantics of `std::optional<T>` are very straightforward—it either holds a value of type `T`, or it is empty (`std::nullopt`). When applied to error handling, this means "return a value on success, return empty on failure":

```cpp
#include <optional>
#include <string>

/// 尝试将字符串解析为整数，失败则返回空
std::optional<int> parse_int(const std::string& s) {
    try {
        std::size_t pos = 0;
        int value = std::stoi(s, &pos);
        if (pos != s.size()) {
            return std::nullopt;  // 有多余字符，解析不完整
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}
```

The biggest advantage of this approach is that **the semantics live in the type**. The function signature `std::optional<int>` already tells the caller "this function might not return a value." You don't need to check the documentation or remember conventions—the type itself is the documentation. After receiving the return value, the caller's first natural step is to check whether a value exists:

```cpp
auto result = parse_int("42");
if (result) {
    std::cout << "Got: " << *result << "\n";
} else {
    std::cout << "Parse failed\n";
}
```

------

## Scenarios Suited for optional

The scenarios where `optional` shines all share one common trait: **failure is a normal part of the operation, and the caller doesn't need to know the specific reason for the failure**.

### Scenario 1: Lookup Operations

Lookup is the most classic optional scenario. Searching for an element in a container and not finding it isn't an "error"—it's simply "not found." This distinction is important. You don't need to tell the caller "why it wasn't found," because there is only one reason: it doesn't exist.

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

// 使用
UserRegistry registry;
registry.add(1, User{"Alice", 30});

auto user = registry.find(1);
if (user) {
    std::cout << user->name << "\n";  // Alice
}

auto missing = registry.find(99);
// missing 是 nullopt，但这是正常情况，不是错误
```

### Scenario 2: Parsing Operations

Parsing information from external input (configuration files, user input, network data) is bound to fail from time to time. If the caller only needs to know "did the parsing succeed?", `optional` is sufficient:

```cpp
#include <optional>
#include <string>
#include <charconv>
#include <system_error>

/// 从字符串视图解析浮点数
std::optional<double> parse_double(std::string_view sv) {
    double value = 0.0;
    auto [ptr, ec] = std::from_chars(
        sv.data(), sv.data() + sv.size(), value);
    if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
        return value;
    }
    return std::nullopt;
}

// 使用
auto v1 = parse_double("3.14");     // optional(3.14)
auto v2 = parse_double("hello");    // nullopt
auto v3 = parse_double("3.14abc");  // nullopt（有多余字符）
```

### Scenario 3: Scenarios with Default Values

When you have a reasonable default value if an operation fails, the `value_or` method of `optional` can make your code very concise:

```cpp
#include <optional>
#include <string>
#include <cstdlib>

std::optional<std::string> get_env(const std::string& key) {
    const char* val = std::getenv(key.c_str());
    if (val) return std::string(val);
    return std::nullopt;
}

// 使用 value_or 提供默认值
std::string log_level = get_env("LOG_LEVEL").value_or("INFO");
int max_threads = parse_int(get_env("MAX_THREADS").value_or("4")).value_or(4);
```

### Scenario 4: Cache Lookups

Return a value on a cache hit, return empty on a miss—this doesn't require any error information:

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

## Scenarios Not Suited for optional

The fatal limitation of `optional` is that **it carries no error information**. When the caller needs to know "why did it fail?", `optional` is no longer enough.

### Needing to Distinguish Between Multiple Error Types

```cpp
// 不好：三种不同的失败原因被揉成了一个 nullopt
std::optional<Config> load_config(const std::string& path) {
    auto f = open_file(path);
    if (!f) return std::nullopt;          // 文件不存在？权限不够？

    auto content = read_content(f);
    if (content.empty()) return std::nullopt;  // 空文件？读取出错？

    return parse_config(content);          // 解析失败也是 nullopt
}

auto cfg = load_config("app.cfg");
if (!cfg) {
    // 我现在该怎么办？文件不存在要创建，格式错误要报告，权限不够要提权
    // 但我只知道"失败了"，什么都区分不了
}
```

In this case, you should use `std::expected<Config, ConfigError>` or a return struct that carries error information.

### Needing an Error Propagation Chain

When you need to chain multiple potentially failing operations together and want to know at the end of the chain which step failed, `optional` makes debugging very painful. Every failed step simply becomes `nullopt`, so by the end, you only know "something failed somewhere," but you don't know where.

------

## C++23 Monadic Operations

C++23 added three monadic member functions to `std::optional`: `and_then`, `transform`, and `or_else`. These three operations make chaining `optional` operations much more elegant.

### and_then: Chaining Potentially Failing Operations

`and_then` takes a function that accepts the value inside the `optional` and returns a new `optional`. If the original `optional` is empty, it directly returns empty without calling the function:

```cpp
#include <optional>
#include <string>
#include <iostream>

struct UserProfile {
    std::string name;
    int age;
};

std::optional<UserProfile> fetch_from_cache(int user_id) {
    // 模拟：ID 1 在缓存中
    if (user_id == 1) return UserProfile{"Alice", 30};
    return std::nullopt;
}

std::optional<UserProfile> fetch_from_server(int user_id) {
    // 模拟：ID 1 和 2 在服务器上
    if (user_id == 1 || user_id == 2) return UserProfile{"Bob", 25};
    return std::nullopt;
}

std::optional<int> extract_age(const UserProfile& profile) {
    if (profile.age > 0) return profile.age;
    return std::nullopt;
}

int main() {
    int user_id = 1;

    // C++23 monadic 链
    auto age_next = fetch_from_cache(user_id)
        .or_else([user_id]() { return fetch_from_server(user_id); })
        .and_then(extract_age)
        .transform([](int age) { return age + 1; });

    if (age_next) {
        std::cout << "Next year age: " << *age_next << "\n";
    }
}
```

Compare this with the approach without monadic operations:

```cpp
// C++20 风格：嵌套的 if/else
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

The monadic version puts the "happy path" on a single chain, where each step clearly expresses "what to do after getting the data." Error propagation is automatic—if any step returns empty, all subsequent steps are skipped.

### transform: Transforming the Value

The difference between `transform` and `and_then` is that the function passed to `transform` returns a plain value (not an `optional`), and `transform` automatically wraps the result back into an `optional`:

```cpp
// transform：返回值会被自动包装成 optional
auto upper_name = fetch_from_cache(1)
    .transform([](const UserProfile& p) -> std::string {
        std::string s = p.name;
        for (auto& c : s) c = std::toupper(c);
        return s;
    });
// upper_name 的类型是 std::optional<std::string>
```

To put it simply: `and_then` is for "the next step might fail" operations (the function returns an `optional`), while `transform` is for "the next step is guaranteed to succeed" transformations (the function returns a plain value).

### or_else: Providing a Fallback

`or_else` calls the provided function when the `optional` is empty, typically used to provide a fallback or log a message:

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

Those of you who have used Rust might feel that C++'s `optional` is a bit "underpowered." That's true, mainly in two aspects:

Rust's `Option<T>` has compiler-enforced `#[must_use]` checks—if you ignore an `Option` return value, the compiler will issue a warning. C++'s `std::optional` doesn't have this guarantee. Although you can use `[[nodiscard]]` to annotate return types, the standard library doesn't do this.

Rust's `Option<T>` has a powerful `?` operator for error propagation. Writing `let val = might_fail()?;` inside a function means that if the `might_fail` returns `None`, the function immediately returns `None`. C++ lacks such elegant syntax; you need to check manually, or use macros to simulate it (like the `TRY` macro mentioned earlier).

However, C++23's monadic operations have largely closed this gap—while chained calls aren't as concise as the `?` operator, they are already quite usable.

------

## Comprehensive Example

Finally, let's look at a more complete example—configuration file parsing—demonstrating how to use `optional` in a real-world scenario:

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
                return std::nullopt;  // 端口无效
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

This example showcases the typical usage of `optional`: using `optional` when looking up fields to indicate "might not exist," using `optional` when parsing numbers to indicate "might fail," and using `value_or` to provide default values. The code is clear, and both the happy path and failure paths are obvious at a glance.

------

## Summary

The positioning of `std::optional` in the realm of error handling is very clear: it is suited for simple scenarios where "failure doesn't need a reason"—lookups, parsing, caching, and default values. If your scenario requires distinguishing between error types, needs an error propagation chain, or requires diagnosing issues at the end of the chain, it's time to switch to `expected` or other heavier-weight solutions.

C++23's monadic operations (`and_then`, `transform`, and `or_else`) make chaining `optional` operations elegant, greatly reducing nested `if/else` code. If your project is still on C++17, writing a few helper functions by hand can achieve a similar effect.

In the next article, we'll look at `std::expected<T, E>`—and see how it handles cases when you need "a value + error information."

## References

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [Monadic operations for std::optional (C++23)](https://en.cppreference.com/w/cpp/utility/optional)
- [P0798R8 - Monadic operations for std::expected](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2505r1.html)
