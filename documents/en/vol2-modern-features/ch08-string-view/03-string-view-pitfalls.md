---
chapter: 8
cpp_standard:
- 17
description: Dangling references, null termination, implicit conversions — the common
  pitfalls of string_view and how to avoid them
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'string_view Internals: A Non-Owning String View'
reading_time_minutes: 14
related:
- string_view Performance Analysis
tags:
- host
- cpp-modern
- intermediate
title: string_view Pitfalls and Best Practices
translation:
  source: documents/vol2-modern-features/ch08-string-view/03-string-view-pitfalls.md
  source_hash: b37b2d3d05c91ab1bed6dd8cdfe8621142ee3e9b261d34e51a11580053082298
  translated_at: '2026-11-23T01:11:40+00:00'
  engine: glm-5.3-fence-fix
  token_count: 9792
---
# string_view Pitfalls and Best Practices

In the previous two articles we covered how `string_view` works internally and the performance it buys you, and it looks like the perfect tool — lightweight, fast, zero allocations. But we have to pour a bucket of cold water over it here: `string_view` is, of all the C++ features we have ever used, one of the easiest for writing undefined behavior. The reason is simple: it does not own the data. The moment you forget that, dangling references, wild pointers, garbled output, or even security vulnerabilities may be waiting for you.

This article is dedicated entirely to the pitfalls of `string_view`. We will line up the traps we have stepped in ourselves, the ones we have watched others step in, and the ones static analysis tools can catch for you, and close with a best-practices quick-reference table.

## Pitfall 1: Dangling References — the Number One Killer

`string_view` does not own the underlying data, nor does it extend any object's lifetime. That is its most essential trait — and the root of the vast majority of bugs. Dangling references arise in more scenarios than you would think.

### Returning a View to a Temporary string

This is the classic trap; almost every beginner walks into it exactly once:

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

When `get_name` finishes, the local variable `s` is destroyed and its internal character buffer is released. But the `string_view` is still naively pointing at that memory. This is a textbook use-after-free, undefined behavior — it may happen to work, it may print garbage, it may run fine in a debug build and crash in release. The scariest outcome is "happens to work", because that means the bug stays latent for a long time before it surfaces.

### Implicit Temporary Objects Are More Insidious

In the example above you at least created a local `string` yourself, which makes the bug reasonably easy to track down. More insidious are the temporaries the compiler creates for you:

```cpp
std::string_view sv = std::string("temp");  // UB！临时 string 立刻析构
```

This line looks like it is assigning to a `string_view`, but `std::string("temp")` is a temporary object that gets destroyed at the end of the statement. From the very moment it is born, `sv` points to freed memory.

We turned this process into an animation — you can play it and step through: after the temporary object is destroyed, the arrow still hovers over the same spot; the second half shows the correct way, where the variable is materialized first:

<Anim id="dangling-view" />

Now a slightly more indirect version:

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

The problem in this example is not the `trim` function itself — its logic is correct: it takes a `string_view` parameter and returns a `string_view`, nothing wrong there. The problem is at the call site: a temporary `std::string` was passed in. If the caller passes a string literal (`trim("  hello")`), it is safe, because a literal lives for the entire program. But if a temporary `std::string` is passed in, the returned `string_view` dangles.

The hallmark of this class of bug: it may work fine in a debug build (because the debugger's memory fill pattern may happen to leave the dangling view reading correct data), then suddenly crash in the release build. We once spent an entire afternoon hunting one of these down, only to find it was a three-line utility function — the caller had passed in a temporary `std::string`.

### Indirect Reference Chains

Sometimes the dangling reference does not happen directly, but indirectly through an intermediate layer:

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

The problem with this `Config` class is that the value type of `entries_` is `std::string_view`. `set_value("host", "localhost")` is safe at the moment of the call (literals), but if you write this:

```cpp
Config cfg;
{
    std::string val = "localhost";
    cfg.set_value("host", val);  // val 的 view 被 存入 map
}  // val 销毁，map 中的 view 悬空
auto v = cfg.get_value("host");  // UB！
```

What makes this bug sneaky is that `set_value`'s interface looks perfectly normal and the caller's code looks perfectly normal — it is the combination that breaks. The root cause is that the `string_view` got stored into a container that needs to hold the data long-term, while the underlying data was destroyed before the container.

## Pitfall 2: The null Termination Problem

`string_view` does not guarantee the underlying data ends with `\0`. We already mentioned this in the internals article, but its practical impact is far bigger than you might think.

### The Deadly Combination of data() and C APIs

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

The far more dangerous scenario: the memory right after the buffer the `string_view` points at holds not `\0`, but other data:

```cpp
char buf[] = "helloworld";
std::string_view sv(buf, 5);  // "hello"，buf[5] = 'w'，不是 '\0'
std::printf("%s\n", sv.data());  // 输出 "helloworld" 而不是 "hello"
```

`printf` keeps reading until it encounters a `\0`, so it printed the entire `buf` instead of the first 5 characters of `sv`. And this still counts as the "good case" — if the memory after `buf` contains no `\0` at all, `printf` reads out of bounds and may eventually crash or leak sensitive data from memory.

### The Right Way When NUL Termination Is Required

If your function internally needs to call a C API (`printf`, `fopen`, system calls, and so on) and the data arrives as a `string_view`, the safest approach is to explicitly construct a `std::string`:

```cpp
void safe_c_api_call(std::string_view sv) {
    // 需要 NUL 终止？构造 string
    std::string str(sv);  // 拷贝，保证 NUL 终止
    std::printf("Value: %s\n", str.c_str());  // 安全
}
```

This does introduce a copy — that is the price of correctness. If you adopted `string_view` for performance, then "conceding" and making one copy at the spots that genuinely need NUL termination beats shipping UB every time.

### The Safety of std::string's Constructor

In the other direction, constructing a `std::string` from a `string_view` is safe — `std::string`'s constructor handles input without NUL termination correctly (because it has the length information):

```cpp
std::string_view sv = "hello\x00world"sv;  // 包含一个 \0，长度 11
std::string s(sv);  // 正确！s 包含所有 11 个字符
```

## Pitfall 3: Implicit Conversion Traps

The implicit conversion from `std::string` to `string_view` is one-way and effortless. That is a good thing — it lets you pass a `string` seamlessly to a function taking `string_view`. But the reverse conversion requires explicit action, and sometimes "implicit" is itself the trap.

### string to string_view: Dangerously Easy

```cpp
void process(std::string_view sv);

std::string s = "hello";
process(s);  // 隐式转换，很方便

// 但这也行：
process(std::string("temp"));  // 临时 string 构造 view → 传参期间安全
// 如果 process 不存储这个 view，就没问题
// 但如果 process 内部把这个 view 存到了某个地方...
```

The "convenience" of implicit conversion makes you drop your guard. During code review it is very easy to miss a `string_view` parameter being fed a temporary `string` — the syntax is completely legal, and the compiler will not warn.

### string_view to string: Must Be Explicit

A `string_view` cannot implicitly convert to `std::string`; you must construct one explicitly:

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

This design is deliberate — converting from `string_view` to `string` involves a heap allocation and a character copy, and the compiler does not want to perform an operation that heavy without your knowledge.

## Pitfall 4: Functions That Return string_view

A function returning a `string_view` is not a problem in itself — provided the data the returned view points at lives long enough. Here are the safe patterns:

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

The unsafe pattern:

```cpp
// 不安全：返回指向局部变量的视图
std::string_view format_name(const char* first, const char* last) {
    std::string full = std::string(first) + " " + last;
    return full;  // UB！full 是局部变量
}
```

A useful rule of thumb: if a function returns a `string_view`, it must be an observer of some data that "lives longer". Either it points into a parameter's data (valid for the duration of the call), into static storage (valid forever), or into a member variable (valid for as long as the object lives). If you ever find a function that creates a new `std::string` internally and then returns a view of it — that is a bug, one hundred percent of the time.

## Pitfall 5: Storing string_view in Member Variables

Making `string_view` a member variable of your class is something that demands extra caution. A class's lifetime is usually far longer than a function's, while the data the `string_view` points at may be long gone.

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

The better approach is to have the class own the data itself:

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

This costs one extra copy, but it eliminates an entire class of lifetime bugs. In most scenarios, that performance price is worth paying.

## Best-Practice Quick Reference

We have collected all the traps and their countermeasures into a single table:

| Scenario | Risk | Recommended practice |
|----------|------|----------------------|
| Function parameters (read-only use) | Low | Pass `string_view` by value |
| Function return values | High | Never return a view pointing to local or temporary data |
| Class member variables | High | Hold the data with `std::string`; use `string_view` only for short-term observation |
| Container keys (`unordered_map`) | High | Ensure the underlying string outlives the container, or use `std::string` as the key |
| Calling C APIs | High | Explicitly construct a `std::string` and use `c_str()` |
| Storing `string_view` in containers | High | Store only views of static data, or use `std::string` |
| Async / deferred execution | High | Before capturing a `string_view` into a lambda, make sure the data lives long enough |
| Signal / callback registration | High | A `string_view` in a callback may execute later; replace it with `std::string` |

There is only one core principle: **use `string_view` solely for short-term, synchronous, read-only access.** If the data needs to "outlive the current function call", use `std::string`.

A few more lessons from real projects. First, during code review pay special attention to every `string_view` member variable — if there is one, follow up with the question "when will the data it points at be released?". Second, for every function taking a `string_view` parameter, state explicitly in the documentation that "the parameter must remain valid for the duration of the call". Third, if your project builds with AddressSanitizer (ASan), always run the test suite under ASan — it catches `string_view` use-after-free precisely, about 100 times faster than hunting the bug down yourself. Enabling it is simple: add `-fsanitize=address -fno-omit-frame-pointer` when compiling and `-fsanitize=address` when linking.

```bash
# 开启 ASan 编译
g++ -std=c++17 -O0 -g -fsanitize=address -fno-omit-frame-pointer main.cpp
./a.out
# 如果有 use-after-free，ASan 会打印详细的错误报告
```

## Looking Ahead: C++26 std::zstring_view (Proposal P3655)

The C++ community is well aware of `string_view`'s shortcomings around NUL termination too. Proposal P3655 suggests introducing `std::zstring_view` (also known as `std::cstring_view`), with the goal of providing a `string_view` variant that guarantees NUL termination. The proposal currently targets the C++26 standard and has not been officially published yet.

The design philosophy behind `zstring_view` is to build on `string_view` by adding a NUL-termination guarantee, so that it can be passed safely to C APIs. It remains non-owning, so the lifetime problems persist — but at least the NUL-termination half of the pain is solved.

Until `zstring_view` officially lands in the standard, if you need similar functionality you can wrap a lightweight `zstring_view` class yourself — the core idea is to inherit from (or compose) `string_view`, check for NUL termination in the constructor, and have `data()` return a pointer that is guaranteed NUL-terminated. Honestly, though, in most projects `std::string(sv).c_str()` is already good enough.

## References

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)
- [cppreference: data() notes (NUL not guaranteed)](https://en.cppreference.com/w/cpp/string/basic_string_view/data.html)
- [PVS-Studio: C++ programmer's guide to undefined behavior - string_view](https://pvs-studio.com/en/blog/posts/cpp/1149/)
- [StackOverflow: Using string_view with C API expecting null-terminated strings](https://stackoverflow.com/questions/41286898/using-stdstring-view-with-api-that-expects-null-terminated-string)
- [WG21 P3655R0: zstring_view proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3655r0.html)
- [ISO C++ discussion: string_view design considerations](https://groups.google.com/a/isocpp.org/g/std-discussion/c/Gj5gt5E-po8)
