---
chapter: 5
cpp_standard:
- 17
description: Unpack pairs, tuples, arrays, and structs elegantly with structured binding
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 4: std::variant'
- 'Chapter 4: std::optional'
reading_time_minutes: 11
related:
- if/switch 初始化器
tags:
- host
- cpp-modern
- intermediate
title: 'Structured Binding: Unpacking Multiple Values in One Line'
translation:
  source: documents/vol2-modern-features/ch05-structured-bindings/01-structured-bindings.md
  source_hash: 97fac40ee9565ca01e3a1eb7cae4fe21b39f845573e6ecd7db9f3c865d28793e
  translated_at: '2026-06-16T03:57:49.673279+00:00'
  engine: anthropic
  token_count: 2103
---
# Structured Binding: Unpacking Multiple Values in One Line

When writing code, I often encounter an awkward scenario: a function returns multiple values, and I have to unpack them one by one and assign them to variables. Using `std::pair` means writing `.first` and `.second`, and using `std::tuple` means writing `std::get<0>` and `std::get<1>`—either the semantics are unclear, or the syntax is ugly. C++11 introduced `std::tie` to alleviate this problem, but honestly, the syntax isn't elegant either: you have to declare all the variables first, and then use `std::tie` to stuff values into them. Is there a feature that feels as good as Python's multi-value unpacking? Yes, there is, folks!

C++17 finally gave us a real answer—Structured Binding. One line of code unpacks `std::pair`, `std::tuple`, arrays, and structs directly into named variables. The semantics are clear, and the overhead is zero.

> TL;DR: **Structured binding allows you to "unpack" compound types into multiple named variables, while the compiler handles everything behind the scenes.**

------

## Step 1 — Binding pair and tuple

### pair: The Most Common Multi-Return Value

`std::pair` is the most common way to "pack two values" in the standard library. `std::map::insert` returns a `std::pair`, and `std::map::find` returns an iterator to a `std::pair`. Before structured binding, we had to write this:

```cpp
auto result = my_map.insert(...);
if (result.second) {
    // ...
}
```

What does `result.second` mean? Without checking the documentation, you have no idea. Structured binding writes the semantics directly into the variable names:

```cpp
auto [iter, success] = my_map.insert(...);
if (success) {
    // ...
}
```

It is incredibly elegant when iterating over a map in a range-based for loop. Previously, you would write `it->first` and `it->second`; now, you can write `key` and `value` directly:

```cpp
for (const auto& [key, value] : my_map) {
    std::cout << key << ": " << value << '\n';
}
```

> Why write `'\n'` instead of `std::endl`? Because `std::endl` outputs a newline character **and** flushes the buffer, which can significantly slow down I/O performance. `'\n'` only outputs a newline.

### tuple: Cases with More Than Two Values

When a function needs to return three or more values, `std::tuple` is the natural choice. The syntax for structured binding is exactly the same as for `pair`:

```cpp
std::tuple<int, int, int> get_coords() {
    return {10, 20, 30};
}

auto [x, y, z] = get_coords();
```

### Comparison with std::tie

C++11's `std::tie` can do something similar, but the experience is much worse. It requires declaring all variables first, then using `std::tie` to assign values to them:

```cpp
int x, y, z;
std::tie(x, y, z) = get_coords();
```

The comparison is obvious: structured binding combines variable declaration and unpacking in one step, whereas `std::tie` requires two steps. Although `std::tie` uses references internally (meaning it can handle tuples containing non-copyable types like `std::unique_ptr` because reference binding doesn't involve copying), structured binding offers cleaner syntax and supports multiple semantics: by value, by reference, and by forwarding reference.

------

## Step 2 — Binding Native Arrays and Structs

### Native Arrays

Fixed-size native arrays can also be unpacked directly. This is very convenient when processing data in a fixed format:

```cpp
int arr[3] = {1, 2, 3};
auto [a, b, c] = arr;
```

Each row of a two-dimensional array can also be unpacked in a loop:

```cpp
int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};

for (auto [x, y, z] : matrix) {
    std::cout << x << ", " << y << ", " << z << '\n';
}
```

Note that structured binding only supports direct unpacking of one-dimensional arrays. You cannot write `auto [x, y, z] = matrix`, because `matrix[0]` is essentially `int[3]`, whose size is 2, not 6.

### Structs and Classes

If all non-static data members of a struct are `public`, it can be unpacked directly by structured binding. The compiler binds them in declaration order:

```cpp
struct Point {
    double x;
    double y;
};

Point get_point() {
    return {3.5, 4.5};
}

auto [x, y] = get_point();
```

This is arguably the most intuitive usage of structured binding. You don't even need to understand template metaprogramming; as long as the struct members are public, you can use it.

Structured binding requires data members to be bound in declaration order and fully supports bit fields. If the struct contains `const` members, behavior needs attention: the bound "anonymous variable" might be `const`-qualified, but `mutable` members are not restricted by this and can still be modified.

------

## Step 3 — Understanding the Three Semantics of Binding

Structured binding does not always copy. In fact, the modifier before `auto` determines the type of the underlying anonymous variable:

- **`auto`** — Copy by value. The bound variables refer to this copy.
- **`auto&`** — Bind to an lvalue reference. Allows modification of the original object.
- **`const auto&`** — Bind to a const lvalue reference. Read-only access, no copy.
- **`auto&&`** — Forwarding reference. Can bind to both lvalues and rvalues.

Here is an example to distinguish them:

```cpp
std::tuple<int, std::string> get_tuple() {
    return {42, "hello"};
}

// 1. Copy: x and y are copies of the tuple elements
auto [x1, y1] = get_tuple();

// 2. Reference: x and y refer to the original object's elements
auto& [x2, y2] = t;

// 3. Const reference: read-only access
const auto& [x3, y3] = get_tuple();

// 4. Forwarding reference: deduces based on the value category of the initializer
auto&& [x4, y4] = get_tuple(); // Binds to rvalue reference
```

The underlying mechanism is this: the compiler first declares an anonymous variable (type determined by `auto`/`auto&`/`const auto&`/`auto&&`) and initializes it with the expression on the right. Then, each bound variable is a reference to a member of this anonymous variable (or, in the case of by-value, a reference to a member of the copy).

```cpp
// Compiler roughly transforms this:
auto [x, y] = get_tuple();

// Into this:
auto __anonymous = get_tuple();
using E = std::remove_reference_t<decltype(__anonymous)>;
auto& x = std::get<0>(__anonymous);
auto& y = std::get<1>(__anonymous);
```

This means the bound variables themselves are always references—they refer to the members of that hidden anonymous object. You cannot get the address of the "bound variable itself"; you can only get the address of the sub-object it references.

⚠️ **Note:** `auto&` requires the right-hand side to be an lvalue. If the right-hand side is a temporary object (like the return value of a function), `auto&` will fail to compile because a non-const reference cannot bind to an rvalue. In this case, use `auto&&` or simply `auto` to copy by value.

```cpp
// auto& [x, y] = get_tuple(); // Error: cannot bind non-const lvalue reference to rvalue
auto&& [x, y] = get_tuple();  // OK: binds to rvalue reference
```

------

## Step 4 — Custom Type Binding Support (Tuple-Like Protocol)

If your class has private members, it cannot be unpacked directly using the struct method. However, C++ offers another path: letting the compiler treat your class as a "tuple-like" type. You only need three things:

1. Specialize `std::tuple_size` to tell the compiler how many elements there are.
2. Specialize `std::tuple_element` to tell the compiler the type of the `i`-th element.
3. Provide a `get` function in the same namespace as the class to return the `i`-th element.

```cpp
struct MyElement {
    int value;
};

struct MyContainer {
    MyElement first;
    MyElement second;
};

// 1. Tell the compiler the size
template <>
struct std::tuple_size<MyContainer> {
    static constexpr size_t value = 2;
};

// 2. Tell the compiler the type of each element
template <size_t I>
struct std::tuple_element<I, MyContainer> {
    using type = MyElement;
};

// 3. Provide get() function (ADL)
template <size_t I>
MyElement& get(MyContainer& c) {
    if constexpr (I == 0) return c.first;
    if constexpr (I == 1) return c.second;
}
```

Now you can unpack it happily:

```cpp
MyContainer c;
auto [a, b] = c; // a and b are MyElement
```

> The key here is that the `get` function must be defined in the namespace where the class resides (ADL rule) so the compiler can find it. For specializations in the standard namespace `std`, you need to write specializations for `std::tuple_size` and `std::tuple_element` in the `std` namespace, but the `get` function can simply be placed in the class's namespace.

This mechanism is called the "tuple-like protocol." Standard library types like `std::pair`, `std::tuple`, and `std::array` rely on it to implement structured binding support.

------

## Enhancements in C++20

C++20 made some enhancements to structured binding, mainly related to `constexpr` contexts.

Structured binding can be used inside `constexpr` functions, which means compile-time functions can also return multiple values and receive them via structured binding:

```cpp
constexpr auto split(int x) {
    return std::tuple{x / 10, x % 10};
}

constexpr auto [div, mod] = split(42); // OK in C++20
```

However, note that you cannot declare structured binding with `constexpr` directly at namespace scope (e.g., `constexpr auto [x, y] = ...;` is a compilation error). This is because structured binding is essentially a declaration of a set of reference variables, not a single variable declaration.

Regarding lambda captures, C++17 actually supports capturing structured binding variables directly. The following code works in C++17:

```cpp
auto [x, y] = get_pair();
auto f = [x, y] { return x + y; };
```

C++20 added the init-capture syntax (e.g., `[x = x]`), which is more flexible in some cases. But be aware: default capture (`[=]` or `[&]`) does not automatically capture structured binding variables; you need to list them explicitly.

------

## Performance: Zero-Overhead Syntactic Sugar

Structured binding itself has no runtime overhead. It is purely a compile-time syntactic transformation—the compiler creates an anonymous variable behind the scenes and then has the bound variables reference the anonymous variable's members. The generated assembly code is identical to hand-written code that "extracts members and assigns them."

```cpp
// These two generate the same assembly:
auto [x, y] = get_point();
std::cout << x << y;

// vs
auto p = get_point();
std::cout << p.x << p.y;
```

Performance advice is simple: use `auto&` or `const auto&` for large structs to avoid copying, and use `auto` for small types (built-in types, small structs) to copy by value. `auto&&` is very useful in generic code, but when the specific type is known, explicitly writing `auto&` or `const auto&` is clearer.

------

## Common Pitfalls

### Lifetime Issues

When `auto&&` or `auto` binds to a temporary object, the anonymous variable's lifetime is extended to the end of the binding variable's scope, so using `auto&&` or `auto` is safe. However, if you take a pointer or reference to the bound variable and pass it out, there is a risk of dangling:

```cpp
auto& [x, y] = get_temp_pair(); // get_temp_pair returns a temporary
// &x is a dangling reference after this line!
```

### Cannot Be Used Directly as Return Values

The variable names from structured binding cannot be used directly as function return values. If you want to return the unpacked values, you need to repack them:

```cpp
auto [x, y] = get_pair();
// return x, y; // Error
return std::make_pair(x, y); // OK
```

### Cannot Be Used for Class Member Declarations

You cannot use structured binding in class member declarations:

```cpp
struct S {
    auto [x, y] = get_pair(); // Error
};
```

If you need to store unpacked values, use a struct or `std::tuple`/`std::pair` members instead.

------

## Run Online

Run the structured binding examples online to experience unpacking with `pair`, `tuple`, arrays, and structs:

<OnlineCompilerDemo
  title="Structured Binding: Unpacking pair, tuple, arrays, and structs"
  source-path="code/examples/vol2/11_structured_bindings.cpp"
  description="Run online and observe the unpacking effect of structured binding on pair, tuple, arrays, and structs."
  allow-run
/>

## Summary

Structured binding is one of the most practical features in C++17. It covers the vast majority of daily development scenarios: `std::pair`, `std::tuple`, native arrays, structs with public members, and custom types implementing the tuple-like protocol. The binding semantics are entirely determined by the modifier before `auto`—`auto` is a copy, `auto&` is a reference, `const auto&` is a read-only reference, and `auto&&` is a forwarding reference.

In practice, I most commonly use it for iterating over maps in range-based for loops (`const auto& [key, value]`) and handling multi-return functions. Combined with the if/switch initializers discussed in the next chapter, structured binding can take code conciseness and readability to the next level.

## References

- [cppreference: Structured binding declaration](https://en.cppreference.com/w/cpp/language/structured_binding)
- [Structured bindings in C++17, 8 years later - C++ Stories](https://www.cppstories.com/2025/structured-bindings-cpp26-updates/)
- [Adding structured bindings to your classes - Sy Brand](https://tartanllama.xyz/structured-bindings/)
