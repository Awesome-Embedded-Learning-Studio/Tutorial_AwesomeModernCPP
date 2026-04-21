---
title: "optional 用于错误处理"
description: "用 std::optional 表示'可能失败的操作'，替代错误码和异常"
chapter: 10
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - optional
  - 类型安全
difficulty: intermediate
platform: host
cpp_standard: [17, 23]
reading_time_minutes: 15
prerequisites:
  - "Chapter 10: 错误处理的演进"
  - "Chapter 4: std::optional"
related:
  - "std::expected"
---

# optional 用于错误处理

在上一篇里我们梳理了 C++ 错误处理的演进路线，最后提到 `std::optional` 可以用于表达"可能失败的操作"。这一篇我们就来深入看看，`optional` 在错误处理场景下到底好不好用、该怎么用、以及什么时候不该用它。

先说结论：`std::optional` 是一把精确的手术刀，不是瑞士军刀。它在特定场景下非常好用，但如果拿来当通用的错误处理工具，你会发现自己到处都在猜"为什么返回了 nullopt"。

------

## optional 的语义：成功或无值

`std::optional<T>` 的语义非常直白——它要么持有一个 `T` 类型的值，要么是空的（`std::nullopt`）。把它用在错误处理上，就是"成功返回值，失败返回空"：

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

这种写法最大的好处是**语义在类型里**。函数签名 `std::optional<int>` 就已经告诉调用方"这个函数可能不返回值"，你不需要查文档、不需要记约定——类型本身就是文档。调用方拿到返回值后，第一件事自然是检查有没有值：

```cpp
auto result = parse_int("42");
if (result) {
    std::cout << "Got: " << *result << "\n";
} else {
    std::cout << "Parse failed\n";
}
```

------

## 适合 optional 的场景

`optional` 最适合的场景有一个共同特征：**失败是正常情况的一部分，而且调用方不需要知道失败的具体原因**。

### 场景一：查找操作

查找是最经典的 optional 场景。从容器中查找一个元素，找不到不是"错误"，而是"没找到"——这个区别很重要。你不需要告诉调用方"为什么没找到"，因为原因只有一个：不存在。

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

### 场景二：解析操作

从外部输入（配置文件、用户输入、网络数据）中解析信息，失败是家常便饭。如果调用方只需要知道"解析成功了吗"，`optional` 就够了：

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

### 场景三：带默认值的场景

当操作失败时你有合理的默认值，`optional` 的 `value_or` 可以让代码非常简洁：

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

### 场景四：缓存查找

缓存命中就返回值，未命中就返回空——这不需要任何错误信息：

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

## 不适合 optional 的场景

`optional` 的致命局限是**不携带错误信息**。当调用方需要知道"为什么失败"时，`optional` 就不够用了。

### 需要区分多种错误类型

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

这种情况应该用 `std::expected<Config, ConfigError>` 或者一个携带错误信息的返回结构体。

### 需要错误传播链

当你需要把多个可能失败的操作串联起来，并且在链的末端知道是哪一步失败了，`optional` 会让调试变得非常痛苦。每一步失败都变成 `nullopt`，到了最后你只知道"某个地方失败了"，但不知道是哪里。

------

## C++23 的 monadic 操作

C++23 为 `std::optional` 新增了三个 monadic 成员函数：`and_then`、`transform`、`or_else`。这三个操作让 `optional` 的链式处理变得优雅得多。

### and_then：链接可能失败的操作

`and_then` 接受一个函数，该函数接受 `optional` 内部的值并返回一个新的 `optional`。如果原始 `optional` 为空，直接返回空，不调用函数：

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

对比一下没有 monadic 操作时的写法：

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

monadic 版本把"正常路径"放在一条链上，每个步骤都清楚地表达了"拿到数据后做什么"。错误传播是自动的——任何一步返回空，后续步骤全部跳过。

### transform：对值做变换

`transform` 和 `and_then` 的区别在于，传入 `transform` 的函数返回一个普通值（不是 `optional`），`transform` 会自动把结果包回 `optional`：

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

一句话区分：`and_then` 用于"下一步可能失败"的操作（函数返回 `optional`），`transform` 用于"下一步一定成功"的变换（函数返回普通值）。

### or_else：提供备选方案

`or_else` 在 `optional` 为空时调用传入的函数，通常用于提供回退方案或记录日志：

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

## 与 Rust Option 的对比

用过 Rust 的朋友可能觉得 C++ 的 `optional` 有点"不够力"。确实如此，主要体现在两个方面：

Rust 的 `Option<T>` 有编译器的 `#[must_use]` 检查——如果你忽略了一个 `Option` 返回值，编译器会发出警告。C++ 的 `std::optional` 没有这个保证，虽然你可以用 `[[nodiscard]]` 标注返回类型，但标准库并没有这么做。

Rust 的 `Option<T>` 有一个强大的 `?` 操作符用于错误传播。在函数里写 `let val = might_fail()?;`，如果 `might_fail` 返回 `None`，函数立即返回 `None`。C++ 没有这么优雅的语法，你需要手动检查，或者用宏来模拟（比如前面提到的 `TRY` 宏）。

不过 C++23 的 monadic 操作已经在很大程度上弥补了这个差距——链式调用虽然不如 `?` 操作符简洁，但已经足够好用了。

------

## 通用示例

最后来看一个比较完整的例子——配置文件解析，展示 `optional` 在真实场景下的使用方式：

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

这个例子展示了 `optional` 的典型用法：查找字段时用 `optional` 表示"可能不存在"，解析数值时用 `optional` 表示"可能失败"，用 `value_or` 提供默认值。代码清晰，正常路径和失败路径一目了然。

------

## 小结

`std::optional` 在错误处理领域的定位很明确：它适合那些"失败不需要原因"的简单场景——查找、解析、缓存、默认值。如果场景需要区分错误类型、需要错误传播链、或者需要在链末端诊断问题，就该换 `expected` 或者其他更重的方案了。

C++23 的 monadic 操作（`and_then`、`transform`、`or_else`）让 `optional` 的链式处理变得优雅，大大减少了嵌套的 `if/else` 代码。如果你的项目还在 C++17，手写几个辅助函数也能达到类似效果。

下一篇我们就来看看 `std::expected<T, E>` —— 当你需要"值 + 错误信息"时，它是怎么做的。

## 参考资源

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [Monadic operations for std::optional (C++23)](https://en.cppreference.com/w/cpp/utility/optional)
- [P0798R8 - Monadic operations for std::expected](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2505r1.html)
