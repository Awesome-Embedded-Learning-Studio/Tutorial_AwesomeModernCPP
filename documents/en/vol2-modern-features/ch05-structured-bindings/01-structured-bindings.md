---
chapter: 5
cpp_standard:
- 17
description: Unpack pairs, tuples, arrays, and structs elegantly with structured bindings
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'std::variant: A Type-Safe Union'
- 'std::optional: Elegantly Expressing ''A Value May Be Absent'''
reading_time_minutes: 11
related:
- 'if/switch Initializers: Narrowing Variable Scope'
tags:
- host
- cpp-modern
- intermediate
title: 'Structured Bindings: Unpacking Multiple Values in One Line'
translation:
  source: documents/vol2-modern-features/ch05-structured-bindings/01-structured-bindings.md
  source_hash: c54cef7d2cf4d2524c7a2f43a6a0d5f181280ab73a3917949cd7ac0c17fbf84c
  translated_at: '2026-11-23T01:10:31+00:00'
  engine: glm-5.3-refresh
  token_count: 9723
---
# Structured Bindings: Unpacking Multiple Values in One Line

When I'm writing code, there's an awkward scenario I keep running into: a function returns multiple values, and we have to tear the returned things apart one by one into variables. With `pair`, we write `result.first`, `result.second`—and the meaning of the variables is left entirely to guesswork. And `tuple`'s `std::get<0>(t)` isn't pretty to write either. C++11's `std::tie` eased the pain, but honestly its syntax hardly counts as elegant: you have to declare all the variables up front, and only then does the stuffing of values fall to `tie`. Isn't there an unpacking style as satisfying as Python's `a, b = func()`? Well, now there is, folks.

C++17 finally handed us a proper answer, and its name is structured bindings. In one line we can unpack a `pair`, `tuple`, array, or struct and get properly named variables directly. The semantics turn clear on the spot, and the runtime overhead is zero.

------

## Starting with pair and tuple

### pair: The Most Common Multi-Value Return

`std::pair` is the standard library's most common way of "bundling two values together": `std::map::insert` returns a `pair<iterator, bool>`, and when we iterate a map, each element we get is in turn a `pair<const Key, Value>`. In the days before structured bindings, this was all we could write:

```cpp
auto result = m.insert({1, "one"});
if (result.second) {
    std::cout << "Inserted: " << result.first->second << '\n';
}
```

What does `result.second` mean? Without checking the documentation, you can't read its semantics out of it. Structured bindings write the semantics straight into the variable names:

```cpp
auto [it, inserted] = m.insert({1, "one"});
if (inserted) {
    std::cout << "Inserted: " << it->second << '\n';
}
```

How `.first` and `.second` each get bound to a name has been turned into an animation. You can play it, pause it, or step through it segment by segment with the step controls:

<Anim id="structured-bindings" />

Using it to iterate a map in a range-based for loop is where it gets downright elegant: where we used to write `it->first` and `it->second`, what we write now is `[key, value]`:

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

There's a detail here: the loop body says `+id` rather than `id`. The reason is that `uint8_t`'s `operator<<` treats it as a character and prints it as one, while the integral promotion that `+` performs turns it into an `int` value before printing. Words are cheap—hit "Try it yourself" and run it, and it's plain as day:

<OnlineCompilerDemo
  title="Hands-On Verification: Integral Promotion of +id"
  source-path="code/examples/vol2/35_uint8_promotion.cpp"
  description="Compare the same uint8_t online: without the + it prints as the character A; with the +, integral promotion makes it print as the number 65."
  run-options="-std=c++17"
  allow-run
/>

Try the same `id = 65`: without the `+`, what you see is the character `A`. With the `+`, what you see is the number.

### tuple: When Two Values Aren't Enough

By the time a function needs to return three or even more values, `std::tuple` is the natural choice. The structured-binding syntax looks exactly the same as with pair—let's just look:

```cpp
std::tuple<int, std::string, double> query_database(int id) {
    return {id, "sensor_" + std::to_string(id), 23.5};
}

auto [record_id, name, value] = query_database(42);
```

### Comparison with std::tie

C++11's `std::tie` can do something similar, but the experience is noticeably worse. It requires you to declare all the variables up front, and only then is the assigning handed over to `tie`:

```cpp
int record_id;
std::string name;
double value;
std::tie(record_id, name, value) = query_database(42);
```

Put them side by side and the difference is obvious: structured bindings declare and unpack in a single step, while `std::tie` needs two. Here let me say a fair word for `tie`, though: it works through references internally, so it can in fact handle tuples containing non-copyable types (such as `std::unique_ptr`), because reference binding never involves a copy at all. When it comes to syntax, though, structured bindings are still the cleaner spelling, and they cover more semantics: by value, by reference, and by forwarding reference are all supported.

------

## Native Arrays and Structs

### Native Arrays

Fixed-size native arrays can be unpacked directly too. That is extremely handy when dealing with fixed-format data:

```cpp
int rgb[3] = {255, 128, 0};
auto [r, g, b] = rgb;
```

Each row of a two-dimensional array can also be unpacked inside a loop:

```cpp
int matrix[2][3] = {
    {1, 2, 3}, {4, 5, 6}
};
for (auto& row : matrix) {
    auto [a, b, c] = row;
    std::cout << a << ' ' << b << ' ' << c << '\n';
}
```

Note that structured bindings only support directly unpacking one-dimensional arrays. You can't write `auto [a, b, c, d, e, f] = matrix`, because `matrix` is essentially an `int[2][3]`—its size is 2, not 6.

### Structs and Classes

If all of a struct's non-static data members are `public`, structured bindings can unpack it directly. The compiler binds the members in declaration order:

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

No template metaprogramming knowledge required—as long as the struct's members are public, it just works. This is arguably the most intuitive use of structured bindings.

Two more details deserve our attention. Bit fields are fully supported, and `mutable` members have their own special behavior: the hidden "anonymous variable" the names bind into may be `const`-qualified, but `mutable` members are exempt from that restriction—you can still modify them.

------

## The Three Binding Semantics

Structured bindings don't always copy. In fact, the qualifiers you attach to `auto` determine the type of the underlying anonymous variable:

- **`auto [...]`**—copy by value. The bound names refer to that copy.
- **`auto& [...]`**—binds to an lvalue reference. You can modify the original object.
- **`const auto& [...]`**—binds to a const lvalue reference. Read-only access, no copy.
- **`auto&& [...]`**—forwarding reference. Binds to both lvalues and rvalues.

Let's tell them apart with one example:

```cpp
std::pair<int, int> range{1, 10};

// 拷贝：r1、r2 引用的是匿名拷贝，不影响 range
auto [r1, r2] = range;

// 引用：直接操作原对象
auto& [r3, r4] = range;
r3 = 5;  // range.first 变成 5
```

Run it and you'll see that `auto&` modifies the original object while `auto` modifies the copy—the program is right below; just hit 'Try it yourself' to run it:

<OnlineCompilerDemo
  title="Hands-On Verification: Value Binding vs Reference Binding"
  source-path="code/examples/vol2/36_binding_semantics.cpp"
  description="Compare the two binding semantics online: the auto& binding modifies range.first (it becomes 5), while r1 from the by-value auto copy is unaffected (still 1)."
  run-options="-std=c++17"
  allow-run
/>

The underlying mechanism works like this: the compiler first declares an anonymous variable (its type determined by `auto`/`auto&`/`const auto&`/`auto&&`) and initializes it with the right-hand expression. Then each bound name is a reference to a member of that anonymous variable (or, in the by-value case, a reference to a member of the copy).

```cpp
// auto [x, y] = get_point(); 大致等价于：
auto __anonymous = get_point();
auto& x = __anonymous.first;   // 引用匿名变量的成员
auto& y = __anonymous.second;
```

This means the bound names are always references—they refer to members of that hidden anonymous object. You can't take the address of "the bound name itself"; you can only take the address of the subobject it refers to.

One caveat: `auto&` requires the right-hand side to be an lvalue. If the right-hand side is a temporary (for example, the return value of `std::make_pair(1, 2)`), `auto&` fails to compile, because a non-const reference cannot bind to an rvalue. In that case, use `const auto&`, or plain `auto` to copy by value.

```cpp
// 错误：auto& 不能绑定到临时对象
auto& [x, y] = std::make_pair(1, 2);

// 正确：const 引用可以延长临时对象生命周期
const auto& [x, y] = std::make_pair(1, 2);

// 或者直接拷贝
auto [x, y] = std::make_pair(1, 2);
```

------

## Making Custom Types Bindable: The Tuple-Like Protocol

If your class has private members, the struct-style unpacking above won't work. But C++ offers another path: teach the compiler to treat your class as a "tuple-like" type. You only need three things:

1. Specialize `std::tuple_size<YourType>` to tell the compiler how many elements there are.
2. Specialize `std::tuple_element<I, YourType>` to tell the compiler the type of element `I`.
3. Provide a `get<I>()` function in `YourType`'s namespace that returns element `I`.

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

Paired with the ADL overloads of `get<I>`, you can now happily unpack:

```cpp
SensorData data{5, 23.5f};
auto [id, value] = data;    // id = 5, value = 23.5
```

A live run confirms it below (note that `id` once again needs the `+` to print as a number)—just hit 'Try it yourself' to run it:

<OnlineCompilerDemo
  title="Hands-On Verification: Structured Bindings on a Custom Type"
  source-path="code/examples/vol2/37_sensor_data_binding.cpp"
  description="Verify the tuple-like protocol online: after specializing tuple_size/tuple_element, SensorData with private members can also be unpacked with auto [id, value]."
  run-options="-std=c++17"
  allow-run
/>

> The part that deserves our full attention here is where the `get<I>()` function lives: it must be defined in the namespace the class belongs to (ADL rules) for the compiler to find it. The specializations on the `std` side have their own separate requirement: write the `tuple_size` and `tuple_element` specializations inside `std`, while the `get` function can simply stay in the class's own namespace.

This mechanism is known as the "tuple-like protocol", and the standard library's `std::pair`, `std::tuple`, and `std::array` all build their structured-binding support on it.

------

## Changes in C++20

C++20 adjusted structured bindings in a few places, mostly related to `constexpr` contexts—let's go through them one by one.

Structured bindings can be used inside `constexpr` functions. That means a compile-time computation function can return multiple values and we can catch them all in one line via structured bindings:

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

One thing to watch, though: you cannot declare a `constexpr` structured binding directly at namespace scope (`constexpr auto [x, y] = get_point();` is a compile error). That's because a structured binding is essentially a declaration of a group of reference variables, not a declaration of a single variable.

As for lambda captures, there's a common misconception to clear up first: C++17 already supports directly capturing structured-binding variables. Look—the following code runs as-is under C++17:

```cpp
std::map<int, std::string> m = { { 1, "one"}, {2, "two"} };

for (const auto& [k, v] : m) {
    auto callback = [k, v] {  // C++17 就支持直接捕获
        std::cout << k << ": " << v << '\n';
    };
    callback();
}
```

What C++20 adds is the init-capture syntax (`key = k`), which is more flexible in some situations. But note that the `[=]` default capture does not automatically capture structured-binding variables—you need to list them explicitly.

------

## Performance: Zero-Overhead Syntactic Sugar

Performance-wise it costs you nothing: structured bindings themselves carry no runtime overhead—they are purely a compile-time syntax transformation. The compiler creates the anonymous variable behind the scenes and makes the bound names refer to its members, and all of it happens at compile time.

```cpp
// 这两种写法生成的汇编代码完全一样
auto [x, y] = get_point();

// 等价于
auto __tmp = get_point();
auto x = __tmp.first;
auto y = __tmp.second;
```

"The assembly is identical" isn't a claim you make empty-handed. We tested it for real with GCC 16.1.1: compile each version with `g++ -std=c++17 -O2 -S`, then run `diff`:

```bash
g++ -std=c++17 -O2 -S sb_structured.cpp
g++ -std=c++17 -O2 -S sb_manual.cpp
diff sb_structured.s sb_manual.s
```

The `diff` output is a single line—the `.file` line, which differs in the source file name; the actual instructions are identical:

```text
_Z1fv:                  # f()，两个版本一模一样
    movl    $7, %eax    # 直接返回 3 + 4 = 7
    ret
```

Look at what the compiler did after inlining `get_point()`: it constant-folded everything straight into `movl $7, %eax`, and the structured binding left not a single extra instruction behind. So the performance advice is simple: use `const auto&` for large structs to avoid copies, and plain `auto` by value for small types (built-in types, small structs). `auto&&` is useful in generic code, but when the concrete type is known, spelling out `auto` or `const auto&` explicitly is clearer.

------

## Where It's Easy to Go Wrong

### Lifetime Issues

When `auto&&` binds to a temporary, the anonymous variable's lifetime is extended to the end of the bound names' scope, so `auto&&` or `const auto&` is safe. But if you take a pointer or reference to a bound name and pass it out of the scope, you're exposed to dangling risks:

```cpp
const auto& [x, y] = std::make_pair(1, 2);
// x, y 在这个作用域内有效，安全
// 但如果 &x 被存到外部，作用域结束后就悬空了
```

### Can't Be Used Directly as a Return Value

The names introduced by structured bindings cannot be used directly as a function return. If you want to return the unpacked values, you need to re-pack them:

```cpp
auto [x, y] = get_point();
// 不能 return x, y; 必须重新打包
return std::make_pair(x, y);

// 或者直接返回函数结果
return get_point();
```

### Can't Be Used in Class Member Declarations

You cannot use structured bindings in class member declarations:

```cpp
class MyClass {
    auto [x, y] = get_point();  // 编译错误
};
```

If you need to store the unpacked values, use a struct or a `pair`/`tuple` member instead.

------

## Run It Online

Unpacking pairs, tuples, arrays, and structs is all collected into a single example—run it online and try each of them:

<OnlineCompilerDemo
  title="Structured Bindings: Unpacking pairs, tuples, arrays, and structs"
  source-path="code/examples/vol2/11_structured_bindings.cpp"
  description="Run it online and watch structured bindings unpack pairs, tuples, arrays, and structs."
  allow-run
/>

## References

- [cppreference: Structured binding declaration](https://en.cppreference.com/w/cpp/language/structured_binding)
- [Structured bindings in C++17, 8 years later - C++ Stories](https://www.cppstories.com/2025/structured-bindings-cpp26-updates/)
- [Adding structured bindings to your classes - Sy Brand](https://tartanllama.xyz/structured-bindings/)
