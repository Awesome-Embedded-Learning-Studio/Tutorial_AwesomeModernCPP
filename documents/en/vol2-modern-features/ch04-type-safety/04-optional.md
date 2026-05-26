---
title: 'std::optional: Elegantly Expressing "Possibly No Value'
description: Use `optional` instead of special values and raw pointers to safely express
  optional semantics.
chapter: 4
order: 4
tags:
- host
- cpp-modern
- intermediate
- optional
- 类型安全
difficulty: intermediate
platform: host
cpp_standard:
- 17
- 23
reading_time_minutes: 15
prerequisites:
- 'Chapter 4: std::variant'
related:
- 错误处理的现代方式
translation:
  source: documents/vol2-modern-features/ch04-type-safety/04-optional.md
  source_hash: fe490675dabaaabd0a7c1c6190f92fbacce31d9cda001a374e1daeab2cdddf21
  translated_at: '2026-05-26T11:28:33.448671+00:00'
  engine: anthropic
  token_count: 2839
---
# std::optional: Elegantly Expressing "Maybe No Value"

## Introduction

I have written far too much code like this: a function returns `-1` to mean "not found," returns `nullptr` to mean "an error occurred," or returns an empty string to mean "the configuration item does not exist." These conventions feel perfectly natural when we write them, but looking back three months later sends a chill down the spine—does `-1` mean "not found" or "actually returned -1"? Is `nullptr` an "optional empty value" or "an error"? Every function that returns a special value is laying a trap for our future selves.

`std::optional` (introduced in C++17) exists to solve the problem of "how to safely express that a value might be absent." It encodes the "has a value or does not have a value" information directly into the type system—both the compiler and the caller can see from the function signature that "this return value might be empty," without relying on comments or documentation to convey this.

## Step 1 — Traditional Approaches to "Maybe No Value"

Before `std::optional` appeared, C++ programmers mainly relied on the following approaches to express "maybe no value":

**Special values (sentinel values)**: Use a specific value to represent "invalid." `-1` indicates a failed search, `UINT_MAX` indicates an invalid index, and an empty string indicates an unconfigured item. The problem is that the "special value" differs for every function, forcing the caller to remember these conventions. Furthermore, some types simply lack a suitable special value—for example, `-1.0` of a `double` could perfectly well be a legitimate return value.

**Raw pointers**: Return `nullptr` to mean "no value." This is common in lookup functions. The problem is that pointer semantics are too broad. `T*` can mean "an optional value that might be null," "a non-owning observer pointer," or "a pointer to a dynamically allocated object." The caller cannot distinguish these semantics from the type alone. Even more dangerously, dereferencing a null pointer is UB, which gives no friendly error提示.

**std::pair<T, bool>**: The second element indicates whether the value is valid. This is slightly better than the previous two approaches, but it is very verbose to use—we have to check `.second` every time, and the value of `first` when `second == false` is undefined (default construction might not even be valid).

```cpp
// 三种传统方案对比
int find_index_old(const std::vector<int>& v, int target)
{
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
        if (v[i] == target) return i;
    }
    return -1;  // 特殊值约定：调用方必须记住 -1 表示没找到
}

int* find_ptr_old(std::vector<int>& v, int target)
{
    for (auto& x : v) {
        if (x == target) return &x;
    }
    return nullptr;  // 裸指针：语义不明确
}

std::pair<int, bool> find_pair_old(const std::vector<int>& v, int target)
{
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
        if (v[i] == target) return {i, true};
    }
    return {0, false};  // first 的值在此处无意义
}
```

These three approaches share a common flaw: **the type signature does not express the "maybe no value" semantics**. The return type of `int` will not tell you that `-1` is a special value, and `int*` will not tell you that `nullptr` means "not found" rather than "an error occurred." `std::optional` solves this problem directly at the type level.

## Step 2 — Core Semantics and API of optional

`std::optional` represents "either holding a value of type `T`, or holding nothing at all." It is a value type (not a pointer), and the held object is stored directly within the internal storage of `std::optional`—there is no dynamic memory allocation.

### Construction

```cpp
#include <optional>
#include <string>
#include <iostream>

std::optional<int> a;                      // 空（不持有值）
std::optional<int> b = 42;                 // 持有 42
std::optional<int> c = std::nullopt;       // 显式空
std::optional<std::string> d = "hello";    // 持有 "hello"

// 就地构造（避免临时对象）
std::optional<std::string> e(std::in_place, 10, 'x');  // "xxxxxxxxxx"
```

### Checking and Accessing

```cpp
std::optional<int> opt = 42;

// 检查是否有值
if (opt.has_value()) { /* ... */ }
if (opt) { /* ... */ }             // 等价的隐式 bool 转换

// 访问值
int x = *opt;                       // 解引用（未检查——空时是 UB）
int y = opt.value();                // 空时抛 std::bad_optional_access
int z = opt.value_or(0);            // 空时返回默认值 0

// 访问成员（对于类类型）
std::optional<std::string> name = "Alice";
if (name) {
    std::cout << "length: " << name->size() << "\n";  // operator->
}
```

⚠️ Regarding the choice between `operator*` and `value()`, my recommendation is: in code paths where you have **already checked** `has_value()`, using `*opt` is sufficient—it offers better performance and clearer semantics. When you have **not checked**, `value()` is safer—it throws an exception rather than resulting in UB. However, neither approach is as elegant as `value_or()`, since the latter directly handles the "what to do when empty" question.

### The Magic of value_or

`value_or()` is one of the most practical APIs of `optional`. It accepts a default value parameter: if `optional` has a value, it returns the held value; otherwise, it returns the default value:

```cpp
std::optional<std::string> get_config(const std::string& key);

// 读取配置，未配置则使用默认值
std::string host = get_config("server_host").value_or("localhost");
int port = get_config("server_port")
    .transform([](const std::string& s) { return std::stoi(s); })
    .value_or(8080);
```

The `transform` above is a C++23 feature, which we will cover in detail later.

## Step 3 — Memory Layout of optional

The internal storage of `optional<T>` typically consists of two parts: an aligned buffer for storing `T`, plus a `bool` flag indicating whether a value is present. This means `sizeof(std::optional<T>)` is generally larger than `sizeof(T)`.

```cpp
#include <optional>

std::cout << "sizeof(int):              " << sizeof(int) << "\n";            // 4
std::cout << "sizeof(optional<int>):    " << sizeof(std::optional<int>) << "\n";    // 典型：8
std::cout << "sizeof(double):           " << sizeof(double) << "\n";         // 8
std::cout << "sizeof(optional<double>): " << sizeof(std::optional<double>) << "\n"; // 典型：16
std::cout << "sizeof(string):           " << sizeof(std::string) << "\n";    // 典型：32
std::cout << "sizeof(optional<string>): " << sizeof(std::optional<std::string>) << "\n"; // 典型：40
```

The actual `sizeof` result depends on the standard library implementation and the platform's alignment requirements. But the core fact is: `optional<T>` is roughly larger than `T` by the size of an aligned `bool`. Due to alignment requirements, the increase is sometimes more than expected. This is not a design flaw in `optional`—it stores the `T` value directly on the stack without involving heap allocation, so this extra overhead is reasonable.

The object held by `optional` and the "has value" flag reside inside the same object, without any dynamic memory allocation. Upon destruction, if `optional` holds a value, the destructor of `T` is automatically called. All of this is automatic and requires no manual management.

## Step 4 — Differences Between optional and Pointers

Both `optional<T>` and `T*` can express "maybe no value," but their semantics are fundamentally different.

`optional<T>` has value semantics—it holds (or intends to hold) a complete `T` object. Copying `optional` copies the `T` value (if present), and destroying `optional` destroys the `T`. It expresses "there is a `T` here, or temporarily there is not."

`T*` has reference semantics—it points to some external `T` object (or is null). Copying a pointer only copies the address, not the object itself. It expresses "there is a `T` somewhere, and I might point to it."

```cpp
std::optional<int> opt = 42;
int* ptr = &opt.value();  // 指向 optional 内部的 int

opt = 123;                // optional 重新赋值，旧的 42 被销毁
// ptr 现在可能指向 123（取决于实现），也可能悬空——不要这么用

std::optional<int> opt2 = opt;  // 拷贝：opt2 是独立的副本，持有 123
int* ptr2 = &raw;               // 假设 raw 是某个 int 变量
std::optional<int> opt3 = *ptr2;  // 拷贝 ptr2 指向的值——与 ptr2 无关
```

My general principle is: **if you need to express "a value might or might not exist," use `optional`; if you need to express "a nullable reference to an external object," use a pointer**. Do not use `optional` to simulate pointers, and do not use pointers to simulate `optional`—their responsibilities are different.

## Step 5 — optional as a Return Value

The most common use of `optional` is as a function return value. Its semantics are very clear: the function might return a valid value, or it might return "no value." The caller must handle the "no value" case at the type system level.

### Lookup Operations

```cpp
#include <optional>
#include <vector>
#include <string>

std::optional<std::size_t> find_index(
    const std::vector<int>& v, int target)
{
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == target) return i;
    }
    return std::nullopt;
}

// 调用方
auto idx = find_index(data, 42);
if (idx) {
    std::cout << "found at index " << *idx << "\n";
} else {
    std::cout << "not found\n";
}
```

Compared to the previous version using `-1` as a sentinel value, the advantage of `optional` is that the caller **cannot forget** to check the return value. If you directly write `data[*find_index(data, 42)]` without checking `has_value()`, dereferencing in the empty case is UB, but at least the API's design intent is clear—the type signature has already told you "this value might be empty."

### Factory Functions

```cpp
class Connection {
public:
    static std::optional<Connection> create(const std::string& addr)
    {
        // 尝试建立连接
        if (addr.empty()) return std::nullopt;  // 无效参数
        // ... 实际连接逻辑
        return Connection(addr);
    }

private:
    explicit Connection(std::string addr) : addr_(std::move(addr)) {}
    std::string addr_;
};

// 使用
auto conn = Connection::create("192.168.1.1");
if (conn) {
    // 连接成功
} else {
    // 连接失败
}
```

## Step 6 — optional as a Parameter

`optional` can also be used as a function parameter to indicate "this parameter is optional." This is more flexible than function overloading or default parameters, because the caller can decide at runtime whether to provide a value:

```cpp
void print_greeting(const std::string& name,
                    std::optional<std::string> title = std::nullopt)
{
    if (title) {
        std::cout << "Hello, " << *title << " " << name << "!\n";
    } else {
        std::cout << "Hello, " << name << "!\n";
    }
}

print_greeting("Alice");                    // Hello, Alice!
print_greeting("Bob", std::string("Dr."));  // Hello, Dr. Bob!
```

However, I should point out one thing: do not overuse `optional` parameters. If a parameter needs to be provided in most cases, using a default value might be more appropriate than `optional`. `optional` parameters are best suited for scenarios where "sometimes it is present, sometimes it is not, and the meaning of the two cases is completely different."

## Step 7 — Preview of C++23 Monadic Operations

C++23 introduces three monadic operations for `std::optional`: `and_then`, `transform`, and `or_else`. These operations borrow concepts from functional programming, making the chained processing of `optional` much more elegant.

### transform: Transforming the Value

`transform` accepts a function. If `optional` has a value, it uses this function to transform the value and returns an `optional` containing the transformed result; if `optional` is empty, it returns an empty `optional`.

```cpp
std::optional<int> parse_int(const std::string& s)
{
    try {
        return std::stoi(s);
    } catch (...) {
        return std::nullopt;
    }
}

// C++20 风格：手动检查
std::optional<std::string> input = get_input();
std::optional<int> result;
if (input) {
    result = parse_int(*input);
}

// C++23 风格：链式 transform
auto result2 = get_input().transform([](const std::string& s) -> int {
    return std::stoi(s);  // 简化示例，实际应处理异常
});
```

### and_then: Chaining Operations That Might Fail

`and_then` accepts a function that returns an `optional`. If the current `optional` has a value, it calls this function and returns its result; otherwise, it directly returns an empty `optional`. This is more suitable than `transform` for scenarios where "the result of the previous step is the input to the next step, and each step might fail."

```cpp
std::optional<User> find_user(int id);
std::optional<std::string> get_email(const User& u);

// C++20 风格：嵌套 if
auto user = find_user(42);
if (user) {
    auto email = get_email(*user);
    if (email) {
        std::cout << "Email: " << *email << "\n";
    }
}

// C++23 风格：链式 and_then
find_user(42)
    .and_then(get_email)
    .transform([](const std::string& email) {
        std::cout << "Email: " << email << "\n";
        return email;
    });
```

### or_else: Handling the Empty Case

`or_else` accepts a function that is called when `optional` is empty. It is typically used for logging or providing an alternative:

```cpp
auto email = find_user(42)
    .and_then(get_email)
    .or_else([] {
        std::cerr << "Failed to get email\n";
        return std::optional<std::string>("fallback@example.com");
    });
```

Combining these three operations allows us to write very fluent chained code, avoiding deeply nested `if` statements. If your compiler does not yet support C++23, you can refer to the previously mentioned `optional_map` helper function to achieve a similar effect.

## Practical Application — Lazy Initialization

`optional` can also be used to implement lazy initialization: deferring the construction of an object until it is actually needed. This is very useful when object construction is expensive, but whether it is needed cannot be determined at compile time:

```cpp
class ExpensiveResource {
public:
    ExpensiveResource() { /* 耗时的初始化 */ }
    void do_work() { /* ... */ }
};

class Service {
public:
    void process()
    {
        if (!resource_) {
            resource_.emplace();  // 首次使用时才构造
        }
        resource_->do_work();
    }

private:
    std::optional<ExpensiveResource> resource_;  // 初始为空
};
```

This is superior to implementing lazy initialization with `std::unique_ptr`, because `optional` does not involve heap allocation—the object is stored directly in the internal buffer of `optional`.

## Embedded Practical Application — Configuration Items and Sensor Reading

In embedded systems, sensor data cannot always be read successfully (the sensor might not be ready, the bus might time out), and configuration items do not always exist. `optional` can elegantly express these "might fail" operations:

```cpp
#include <optional>
#include <cstdint>

struct SensorReading {
    float temperature;
    uint32_t timestamp;
};

class TemperatureSensor {
public:
    std::optional<SensorReading> read()
    {
        if (!is_ready()) return std::nullopt;

        SensorReading r;
        r.temperature = read_raw_value() * kScale;
        r.timestamp = get_tick();
        return r;
    }

private:
    bool is_ready();
    float read_raw_value();
    uint32_t get_tick();

    static constexpr float kScale = 0.0625f;
};

// 使用
void print_temperature(TemperatureSensor& sensor)
{
    auto reading = sensor.read();
    if (reading) {
        std::printf("Temp: %.1f C (at %u)\n",
                    reading->temperature,
                    static_cast<unsigned>(reading->timestamp));
    } else {
        std::printf("Sensor not ready\n");
    }
}
```

The value of `optional` in this scenario is that it encodes "read failure" as part of the return type. The caller cannot forget to handle the "read failure" case—because we must check `has_value()` before accessing the temperature value. This is much safer than returning a `0.0f` and relying on the caller to "remember that 0.0 might indicate failure."

## Summary

`std::optional` is the standard way in C++17 to express "maybe no value." It is safer than sentinel values (no confusion with legitimate values), has clearer semantics than raw pointers (value semantics vs. reference semantics), and is more elegant than `std::pair<T, bool>` (the API is specifically designed for this purpose).

The core API of `optional` is very concise: `has_value()` for checking, `operator*` for dereferencing, and `value_or()` for providing a default value. It involves no dynamic memory allocation, and objects are stored directly inside `optional`. C++23's `transform`, `and_then`, and `or_else` provide more elegant syntax for chained processing.

The key principle for using `optional` is: use it to express the "absence of a value" semantics, not "an error occurred" semantics. If we need to pass error information (error codes, error descriptions), please use `std::expected` (C++23) or a custom `Result` type. `optional` is only responsible for "whether there is a value," not "why there is no value."

In the next article, we will discuss `std::any`, which belongs to the same family as `optional`—"can hold some kind of value or hold nothing at all"—but `any` is more powerful and comes with a greater cost.

## Reference Resources

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [cppreference: std::bad_optional_access](https://en.cppreference.com/w/cpp/utility/optional/bad_optional_access)
- [C++23 Monadic operations for std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [C++ Core Guidelines: Optional](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
