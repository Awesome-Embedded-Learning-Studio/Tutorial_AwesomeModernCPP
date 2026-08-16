---
title: "卷 2 Lab 实验参考"
description: "零拷贝配置读取器 Lab 的实验参考：五个步骤加 L5 挑战的逐步解答，每步标注知识点链接，所有输出在 WSL Arch（g++ 16.1.1，-std=c++17）真实运行得到，L5 附 ASan/UBSan 验证与完整源码。"
chapter: 2
order: 4
tags: [host, intermediate, cpp-modern, 移动语义, 类型安全]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 30
prerequisites:
  - "卷 2 Lab：零拷贝配置读取器"
related:
  - "卷 2 全部章节（第 0~11 章）"
---

# 卷 2 Lab 实验参考

> 所有输出在 WSL Arch（g++ 16.1.1，`-std=c++17`）真实运行得到。建议卡住时先看「思路」逐步对照。

## 步骤 1：值类别体检台 {#lab-1}

**思路**：`decltype((expr))` 按值类别求类型——lvalue 得 `T&`、xvalue 得 `T&&`、prvalue 得 `T`，套 `is_lvalue_reference_v`/`is_rvalue_reference_v` 就是一台体检仪。

1. `PROBE` 宏打印 9 个表达式；返回引用的 `get_global()` 是左值、返回值的 `get_temp()` 是 prvalue、赋值表达式 `x = 5` 是左值。→ 知识点：[右值引用：从拷贝到移动](../ch00-move-semantics/01-rvalue-reference.md)「给任意表达式做值类别体检」一节
2. `rr` 是命名右值引用、体检结果是 lvalue——有名字就降级成左值，`std::move` 产出的 xvalue 一被命名就不再自动触发移动，这正是 `std::forward` 要补的洞。→ 知识点：[右值引用：从拷贝到移动](../ch00-move-semantics/01-rvalue-reference.md)（`rref` 反直觉点的解释）

```cpp
// lab1.cpp -- Lab step 1: value category checkup table
// Standard: C++17
#include <iostream>
#include <type_traits>
#include <utility>

template <class T>
constexpr const char* category()
{
    if constexpr (std::is_lvalue_reference_v<T>) {
        return "lvalue";
    } else if constexpr (std::is_rvalue_reference_v<T>) {
        return "xvalue";
    } else {
        return "prvalue";
    }
}

#define PROBE(expr) \
    std::cout << "  " #expr "  ->  " << category<decltype((expr))>() << "\n"

int g = 100;
int& get_global() { return g; }
int get_temp() { return 7; }

int main()
{
    int x = 10;
    int&& rr = std::move(x);

    std::cout << "--- probes ---\n";
    PROBE(x);
    PROBE(rr);
    PROBE(get_global());
    PROBE(get_temp());
    PROBE(std::move(x));
    PROBE(x = 5);
    PROBE(x > 0 ? x : g);
    PROBE("hello");
    PROBE("hello"[0]);

    static_assert(std::is_lvalue_reference_v<decltype((x))>);
    static_assert(std::is_rvalue_reference_v<decltype((std::move(x)))>);
    static_assert(!std::is_reference_v<decltype((get_temp()))>);
    static_assert(std::is_lvalue_reference_v<decltype((get_global()))>);
    static_assert(std::is_lvalue_reference_v<decltype((x = 5))>);
    std::cout << "static_asserts passed\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o lab1 lab1.cpp && ./lab1
--- probes ---
  x  ->  lvalue
  rr  ->  lvalue
  get_global()  ->  lvalue
  get_temp()  ->  prvalue
  std::move(x)  ->  xvalue
  x = 5  ->  lvalue
  x > 0 ? x : g  ->  lvalue
  "hello"  ->  lvalue
  "hello"[0]  ->  lvalue
static_asserts passed
```

## 步骤 2：noexcept 与扩容策略 {#lab-2}

**思路**：`vector` 扩容要搬老元素，搬的过程中搬一半抛异常就回不去了——所以只有 `nothrow_move_constructible` 的类型才敢用移动，否则退回拷贝保强异常安全。`template <bool NoexceptMove>` 让两个版本只差一个 `noexcept` 标记，其余代码逐字相同。

1. `TrackedBuffer` 模板 + 静态计数器。→ 知识点：[移动构造与移动赋值](../ch00-move-semantics/02-move-semantics.md)「noexcept——移动操作的安全承诺」一节
2. 两轮 `reserve(1)` + 两次 `emplace_back` 触发扩容。→ 知识点：[移动语义实战：从 STL 到自定义类型](../ch00-move-semantics/05-move-in-practice.md)（`move_if_noexcept` 的策略）

```cpp
// lab2.cpp -- Lab step 2: noexcept move decides vector reallocation strategy
// Standard: C++17
#include <cstddef>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

template <bool NoexceptMove>
class TrackedBuffer
{
    char* data_;
    std::size_t capacity_;
    std::string tag_;

public:
    static int copy_count;
    static int move_count;

    explicit TrackedBuffer(std::size_t cap, std::string tag)
        : data_(new char[cap])
        , capacity_(cap)
        , tag_(std::move(tag))
    {
    }

    ~TrackedBuffer() { delete[] data_; }

    TrackedBuffer(const TrackedBuffer& other)
        : data_(new char[other.capacity_])
        , capacity_(other.capacity_)
        , tag_(other.tag_)
    {
        ++copy_count;
        std::cout << "  [" << tag_ << "] copy ctor\n";
    }

    TrackedBuffer(TrackedBuffer&& other) noexcept(NoexceptMove)
        : data_(other.data_)
        , capacity_(other.capacity_)
        , tag_(std::move(other.tag_))
    {
        other.data_ = nullptr;
        other.capacity_ = 0;
        ++move_count;
        std::cout << "  [" << tag_ << "] move ctor\n";
    }

    TrackedBuffer& operator=(const TrackedBuffer&) = delete;
    TrackedBuffer& operator=(TrackedBuffer&&) = delete;

    static void reset()
    {
        copy_count = 0;
        move_count = 0;
    }
};

template <bool N>
int TrackedBuffer<N>::copy_count = 0;
template <bool N>
int TrackedBuffer<N>::move_count = 0;

int main()
{
    using NB = TrackedBuffer<true>;
    using TB = TrackedBuffer<false>;

    static_assert(std::is_nothrow_move_constructible_v<NB>);
    static_assert(!std::is_nothrow_move_constructible_v<TB>);

    std::cout << "=== noexcept move + realloc ===\n";
    {
        std::vector<NB> v;
        v.reserve(1);
        v.emplace_back(64, "Noexcept");
        std::cout << "--- trigger realloc ---\n";
        v.emplace_back(64, "Noexcept");
    }
    std::cout << "copy=" << NB::copy_count << " move=" << NB::move_count << "\n\n";

    std::cout << "=== throwing move + realloc ===\n";
    {
        std::vector<TB> v;
        v.reserve(1);
        v.emplace_back(64, "Throwing");
        std::cout << "--- trigger realloc ---\n";
        v.emplace_back(64, "Throwing");
    }
    std::cout << "copy=" << TB::copy_count << " move=" << TB::move_count << "\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o lab2 lab2.cpp && ./lab2
=== noexcept move + realloc ===
--- trigger realloc ---
  [Noexcept] move ctor
copy=0 move=1

=== throwing move + realloc ===
--- trigger realloc ---
  [Throwing] copy ctor
copy=1 move=0
```

要点：两轮输出一个只有 move、一个只有 copy。`vector` 不是「懒得移动」——移动抛异常会把容器留在半搬状态，拷贝失败则老数据完好，这是异常安全的权衡，不是编译器心情。

## 步骤 3：零拷贝 KV 解析核心 {#lab-3}

**思路**：`parse_kv` 全用 `string_view` 的指针/长度操作：`find` 定位 `=`、`substr` 切两半、`remove_prefix/remove_suffix` 做 trim；`optional` 表达「这一截不是合法键值对」。整个解析路径零堆分配——只有 `ostream` 内部可能动堆，与解析逻辑无关。

1. `parse_kv` + trim + `optional` 返回。→ 知识点：[string_view 内部原理：非拥有字符串视图](../ch08-string-view/01-string-view-internals.md)「修改视图本身」一节、[std::optional：优雅表达「可能没有值」](../ch04-type-safety/04-optional.md)
2. 主循环按 `;` 分段、`remove_prefix` 消费。→ 知识点：[string_view 内部原理：非拥有字符串视图](../ch08-string-view/01-string-view-internals.md)「实战：手写一个简单的 token 分割器」一节

```cpp
// lab3.cpp -- Lab step 3: zero-copy key=value parser core
// Standard: C++17
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>

/// Parse "key = value" (spaces optional), trim both sides.
std::optional<std::pair<std::string_view, std::string_view>>
parse_kv(std::string_view entry)
{
    auto pos = entry.find('=');
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    auto key = entry.substr(0, pos);
    auto value = entry.substr(pos + 1);
    auto is_ws = [](char c) { return c == ' ' || c == '\t'; };
    while (!key.empty() && is_ws(key.front())) key.remove_prefix(1);
    while (!key.empty() && is_ws(key.back())) key.remove_suffix(1);
    while (!value.empty() && is_ws(value.front())) value.remove_prefix(1);
    while (!value.empty() && is_ws(value.back())) value.remove_suffix(1);
    if (key.empty()) {
        return std::nullopt;
    }
    return std::make_pair(key, value);
}

int main()
{
    const char* raw = "name = Alice ; age = 30 ; city = Beijing";
    std::string_view input(raw);
    while (!input.empty()) {
        auto semi = input.find(';');
        auto segment = (semi == std::string_view::npos) ? input : input.substr(0, semi);
        if (auto kv = parse_kv(segment)) {
            std::cout << "key=[" << kv->first << "] value=[" << kv->second << "]\n";
        } else {
            std::cout << "skip: [" << segment << "]\n";
        }
        if (semi == std::string_view::npos) {
            break;
        }
        input.remove_prefix(semi + 1);
    }
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o lab3 lab3.cpp && ./lab3
key=[name] value=[Alice]
key=[age] value=[30]
key=[city] value=[Beijing]
```

要点：`raw` 是静态字面量，所有 view 只是指针加减——解析路径堆分配 0 次；`std::string` 版每个 `substr` 都要「分配 + 拷贝 + 释放」一轮，同样三段就是三次。这就是第 8 章说的 O(1) vs O(n) 的天壤之别。

## 步骤 4：类型安全的配置值 {#lab-4}

**思路**：variant 四个备选类型 + `from_chars` 的「全消费」判据做类型归类；三种访问方式各司其职——`index()` 只问位置、`get_if` 免异常取指针、`visit` 编译期穷尽分派。

1. `parse_value` 按「bool → int → double → 字符串」归类。→ 知识点：[std::variant：类型安全的联合体](../ch04-type-safety/03-variant.md)「配置值」一节
2. `index()` / `holds_alternative` / `get_if` / `Overloaded` 访问者四连。→ 知识点：[std::variant：类型安全的联合体](../ch04-type-safety/03-variant.md)「访问值」「使用 lambda 的简单 visit」两节、[结构化绑定：一行解包多个值](../ch05-structured-bindings/01-structured-bindings.md)

```cpp
// lab4.cpp -- Lab step 4: typed config values via variant + visit
// Standard: C++17
#include <charconv>
#include <iostream>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

using ConfigValue = std::variant<int, double, std::string_view, bool>;

struct Entry
{
    std::string_view key;
    ConfigValue value;
};

template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

ConfigValue parse_value(std::string_view sv)
{
    if (sv == "true") return true;
    if (sv == "false") return false;
    int iv = 0;
    auto [p1, ec1] = std::from_chars(sv.data(), sv.data() + sv.size(), iv);
    if (ec1 == std::errc{} && p1 == sv.data() + sv.size()) return iv;
    double dv = 0.0;
    auto [p2, ec2] = std::from_chars(sv.data(), sv.data() + sv.size(), dv);
    if (ec2 == std::errc{} && p2 == sv.data() + sv.size()) return dv;
    return sv;
}

int main()
{
    std::vector<Entry> entries;
    entries.push_back(Entry{"port", parse_value("8080")});
    entries.push_back(Entry{"debug", parse_value("true")});
    entries.push_back(Entry{"ratio", parse_value("0.75")});
    entries.push_back(Entry{"host", parse_value("example.com")});

    for (const auto& [key, value] : entries) {
        // accessor toolkit
        std::cout << "key=" << key
                  << " index=" << value.index()
                  << " holds_string=" << std::holds_alternative<std::string_view>(value) << "\n";
        if (const int* ip = std::get_if<int>(&value)) {
            std::cout << "  get_if<int>: " << *ip << "\n";
        }
        std::visit(Overloaded{
            [](int v) { std::cout << "  visit: int " << v << "\n"; },
            [](double v) { std::cout << "  visit: double " << v << "\n"; },
            [](bool v) { std::cout << "  visit: bool " << (v ? "true" : "false") << "\n"; },
            [](std::string_view v) { std::cout << "  visit: string " << v << "\n"; }
        }, value);
    }
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o lab4 lab4.cpp && ./lab4
key=port index=0 holds_string=0
  get_if<int>: 8080
  visit: int 8080
key=debug index=3 holds_string=0
  visit: bool true
key=ratio index=1 holds_string=0
  visit: double 0.75
key=host index=2 holds_string=1
  visit: string example.com
```

要点：`index()` 是「当前是第几个备选」、`get_if` 免异常、`visit` 走编译期穷尽分派——`Overloaded` 少写一个分支，编译器就直接报错，这是裸 `union` 给不了的安全网。

## 步骤 5：错误传播链 {#lab-5}

**思路**：C++17 没有 `std::expected`，照教材思路自制精简版；先跨过 union 编译坑——匿名 union 里有非平凡成员时默认构造/析构被删除，起名并补一对空的 `Storage()/` `~Storage()`。`and_then` 的穿透逻辑只有一行：有值就继续调 `f`，没值就把错误原样包回新类型的 `expected`。

1. `expected` + `unexpected` 的实现（union 修复在内）。→ 知识点：[std::expected<T, E>：类型安全的错误传播](../ch10-error-handling/03-expected-error.md)「C++17 环境下的简化实现」一节
2. `enum class [[nodiscard]] ParseError`（位置在 `enum class` 关键字之后——放前面 GCC 16 会忽略并警告）+ `parse_port`/`find_field` 两个可能失败的操作。→ 知识点：[标准属性详解：让编译器成为你的代码审查员](../ch07-attributes/01-standard-attributes.md)、[错误处理模式总结：选择指南与最佳实践](../ch10-error-handling/04-error-patterns.md)
3. `and_then` 串链 + 四个输入。→ 知识点：[std::expected<T, E>：类型安全的错误传播](../ch10-error-handling/03-expected-error.md)「and_then：链接可能失败的操作」一节

```cpp
// lab5.cpp -- Lab step 5: error propagation chain with a mini expected
// Standard: C++17
#include <charconv>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

enum class [[nodiscard]] ParseError
{
    kMissingField,
    kNotANumber,
    kOutOfRange,
};

const char* to_string(ParseError e)
{
    switch (e) {
    case ParseError::kMissingField: return "field not found";
    case ParseError::kNotANumber:   return "value is not a number";
    case ParseError::kOutOfRange:   return "value out of range [1,65535]";
    }
    return "unknown";
}

template <typename E>
struct unexpected
{
    E value;
    constexpr explicit unexpected(E v) : value(std::move(v)) {}
};

template <typename T, typename E>
class expected
{
    bool has_value_;
    union Storage
    {
        T val_;
        E err_;
        Storage() {}
        ~Storage() {}
    } storage_;

public:
    expected(const T& v) : has_value_(true) { new (&storage_.val_) T(v); }
    expected(T&& v) : has_value_(true) { new (&storage_.val_) T(std::move(v)); }
    expected(unexpected<E> u) : has_value_(false) { new (&storage_.err_) E(std::move(u.value)); }
    expected(const expected& other) : has_value_(other.has_value_)
    {
        if (has_value_) {
            new (&storage_.val_) T(other.storage_.val_);
        } else {
            new (&storage_.err_) E(other.storage_.err_);
        }
    }
    ~expected()
    {
        if (has_value_) {
            storage_.val_.~T();
        } else {
            storage_.err_.~E();
        }
    }

    constexpr bool has_value() const noexcept { return has_value_; }
    constexpr explicit operator bool() const noexcept { return has_value_; }
    T& operator*() { return storage_.val_; }
    T* operator->() { return &storage_.val_; }
    const E& error() const { return storage_.err_; }

    template <typename F>
    auto and_then(F&& f) -> decltype(f(std::declval<T>()))
    {
        using R = decltype(f(std::declval<T>()));
        if (has_value_) {
            return f(storage_.val_);
        }
        return R(unexpected<E>{storage_.err_});
    }
};

expected<int, ParseError> parse_port(std::string_view sv)
{
    int value = 0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    if (ec != std::errc{} || ptr != sv.data() + sv.size()) {
        return unexpected<ParseError>{ParseError::kNotANumber};
    }
    if (value < 1 || value > 65535) {
        return unexpected<ParseError>{ParseError::kOutOfRange};
    }
    return value;
}

expected<std::string_view, ParseError> find_field(
    std::string_view line, std::string_view key)
{
    auto pos = line.find(key);
    if (pos == std::string_view::npos) {
        return unexpected<ParseError>{ParseError::kMissingField};
    }
    auto rest = line.substr(pos + key.size());
    auto eq = rest.find('=');
    if (eq == std::string_view::npos) {
        return unexpected<ParseError>{ParseError::kMissingField};
    }
    return rest.substr(eq + 1);
}

int main()
{
    auto run = [](std::string_view line) {
        auto result = find_field(line, "port")
            .and_then(parse_port);
        if (result) {
            std::cout << "  OK   \"" << line << "\" -> port " << *result << "\n";
        } else {
            std::cout << "  FAIL \"" << line << "\" -> " << to_string(result.error()) << "\n";
        }
    };
    run("host=localhost;port=8080");
    run("host=localhost");
    run("host=localhost;port=abc");
    run("host=localhost;port=99999");
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o lab5 lab5.cpp && ./lab5
  OK   "host=localhost;port=8080" -> port 8080
  FAIL "host=localhost" -> field not found
  FAIL "host=localhost;port=abc" -> value is not a number
  FAIL "host=localhost;port=99999" -> value out of range [1,65535]
```

要点：`and_then` 里「没值就直接 `return R(unexpected<E>{err_})`」一行就是穿透的全部——错误对象一路拷贝到链尾，后续步骤的 `f` 根本不被调用。`-Wall -Wextra` 编译零警告。

## 附加挑战（L5）：组装零分配 INI 读取器 {#lab-l5}

**思路**：把五个零件接上「文件 → 缓冲 → 逐行 → 链式解析 → visit 打印」的流水线。唯一一次堆分配在 `content.resize(...)`——整份文件读进一个 `std::string` 缓冲；此后每一行、每个键值、每个值分类全部在 `string_view` 上做指针运算，零新增分配。错误链的错误类型升级成 `LineError{行号, 消息}`，让「第几行、错在哪」一次讲清。

1. `unique_ptr<FILE, FcloseDeleter>` + 整文件一次 `fread`。→ 知识点：[自定义删除器与侵入式引用计数](../ch01-smart-pointers/05-custom-deleter.md)、[RAII 深入理解：资源管理的基石](../ch01-smart-pointers/01-raii-deep-dive.md)
2. 逐行：去 `\r`、截注释（`#`/`;`）、trim；空行跳过。→ 知识点：[string_view 内部原理：非拥有字符串视图](../ch08-string-view/01-string-view-internals.md)
3. `parse_kv`/`parse_value` 包装成 `expected` 链，错误带行号；值走 `Overloaded` 访问者。→ 知识点：[错误处理的演进：从错误码到类型安全](../ch10-error-handling/01-error-handling-evolution.md)、[std::variant：类型安全的联合体](../ch04-type-safety/03-variant.md)

```cpp
// lab5l.cpp -- Lab L5: zero-allocation INI reader with error propagation
// Standard: C++17
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <charconv>
#include <system_error>

// ---- RAII file handle (ch01) ----
struct FcloseDeleter
{
    void operator()(FILE* f) const noexcept
    {
        if (f != nullptr) {
            std::fclose(f);
        }
    }
};

// ---- mini expected (ch10) ----
struct LineError
{
    std::size_t line;
    std::string message;
};

template <typename E>
struct unexpected
{
    E value;
    constexpr explicit unexpected(E v) : value(std::move(v)) {}
};

template <typename T, typename E>
class expected
{
    bool has_value_;
    union Storage
    {
        T val_;
        E err_;
        Storage() {}
        ~Storage() {}
    } storage_;

public:
    expected(const T& v) : has_value_(true) { new (&storage_.val_) T(v); }
    expected(T&& v) : has_value_(true) { new (&storage_.val_) T(std::move(v)); }
    expected(unexpected<E> u) : has_value_(false) { new (&storage_.err_) E(std::move(u.value)); }
    expected(const expected& other) : has_value_(other.has_value_)
    {
        if (has_value_) {
            new (&storage_.val_) T(other.storage_.val_);
        } else {
            new (&storage_.err_) E(other.storage_.err_);
        }
    }
    ~expected()
    {
        if (has_value_) {
            storage_.val_.~T();
        } else {
            storage_.err_.~E();
        }
    }
    constexpr bool has_value() const noexcept { return has_value_; }
    constexpr explicit operator bool() const noexcept { return has_value_; }
    T& operator*() { return storage_.val_; }
    T* operator->() { return &storage_.val_; }
    const E& error() const { return storage_.err_; }
    template <typename F>
    auto and_then(F&& f) -> decltype(f(std::declval<T>()))
    {
        using R = decltype(f(std::declval<T>()));
        if (has_value_) {
            return f(storage_.val_);
        }
        return R(unexpected<E>{storage_.err_});
    }
};

// ---- config value (ch04) ----
using ConfigValue = std::variant<int, double, std::string_view, bool>;

template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

expected<ConfigValue, LineError> parse_value(std::string_view sv, std::size_t line)
{
    if (sv == "true") return ConfigValue{true};
    if (sv == "false") return ConfigValue{false};
    int iv = 0;
    auto [p1, ec1] = std::from_chars(sv.data(), sv.data() + sv.size(), iv);
    if (ec1 == std::errc{} && p1 == sv.data() + sv.size()) return ConfigValue{iv};
    double dv = 0.0;
    auto [p2, ec2] = std::from_chars(sv.data(), sv.data() + sv.size(), dv);
    if (ec2 == std::errc{} && p2 == sv.data() + sv.size()) return ConfigValue{dv};
    if (sv.empty()) {
        return unexpected<LineError>{LineError{line, "empty value"}};
    }
    return ConfigValue{sv};
}

expected<std::pair<std::string_view, std::string_view>, LineError>
parse_kv(std::string_view entry, std::size_t line)
{
    auto pos = entry.find('=');
    if (pos == std::string_view::npos) {
        return unexpected<LineError>{LineError{line, "missing '='"}};
    }
    auto key = entry.substr(0, pos);
    auto value = entry.substr(pos + 1);
    auto is_ws = [](char c) { return c == ' ' || c == '\t'; };
    while (!key.empty() && is_ws(key.front())) key.remove_prefix(1);
    while (!key.empty() && is_ws(key.back())) key.remove_suffix(1);
    while (!value.empty() && is_ws(value.front())) value.remove_prefix(1);
    while (!value.empty() && is_ws(value.back())) value.remove_suffix(1);
    if (key.empty()) {
        return unexpected<LineError>{LineError{line, "empty key"}};
    }
    return std::make_pair(key, value);
}

int main(int argc, char** argv)
{
    const char* path = (argc > 1) ? argv[1] : "sample.ini";
    std::unique_ptr<FILE, FcloseDeleter> file(std::fopen(path, "rb"));
    if (file == nullptr) {
        std::cerr << "cannot open " << path << "\n";
        return 1;
    }

    // one allocation: read the whole file into a string, then parse zero-copy
    std::string content;
    {
        std::fseek(file.get(), 0, SEEK_END);
        long size = std::ftell(file.get());
        std::fseek(file.get(), 0, SEEK_SET);
        content.resize(static_cast<std::size_t>(size));
        if (size > 0) {
            std::fread(content.data(), 1, static_cast<std::size_t>(size), file.get());
        }
    }

    std::string_view input = content;
    std::size_t line_no = 0;
    while (!input.empty()) {
        auto nl = input.find('\n');
        std::string_view line = (nl == std::string_view::npos) ? input : input.substr(0, nl);
        ++line_no;

        // strip trailing \r and comments
        while (!line.empty() && (line.back() == '\r')) line.remove_suffix(1);
        auto hash = line.find_first_of("#;");
        if (hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        auto is_ws = [](char c) { return c == ' ' || c == '\t'; };
        while (!line.empty() && is_ws(line.front())) line.remove_prefix(1);
        while (!line.empty() && is_ws(line.back())) line.remove_suffix(1);

        if (line.empty()) {
            // blank or comment-only line
        } else {
            auto kv = parse_kv(line, line_no);
            if (!kv) {
                std::cout << "line " << kv.error().line << ": ERROR: "
                          << kv.error().message << " in [" << line << "]\n";
            } else {
                auto value = parse_value(kv->second, line_no);
                if (!value) {
                    std::cout << "line " << value.error().line << ": ERROR: "
                              << value.error().message << " in [" << line << "]\n";
                } else {
                    std::cout << kv->first << " = ";
                    std::visit(Overloaded{
                        [](int v) { std::cout << v << " (int)"; },
                        [](double v) { std::cout << v << " (double)"; },
                        [](bool v) { std::cout << (v ? "true" : "false") << " (bool)"; },
                        [](std::string_view v) { std::cout << v << " (string)"; }
                    }, *value);
                    std::cout << "\n";
                }
            }
        }
        if (nl == std::string_view::npos) {
            break;
        }
        input.remove_prefix(nl + 1);
    }
    return 0;
}
```

**验证输出**（`sample.ini` 内容与两次运行）：

```text
$ cat sample.ini
# demo config
host = example.com
port = 8080
debug = true
ratio = 0.75
; comment only line
missing_equals
empty_value =
name = alpha

$ g++ -std=c++17 -Wall -Wextra -O2 -o lab5l lab5l.cpp && ./lab5l sample.ini
host = example.com (string)
port = 8080 (int)
debug = true (bool)
ratio = 0.75 (double)
line 7: ERROR: missing '=' in [missing_equals]
line 8: ERROR: empty value in [empty_value =]
name = alpha (string)

$ g++ -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined -o lab5l_asan lab5l.cpp
$ ./lab5l_asan sample.ini
（输出与上面完全一致，无任何 sanitizer 报告）
```

要点：第 7 行缺 `=`、第 8 行空值，错误信息都带上了行号和原文——`LineError` 把「错在哪一行、那一行长什么样」沿 expected 链一路带出来。唯一一次堆分配在 `content.resize(...)`：整份文件只花一次分配读进内存，之后所有键值、trim、类型归类全是 view 的指针运算，ASan/UBSan 全程零报告。到这里，步骤 1 的「view 不拥有」、步骤 4 的「variant 管类型」、步骤 5 的「expected 管错误」在同一台机器上会师了。
