---
title: 'Structured Bindings: Unpacking Multiple Values in One Line'
description: Unpack pairs, tuples, arrays, and structs elegantly with structured bindings
chapter: 5
order: 1
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
- 'Chapter 4: std::variant'
- 'Chapter 4: std::optional'
related:
- if/switch 初始化器
translation:
  source: documents/vol2-modern-features/ch05-structured-bindings/01-structured-bindings.md
  source_hash: 07aa03d38d149f507524f711f4da22ee3d297b1e6d1764876cb0f6f4c2a19262
  translated_at: '2026-05-26T11:29:30.484231+00:00'
  engine: anthropic
  token_count: 2107
---
# Structured Bindings: Unpacking Multiple Values in One Line

When writing code, we often run into an awkward scenario: a function returns multiple values, and we have to unpack them one by one into variables. When using `std::pair`, we write `first` and `second`; when using `std::tuple`, we write `std::get<0>` and `std::get<1>` — either the semantics are unclear, or the syntax is ugly. C++11 introduced `std::tie` to alleviate this problem, but honestly, the syntax isn't exactly elegant either: you have to declare all variables upfront, then stuff values into them with `std::tie`. Is there a feature that feels as satisfying as Python's multiple return value unpacking? We finally got one, folks!

C++17 finally gave us a real answer — structured bindings. One line of code unpacks `std::pair`, `std::tuple`, arrays, and structs, giving us named variables directly with clear semantics and zero overhead.

> In a nutshell: **Structured bindings let you "unpack" compound types into multiple named variables, while the compiler handles everything behind the scenes.**

------

## Step One — Binding pair and tuple

### pair: The Most Common Multiple Return Value

`std::pair` is the most common way to "pack two values" in the standard library. `std::map::insert` returns a `std::pair`, and `std::unordered_map::emplace` returns a `std::pair`. Before structured bindings, we could only write it like this:

```cpp
auto result = m.insert({1, "one"});
if (result.second) {
    std::cout << "Inserted: " << result.first->second << '\n';
}
```

What does `it->second` mean? Without checking the documentation, you'd have no idea. Structured bindings write the semantics directly into the variable names:

```cpp
auto [it, inserted] = m.insert({1, "one"});
if (inserted) {
    std::cout << "Inserted: " << it->second << '\n';
}
```

It's incredibly elegant when iterating over a map in a range for loop. We used to write `it->first` and `it->second`, but now we can just write:

```cpp
std::map<int, std::string> sensor_names = {
    {1, "Temperature"},
    {2, "Humidity"},
    {3, "Pressure"}
};

for (const auto& [id, name] : sensor_names) {
    std::cout << "Sensor " << +id << ": " << name << '\n';
}
```

> Why write `static_cast<char>(c)`? Because `std::cout`'s `<<` operator treats it as a character, while `std::cout` performs integral promotion, casting it to `int` before printing.

### tuple: When You Have More Than Two Values

When a function needs to return three or more values, `std::tuple` is the natural choice. The syntax for structured bindings is exactly the same as for `std::pair`:

```cpp
std::tuple<int, std::string, double> query_database(int id) {
    return {id, "sensor_" + std::to_string(id), 23.5};
}

auto [record_id, name, value] = query_database(42);
```

### Comparison with std::tie

C++11's `std::tie` can do something similar, but the experience is noticeably worse. It requires you to declare all variables upfront, then assign values into them with `std::tie`:

```cpp
int record_id;
std::string name;
double value;
std::tie(record_id, name, value) = query_database(42);
```

The difference is obvious: structured bindings combine variable declaration and unpacking in one step, whereas `std::tie` requires two separate steps. Although `std::tie` uses references internally, it can actually handle tuples containing non-copyable types (like `std::unique_ptr`) — because binding to a reference doesn't involve copying. However, the syntax of structured bindings is more concise, and it supports multiple semantics such as by-value, by-reference, and by-forwarding-reference.

------

## Step Two — Binding Native Arrays and Structs

### Native Arrays

Fixed-size native arrays can also be unpacked directly. This is very convenient when dealing with data in a fixed format:

```cpp
int rgb[3] = {255, 128, 0};
auto [r, g, b] = rgb;
```

Each row of a two-dimensional array can also be unpacked in a loop:

```cpp
int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
for (auto& row : matrix) {
    auto [a, b, c] = row;
    std::cout << a << ' ' << b << ' ' << c << '\n';
}
```

Note that structured bindings only support direct unpacking of one-dimensional arrays. You cannot write `auto [a, b, c, d, e, f] = matrix;`, because `matrix` is essentially an `int[2][3]`, where the size is two, not six.

### Structs and Classes

If all non-static data members of a struct are `public`, it can be directly unpacked using structured bindings. The compiler binds them in declaration order:

```cpp
struct SensorReading {
    uint8_t sensor_id;
    float value;
    uint32_t timestamp;
    bool is_valid;
};

SensorReading reading{5, 23.5f, 1234567890, true};
auto [id, val, ts, valid] = reading;
```

This is probably the most intuitive use of structured bindings. You don't even need to understand any template metaprogramming — as long as the struct members are public, you're good to go.

Structured bindings require data members to be bound in declaration order, and they fully support bit fields. If the struct has `const` members, you need to be careful about the behavior: the "anonymous variable" they are bound to might be `const`-qualified, but non-`const` members are not subject to this restriction and can still be modified.

------

## Step Three — Understanding the Three Binding Semantics

Structured bindings don't always copy. In fact, the modifier before `auto` determines the type of the underlying anonymous variable:

- **`auto`** — Copy by value. The bound variables are references to this copy.
- **`auto&`** — Bind to an lvalue reference. The original object can be modified.
- **`const auto&`** — Bind to a const lvalue reference. Read-only access, no copy.
- **`auto&&`** — Forwarding reference. Can bind to both lvalues and rvalues.

Let's look at an example to distinguish them:

```cpp
std::pair<int, int> range{1, 10};

// 拷贝：r1、r2 引用的是匿名拷贝，不影响 range
auto [r1, r2] = range;

// 引用：直接操作原对象
auto& [r3, r4] = range;
r3 = 5;  // range.first 变成 5
```

The underlying mechanism works like this: the compiler first declares an anonymous variable (whose type is determined by `auto`/`auto&`/`const auto&`/`auto&&`), and initializes it with the expression on the right. Then, each bound variable is a reference to a member of this anonymous variable (or, in the case of by-value, a reference to a member of the copy).

```cpp
// auto [x, y] = get_point(); 大致等价于：
auto __anonymous = get_point();
auto& x = __anonymous.first;   // 引用匿名变量的成员
auto& y = __anonymous.second;
```

This means the bound variables themselves are always references — they refer to members of that hidden anonymous object. You can't take the address of the "bound variable itself"; you can only take the address of the sub-object it references.

⚠️ Warning: `auto&` requires the right side to be an lvalue. If the right side is a temporary object (such as the return value of a function), `auto&` will fail to compile because a non-const reference cannot bind to an rvalue. In this case, you should use `const auto&` or simply use `auto` to copy by value.

```cpp
// 错误：auto& 不能绑定到临时对象
auto& [x, y] = std::make_pair(1, 2);

// 正确：const 引用可以延长临时对象生命周期
const auto& [x, y] = std::make_pair(1, 2);

// 或者直接拷贝
auto [x, y] = std::make_pair(1, 2);
```

------

## Step Four — Adding Binding Support for Custom Types (Tuple-Like Protocol)

If your class has private members, it can't be directly unpacked using the struct method. But C++ provides another path: letting the compiler treat your class as a "tuple-like" type. You only need three things:

1. Specialize `std::tuple_size`, to tell the compiler how many elements there are.
2. Specialize `std::tuple_element`, to tell the compiler the type of the *i*-th element.
3. Provide a `get` function in the same namespace as the class, returning the *i*-th element.

```cpp
#include <utility>
#include <cstdint>

class SensorData {
public:
    SensorData(uint8_t id, float value) : id_(id), value_(value) {}

    template<std::size_t I>
    auto& get() {
        if constexpr (I == 0) return id_;
        else if constexpr (I == 1) return value_;
    }

    template<std::size_t I>
    const auto& get() const {
        if constexpr (I == 0) return id_;
        else if constexpr (I == 1) return value_;
    }

private:
    uint8_t id_;
    float value_;
};

// 特化 tuple_size：告诉编译器有 2 个元素
template<>
struct std::tuple_size<SensorData> : std::integral_constant<std::size_t, 2> {};

// 特化 tuple_element：告诉编译器每个元素的类型
template<>
struct std::tuple_element<0, SensorData> { using type = uint8_t; };

template<>
struct std::tuple_element<1, SensorData> { using type = float; };
```

Now we can happily unpack it:

```cpp
SensorData data{5, 23.5f};
auto [id, value] = data;    // id = 5, value = 23.5
```

> The key here is that the `get` function must be defined in the same namespace as the class (ADL rules), so the compiler can find it. For specializations in the standard namespace `std`, you need to write the specializations for `std::tuple_size` and `std::tuple_element` in the `std` namespace, but the `get` function can simply be placed in the namespace where the class resides.

This mechanism is known as the "tuple-like protocol." Standard library types like `std::array`, `std::complex`, and `std::pair` all rely on it to implement structured binding support.

------

## C++20 Enhancements

C++20 made some enhancements to structured bindings, primarily related to `constexpr` contexts.

Structured bindings can be used inside `constexpr` functions, which means compile-time computation functions can also return multiple values and receive them using structured bindings:

```cpp
constexpr auto get_point() {
    return std::make_pair(3, 4);
}

constexpr bool test_structured_binding() {
    auto [x, y] = get_point();
    return x == 3 && y == 4;
}

static_assert(test_structured_binding());
```

However, note that you cannot declare `constexpr` structured bindings directly at namespace scope (for example, `constexpr auto [x, y] = ...;` is a compile error). This is because a structured binding is essentially a declaration of a group of reference variables, not a single variable declaration.

Regarding lambda captures, C++17 actually already supports capturing structured binding variables directly. The following code works in C++17:

```cpp
std::map<int, std::string> m = {{1, "one"}, {2, "two"}};

for (const auto& [k, v] : m) {
    auto callback = [k, v] {  // C++17 就支持直接捕获
        std::cout << k << ": " << v << '\n';
    };
    callback();
}
```

What C++20 added is the init-capture syntax (`[name = expr]`), which is more flexible in certain situations. But keep in mind that a default capture (`[=]` or `[&]`) does not automatically capture structured binding variables; you need to list them explicitly.

------

## Performance: Zero-Overhead Syntactic Sugar

Structured bindings have absolutely no runtime overhead. They are purely a compile-time syntactic transformation — the compiler creates an anonymous variable behind the scenes, and then the bound variables reference the members of that anonymous variable. The generated assembly code is identical to what you'd get by manually extracting members and assigning them.

```cpp
// 这两种写法生成的汇编代码完全一样
auto [x, y] = get_point();

// 等价于
auto __tmp = get_point();
auto x = __tmp.first;
auto y = __tmp.second;
```

The performance advice is simple: for large structs, use `const auto&` to avoid copying; for small types (built-in types, small structs), just use `auto` to copy by value. `auto&&` is very useful in generic code, but in scenarios where the concrete type is known, explicitly writing `auto&` or `const auto&` is clearer.

------

## Common Pitfalls

### Lifetime Issues

When `auto` or `const auto&` binds to a temporary object, the lifetime of the anonymous variable is extended to the end of the bound variable's scope, so using `auto` or `const auto&` is safe. But if you take a pointer or reference to a bound variable and pass it out, there's a risk of dangling:

```cpp
const auto& [x, y] = std::make_pair(1, 2);
// x, y 在这个作用域内有效，安全
// 但如果 &x 被存到外部，作用域结束后就悬空了
```

### Cannot Be Used Directly as a Return Value

The variable names from structured bindings cannot be used directly as function return values. If you want to return an unpacked value, you need to repack it:

```cpp
auto [x, y] = get_point();
// 不能 return x, y; 必须重新打包
return std::make_pair(x, y);

// 或者直接返回函数结果
return get_point();
```

### Cannot Be Used in Class Member Declarations

You cannot use structured bindings in class member declarations:

```cpp
class MyClass {
    auto [x, y] = get_point();  // 编译错误
};
```

If you need to store unpacked values, use a struct or `std::tuple`/`std::pair` members instead.

------

## Run Online

Run the structured binding examples online to experience unpacking with pair, tuple, arrays, and structs:

<OnlineCompilerDemo
  title="Structured Bindings: Unpacking pair, tuple, Arrays, and Structs"
  source-path="code/examples/vol2/11_structured_bindings.cpp"
  description="Run online and observe the unpacking effects of structured bindings on pair, tuple, arrays, and structs."
  allow-run
/>

## Summary

Structured bindings are one of the most practical features in C++17. The types it supports cover the vast majority of everyday development scenarios: `std::pair`, `std::tuple`, native arrays, structs with public members, and custom types that implement the tuple-like protocol. The binding semantics are entirely determined by the modifier before `auto` — `auto` is a copy, `auto&` is a reference, `const auto&` is a read-only reference, and `auto&&` is a forwarding reference.

In practice, our most common uses are iterating over maps in range for loops (`auto& [key, value]`) and handling multiple return value functions. Combined with the if/switch initializers covered in the next chapter, structured bindings can take code conciseness and readability to the next level.

## References

- [cppreference: Structured binding declaration](https://en.cppreference.com/w/cpp/language/structured_binding)
- [Structured bindings in C++17, 8 years later - C++ Stories](https://www.cppstories.com/2025/structured-bindings-cpp26-updates/)
- [Adding structured bindings to your classes - Sy Brand](https://tartanllama.xyz/structured-bindings/)
