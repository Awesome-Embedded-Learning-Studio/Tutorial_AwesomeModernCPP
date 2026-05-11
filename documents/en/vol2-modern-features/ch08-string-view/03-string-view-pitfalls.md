---
title: string_view Pitfalls and Best Practices
description: Dangling references, null termination, implicit conversions — common
  pitfalls of string_view and how to avoid them
chapter: 8
order: 3
tags:
- host
- cpp-modern
- intermediate
difficulty: intermediate
platform: host
cpp_standard:
- 17
reading_time_minutes: 15
prerequisites:
- 'Chapter 8: string_view 内部原理'
related:
- string_view 性能分析
---
# string_view Pitfalls and Best Practices

In the previous two articles, we covered the internal mechanics and performance benefits of `string_view`. It seems like a perfect tool—lightweight, fast, and zero-allocation. But we need to offer a reality check: `string_view` is one of the easiest C++ features to use when accidentally introducing undefined behavior (UB). The reason is simple: it does not own the underlying data. The moment you forget this, dangling references, wild pointers, garbled output, and even security vulnerabilities are waiting for you.

In this article, we focus specifically on the pitfalls of `string_view`. We will compile the traps we have fallen into, seen others fall into, and those that static analysis tools can help you catch, followed by a best practices cheat sheet.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Identify all common patterns of `string_view` dangling references
> - [ ] Understand the null termination issue and its impact on C API interoperability
> - [ ] Master the safe usage boundaries of `string_view`
> - [ ] Understand forward-looking information about C++23 `std::zstring_view`

## Pitfall 1: Dangling References—The Number One Killer

`string_view` does not own the underlying data, nor does it extend the lifetime of any object. This is its most fundamental characteristic and the root cause of the vast majority of bugs. Dangling references occur in more scenarios than you might think.

### Returning a view pointing to a temporary string

This is the most classic trap, one that almost every beginner encounters:

```cpp
std::string_view get_name() {
    std::string s = "Alice";
    return std::string_view{s};  // UB！s 在函数返回后销毁
}

int main() {
    auto name = get_name();
    // name 指向已释放的栈内存——未定义行为
    std::cout << name << "\n";  // 可能输出乱码、空字符串、或者 crash
}
```

When the `get_name` function ends, the local variable `s` is destroyed, and its internal character buffer is freed. However, `string_view` still naively points to that memory. This is a typical use-after-free and constitutes undefined behavior (UB)—it might coincidentally work, output garbled text, run fine in debug builds but crash in release builds. The most terrifying outcome is "coincidentally working," because it means the bug will lie dormant for a long time before surfacing.

### Implicit temporary objects are more insidious

In the previous example, you at least explicitly created a local `string`, making it relatively easy to track down. More insidious are temporary objects created implicitly by the compiler:

```cpp
std::string_view sv = std::string("temp");  // UB！临时 string 立刻析构
```

This line of code looks like it is assigning a value to `string_view`, but in reality, `std::string("temp")` is a temporary object that is destroyed as soon as the statement ends. From the moment `sv` is born, it points to freed memory.

Here is a slightly more indirect version:

```cpp
std::string_view trim(std::string_view input) {
    // 去掉前导空格
    while (!input.empty() && input.front() == ' ') {
        input.remove_prefix(1);
    }
    return input;
}

auto result = trim(std::string("  hello"));  // UB！
// trim 参数接收的是临时 string 构造的 view
// 临时 string 在 trim 返回后销毁，result 悬空
```

The problem with this example lies in the call site: the `trim` function's own logic is perfectly correct—it accepts a `string_view` parameter and returns a `string_view`, which is completely fine. The issue is at the call site: a temporary `std::string` is passed in. If the caller passed a string literal (`trim("  hello")`), it would be safe because the lifetime of a literal spans the entire program. But if a temporary `std::string` is passed, the returned `string_view` is left dangling.

⚠️ A hallmark of this type of bug is that it might work fine in debug builds (because the debugger's memory padding patterns might coincidentally allow the dangling view to read the correct data), only to suddenly crash in release builds. We once spent an entire afternoon tracking down such a bug, only to find it was a three-line utility function where the caller passed a temporary `std::string`.

### Indirect reference chains

Sometimes a dangling reference doesn't happen directly, but occurs indirectly through an intermediate layer:

```cpp
class Config {
public:
    void set_value(std::string_view key, std::string_view value) {
        entries_[std::string(key)] = value;  // value 可能指向临时数据
    }

    std::string_view get_value(std::string_view key) const {
        auto it = entries_.find(std::string(key));
        if (it != entries_.end()) {
            return it->second;  // 指向 map 内部的 string，安全
        }
        return {};  // 返回空 view，安全
    }

private:
    std::map<std::string, std::string_view> entries_;  // 危险！value 是 view
};
```

The problem with this `Config` class is that the value type of `entries_` is `std::string_view`. Calling `set_value("host", "localhost")` is safe (because of the literal), but if you write this:

```cpp
Config cfg;
{
    std::string val = "localhost";
    cfg.set_value("host", val);  // val 的 view 被 存入 map
}  // val 销毁，map 中的 view 悬空
auto v = cfg.get_value("host");  // UB！
```

What makes this bug so insidious is that the interface of `set_value` looks perfectly normal, and the caller's code looks perfectly normal too, but the combination causes a failure. The root cause is that `string_view` is stored in a container that needs to hold data long-term, but the underlying data is destroyed before the container.

## Pitfall 2: Null Termination Issues

`string_view` does not guarantee that the underlying data ends with `\0`. We mentioned this in the internals article, but its practical impact is much greater than you might think.

### The fatal combination of data() and C APIs

```cpp
std::string_view sv = "hello, world";
sv.remove_suffix(7);  // sv 变成 "hello,"

// 危险！printf 需要的是 NUL 终止的字符串
std::printf("Value: %s\n", sv.data());  // 未定义行为！
// sv.data() 指向 "hello, world"，但 sv 的长度是 6
// printf 会一直读到遇到 '\0' 为止
// 在这个特殊情况下，因为原始字符串后面有 '\0'，可能"碰巧"工作
// 但这是一个不应该依赖的行为
```

An even more dangerous scenario: when the buffer following what `string_view` points to is not `\0`, but other data:

```cpp
char buf[] = "helloworld";
std::string_view sv(buf, 5);  // "hello"，buf[5] = 'w'，不是 '\0'
std::printf("%s\n", sv.data());  // 输出 "helloworld" 而不是 "hello"
```

`printf` will keep reading until it encounters `\0`, so it outputs the entire `buf` instead of just the first 5 characters of `sv`. This is still a "good case"—if there is no `\0` in the memory following `buf`, `printf` will read out of bounds, potentially crashing or leaking sensitive information from memory.

### The correct approach when NUL termination is required

If your function internally needs to call a C API (`printf`, `fopen`, system calls, etc.) and the data source is a `string_view`, the safest approach is to explicitly construct a `std::string`:

```cpp
void safe_c_api_call(std::string_view sv) {
    // 需要 NUL 终止？构造 string
    std::string str(sv);  // 拷贝，保证 NUL 终止
    std::printf("Value: %s\n", str.c_str());  // 安全
}
```

This introduces a copy, but it is the correct price to pay. If you are using `string_view` for performance, then "conceding" to do a copy where NUL termination is truly needed is far better than writing UB.

### Safety of the std::string constructor

Conversely, constructing a `std::string` from a `string_view` is safe—the constructor of `std::string` correctly handles input without NUL termination (because it has length information):

```cpp
std::string_view sv = "hello\x00world"sv;  // 包含一个 \0，长度 11
std::string s(sv);  // 正确！s 包含所有 11 个字符
```

## Pitfall 3: Implicit Conversion Traps

The implicit conversion from `std::string` to `string_view` is one-way and easy. This is great—it allows you to seamlessly pass a `string` to a function accepting a `string_view`. But the reverse conversion requires explicit action, and sometimes the "implicit" nature itself is a trap.

### string to string_view: Too easy

```cpp
void process(std::string_view sv);

std::string s = "hello";
process(s);  // 隐式转换，很方便

// 但这也行：
process(std::string("temp"));  // 临时 string 构造 view → 传参期间安全
// 如果 process 不存储这个 view，就没问题
// 但如果 process 内部把这个 view 存到了某个地方...
```

The "convenience" of implicit conversion lulls you into a false sense of security. During code review, it can be very hard to notice that a temporary `string` was passed to a `string_view` parameter—because it is syntactically perfectly legal and the compiler won't warn you.

### string_view to string: Must be explicit

`string_view` cannot be implicitly converted to `std::string`; you must construct it explicitly:

```cpp
std::string_view sv = "hello";
std::string s = sv;           // OK，显式构造（其实是隐式的，但概念上是有意的）
std::string s2(sv);           // OK，显式构造
auto s3 = std::string(sv);    // OK

// 但不能这样：
void need_string(const std::string& s);
need_string(sv);  // 编译错误！string_view 不能隐式转为 string
need_string(std::string(sv));  // 必须显式
```

This design is intentional—the conversion from `string_view` to `string` involves heap allocation and character copying, and the compiler doesn't want to perform such an expensive operation without your knowledge.

## Pitfall 4: Functions Returning string_view

A function returning `string_view` is not inherently a problem—provided the data pointed to by the returned view lives long enough. Here is a safe pattern:

```cpp
// 安全：返回指向参数的子视图
std::string_view get_extension(std::string_view filename) {
    auto pos = filename.rfind('.');
    if (pos == std::string_view::npos) {
        return {};
    }
    return filename.substr(pos);  // 指向参数的数据，调用期间有效
}

// 安全：返回指向静态数据的视图
std::string_view get_error_message(int code) {
    static const char kMessages[][32] = {
        "OK",
        "File not found",
        "Permission denied",
        "Out of memory"
    };
    if (code >= 0 && code < 4) {
        return kMessages[code];  // 静态数组，永远有效
    }
    return "Unknown error";
}
```

An unsafe pattern:

```cpp
// 不安全：返回指向局部变量的视图
std::string_view format_name(const char* first, const char* last) {
    std::string full = std::string(first) + " " + last;
    return full;  // UB！full 是局部变量
}
```

A useful rule of thumb is: if a function returns a `string_view`, it must be an observer of some "longer-lived" data. It either points to the parameter's data (valid during the call), points to static storage (valid forever), or points to a member variable (valid during the object's lifetime). If you find a function that creates a new `std::string` internally and then returns its view—that is a bug one hundred percent of the time.

## Pitfall 5: Storing string_view as a Member Variable

Using `string_view` as a class member variable requires extreme caution. A class's lifetime is typically much longer than a function's, but the data pointed to by the `string_view` might be long gone.

```cpp
// 反面教材
class Parser {
public:
    void set_input(std::string_view input) {
        input_ = input;  // 存储了 view
    }

    void parse() {
        // 使用 input_...
        // 如果 input_ 指向的数据已经没了，这里就是 UB
    }

private:
    std::string_view input_;  // 危险！
};
```

If someone calls it like this:

```cpp
Parser p;
{
    std::string data = read_file("config.ini");
    p.set_input(data);  // view 指向 data
}  // data 销毁，p.input_ 悬空
p.parse();  // UB！
```

A better approach is to have the class own the data itself:

```cpp
class SafeParser {
public:
    void set_input(std::string input) {  // 按值传 string，移动语义
        input_ = std::move(input);
    }

    void set_input_view(std::string_view input) {
        input_ = input;  // 拷贝到自己的 string
    }

    void parse() {
        // 安全使用 input_
    }

private:
    std::string input_;  // 自己拥有数据
};
```

Although this introduces an extra copy, it eliminates an entire category of lifetime bugs. In most scenarios, this performance cost is worth it.

## Best Practices Cheat Sheet

We have compiled all the pitfalls and their corresponding workarounds into a table:

| Scenario | Risk | Recommended Practice |
|----------|------|----------------------|
| Function parameters (read-only) | Low | Pass `string_view` by value |
| Function return values | High | Do not return a view pointing to local/temporary data |
| Class member variables | High | Use `std::string` to own data, use `string_view` only for short-term observation |
| Container keys (`unordered_map`) | High | Ensure the underlying string outlives the container, or use `std::string` as the key |
| Calling C APIs | High | Explicitly construct a `std::string`, use `c_str()` |
| Storing `string_view` in a container | High | Only store views pointing to static data, or use `std::string` |
| Async/deferred execution | High | Ensure data lives long enough before capturing `string_view` into a lambda |
| Signal/callback registration | High | A `string_view` in a callback might be executed later; use `std::string` instead |

There is only one core principle: **`string_view` should only be used for short-term, synchronous, read-only access scenarios.** If the data needs to "live longer than the current function call," use `std::string`.

Here are a few more lessons learned from our actual project experience. First, during code review, focus closely on all `string_view` member variables—if there are any, ask: "When will the data it points to be freed?" Second, for all functions accepting a `string_view` parameter, explicitly document in the API that "the parameter must be valid for the duration of the function call." Third, if your project has AddressSanitizer (ASan) enabled, make sure to run your tests under ASan—it can precisely catch use-after-free issues with `string_view`, making it 100 times faster than tracking them down manually. Enabling it is simple: add `-fsanitize=address -fno-omit-frame-pointer` at compile time and `-fsanitize=address` at link time.

```bash
# 开启 ASan 编译
g++ -std=c++17 -O0 -g -fsanitize=address -fno-omit-frame-pointer main.cpp
./a.out
# 如果有 use-after-free，ASan 会打印详细的错误报告
```

## Looking Ahead: C++26 std::zstring_view (Proposal P3655)

The C++ community is also aware of `string_view`'s shortcomings regarding NUL termination. Proposal P3655 suggests introducing `std::zstring_view` (also known as `std::cstring_view`), with the goal of providing a `string_view` variant that guarantees NUL termination. This proposal is currently targeting the C++26 standard and has not been officially released yet.

The design philosophy of `zstring_view` is to add a NUL termination guarantee on top of `string_view`, making it safe to pass to C APIs. It remains non-owning, so lifetime issues still exist, but it at least solves half of the pain points related to NUL termination.

Before `zstring_view` officially enters the standard, if you need similar functionality, you can wrap your own lightweight `zstring_view` class—the core idea is to inherit from (or compose with) `string_view`, check for NUL termination at construction time, and have the `data()` method return a pointer that is guaranteed to be NUL-terminated. But honestly, in most projects, directly using `std::string(sv).c_str()` is sufficient.

## Summary

`string_view` is a double-edged sword. Its performance benefits are real and significant, but its lifetime risks are equally real and severe. Our guiding principle for using it is: feel free to use `string_view` for function parameters (read-only, short-term use), use it cautiously for return values (ensure the pointed-to data lives long enough), strictly avoid storing it as member variables or in containers (unless you are absolutely certain about the data's lifetime), and remember to explicitly convert to a NUL-terminated `std::string` when calling C APIs.

The key to using `string_view` well is not memorizing a bunch of rules, but building an intuition: every time you write `string_view`, your brain should automatically ask one question—"Is the data it points to still alive?"
