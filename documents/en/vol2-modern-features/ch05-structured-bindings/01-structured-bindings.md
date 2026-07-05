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
- if/switch initializers
tags:
- host
- cpp-modern
- intermediate
title: 'Structured Binding: Unpacking Multiple Values in One Line'
translation:
  source: documents/vol2-modern-features/ch05-structured-bindings/01-structured-bindings.md
  source_hash: 7d6b4bfb92245b45aca6741e936ad295899a1c75d9b52333111a9b1f51fe3dec
  translated_at: '2026-07-05T04:06:39.000000+00:00'
  engine: manual
  token_count: 2200
---
# Structured Binding: Unpacking Multiple Values in One Line

When I'm writing code, I keep bumping into an awkward scenario: a function returns multiple values, and you have to unpack them one by one into variables. With `pair` you write `result.first`, `result.second`; with `tuple` you write `std::get<0>(t)`—either the semantics are unclear or the syntax is ugly. C++11 brought `std::tie` to ease this, but honestly that syntax isn't elegant either: you declare all the variables first, then use `tie` to stuff values in. Isn't there something as satisfying as Python's `a, b = func()`? Yes, there is, folks.

C++17 finally gave a real answer—Structured Binding. One line unpacks `pair`, `tuple`, arrays, and structs into named variables. Clear semantics, zero overhead.

------

## Starting with pair and tuple

### pair: the most common multi-return

`std::pair` is the most common "pack two values" type in the standard library. `std::map::insert` returns a `pair<iterator, bool>`, and `std::map::find` returns a `pair<const Key, Value>&`. Before structured binding, you had to write:

```cpp
auto result = m.insert({1, "one"});
if (result.second) {
    std::cout << "Inserted: " << result.first->second << '\n';
}
```

What does `result.second` mean? Without checking docs you have no idea. Structured binding writes the semantics straight into the variable names:

```cpp
auto [it, inserted] = m.insert({1, "one"});
if (inserted) {
    std::cout << "Inserted: " << it->second << '\n';
}
```

It's downright elegant when iterating a map in a range-based for loop. You used to write `it->first` and `it->second`; now it's just `[key, value]`:

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

One detail: the loop body writes `+id`, not `id`. Why? Because `uint8_t`'s `operator<<` treats it as a char, while `+` performs integral promotion, forcing it to `int` before printing. Don't take my word for it—running it is the clearest proof (GCC 16.1.1, `-O2`):

```text
without + (raw uint8_t): A
with +    (promoted)   : 65
```

Same `id = 65`: without `+` you get the char `A`; with `+` you get the number.

### tuple: more than two values

When a function needs to return three or more values, `std::tuple` is the natural choice. The structured-binding syntax is exactly the same as for `pair`:

```cpp
std::tuple<int, std::string, double> query_database(int id) {
    return {id, "sensor_" + std::to_string(id), 23.5};
}

auto [record_id, name, value] = query_database(42);
```

### Compared to std::tie

C++11's `std::tie` can do something similar, but the ergonomics are noticeably worse. It makes you declare all variables first, then assign through `tie`:

```cpp
int record_id;
std::string name;
double value;
std::tie(record_id, name, value) = query_database(42);
```

The comparison is obvious: structured binding does declaration and unpacking in one step, while `std::tie` needs two. `std::tie` does use references internally, so it can handle tuples with non-copyable types (like `std::unique_ptr`)—reference binding doesn't copy. But structured binding has cleaner syntax and supports multiple semantics: by value, by reference, by forwarding reference.

------

## Native arrays and structs

### Native arrays

Fixed-size native arrays unpack directly too. Handy when processing fixed-format data:

```cpp
int rgb[3] = {255, 128, 0};
auto [r, g, b] = rgb;
```

Each row of a 2D array can be unpacked in a loop:

```cpp
int matrix[2][3] = {
    {1, 2, 3}, {4, 5, 6}
};
for (auto& row : matrix) {
    auto [a, b, c] = row;
    std::cout << a << ' ' << b << ' ' << c << '\n';
}
```

Note that structured binding only supports direct unpacking of one-dimensional arrays. You can't write `auto [a, b, c, d, e, f] = matrix`, because `matrix` is essentially `int[2][3]`—its size is 2, not 6.

### Structs and classes

If every non-static data member of a struct is `public`, the struct can be unpacked directly. The compiler binds members in declaration order:

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

No template metaprogramming needed—as long as the members are public, it just works. This is arguably the most intuitive use of structured binding.

Structured binding requires members to be bound in declaration order, and it fully supports bit fields. If the struct has `mutable` members, watch out: the bound "anonymous variable" may be `const`-qualified, but `mutable` members aren't affected and stay modifiable.

------

## The three binding semantics

Structured binding doesn't always copy. The modifier in front of `auto` decides the type of the underlying anonymous variable:

- **`auto [...]`**—copy by value. The bound names refer to this copy.
- **`auto& [...]`**—binds to an lvalue reference. You can modify the original.
- **`const auto& [...]`**—binds to a const lvalue reference. Read-only, no copy.
- **`auto&& [...]`**—forwarding reference. Binds to both lvalues and rvalues.

One example to tell them apart:

```cpp
std::pair<int, int> range{1, 10};

// Copy: r1, r2 refer to an anonymous copy, don't affect range
auto [r1, r2] = range;

// Reference: operate on the original directly
auto& [r3, r4] = range;
r3 = 5;  // range.first becomes 5
```

Run it and you can see `auto&` mutates the original, `auto` mutates a copy:

```text
range.first  after auto& mutation: 5
r1 (copy,    unaffected)         : 1
```

The underlying mechanism: the compiler first declares an anonymous variable (type decided by `auto`/`auto&`/`const auto&`/`auto&&`) and initializes it with the right-hand side. Then each bound variable is a reference to a member of that anonymous variable (or, for the by-value case, a reference to a member of the copy).

```cpp
// auto [x, y] = get_point(); is roughly equivalent to:
auto __anonymous = get_point();
auto& x = __anonymous.first;   // refers to the anonymous variable's member
auto& y = __anonymous.second;
```

So the bound variables are always references—they refer to members of the hidden anonymous object. You can't take the address of "the bound variable itself"; you can only take the address of the sub-object it refers to.

⚠️ Note: `auto&` requires the right-hand side to be an lvalue. If the right-hand side is a temporary (like the return value of `std::make_pair(1, 2)`), `auto&` fails to compile—a non-const reference can't bind to an rvalue. Use `const auto&` or plain `auto` to copy instead.

```cpp
// Error: auto& can't bind to a temporary
auto& [x, y] = std::make_pair(1, 2);

// OK: const reference extends the temporary's lifetime
const auto& [x, y] = std::make_pair(1, 2);

// Or just copy
auto [x, y] = std::make_pair(1, 2);
```

------

## Making custom types bindable: the tuple-like protocol

If your class has private members, you can't use the struct route. But C++ offers another path: tell the compiler to treat your class as a "tuple-like" type. You need three things:

1. Specialize `std::tuple_size<YourType>` to tell the compiler how many elements there are.
2. Specialize `std::tuple_element<I, YourType>` to tell it the type of the `I`-th element.
3. Provide a `get<I>()` function in `YourType`'s namespace that returns the `I`-th element.

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

// Specialize tuple_size: tell the compiler there are 2 elements
template<>
struct std::tuple_size<SensorData> : std::integral_constant<std::size_t, 2> {};

// Specialize tuple_element: tell the compiler each element's type
template<>
struct std::tuple_element<0, SensorData> { using type = uint8_t; };

template<>
struct std::tuple_element<1, SensorData> { using type = float; };
```

With ADL overloads for `get<I>`, you can now happily unpack it:

```cpp
SensorData data{5, 23.5f};
auto [id, value] = data;    // id = 5, value = 23.5
```

Run it to confirm (note `id` again needs `+` to print as a number):

```text
id = 5, value = 23.5
```

> The key here is that `get<I>()` must be defined in the class's namespace (ADL rules) so the compiler can find it. For specializations that live in `std`, you write the `tuple_size` and `tuple_element` specializations inside `namespace std`, but the `get` function can stay in the class's namespace.

This mechanism is called the "tuple-like protocol." The standard library's `std::pair`, `std::tuple`, and `std::array` all rely on it for structured binding support.

------

## Changes in C++20

C++20 made a few tweaks to structured binding, mostly around `constexpr` contexts.

Structured binding can now be used inside `constexpr` functions, meaning compile-time computation can return multiple values and receive them via structured binding:

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

Note, though, that you can't declare a `constexpr` structured binding at namespace scope (e.g., `constexpr auto [x, y] = get_point();` is a compile error). That's because structured binding is fundamentally a declaration of a set of reference variables, not a single variable.

On lambda captures: C++17 already supports capturing structured-binding variables directly. This works in C++17:

```cpp
std::map<int, std::string> m = {{1, "one"}, {2, "two"}};

for (const auto& [k, v] : m) {
    auto callback = [k, v] {  // direct capture, valid in C++17
        std::cout << k << ": " << v << '\n';
    };
    callback();
}
```

What C++20 adds is the init-capture syntax (`key = k`), which is more flexible in some cases. But note: `[=]` default capture does not capture structured-binding variables automatically—you have to list them explicitly.

------

## Performance: zero-overhead syntactic sugar

Structured binding has no runtime overhead. It's purely a compile-time syntactic transformation—the compiler creates an anonymous variable behind the scenes and has the bound variables refer to its members.

```cpp
// These two generate identical assembly
auto [x, y] = get_point();

// equivalent to
auto __tmp = get_point();
auto x = __tmp.first;
auto y = __tmp.second;
```

"Identical assembly" is not a claim to make empty-handed. Compile both with GCC 16.1.1, `g++ -std=c++17 -O2 -S` each, then `diff`:

```bash
g++ -std=c++17 -O2 -S sb_structured.cpp
g++ -std=c++17 -O2 -S sb_manual.cpp
diff sb_structured.s sb_manual.s
```

The `diff` output is a single line—the `.file` header differs (source filename); the actual instructions are identical:

```text
_Z1fv:                  # f(), both versions identical
    movl    $7, %eax    # returns 3 + 4 = 7 directly
    ret
```

The compiler inlined `get_point()` and constant-folded it down to `movl $7, %eax`—structured binding left no trace. So the performance advice is simple: use `const auto&` for large structs to avoid copies, and `auto` to copy small types (built-ins, small structs). `auto&&` is useful in generic code, but when the concrete type is known, writing `auto` or `const auto&` explicitly is clearer.

------

## Common pitfalls

### Lifetime issues

When `auto&&` binds to a temporary, the anonymous variable's lifetime is extended to the end of the binding's scope, so `auto&&` and `const auto&` are safe. But if you take a pointer or reference to the bound variable and pass it out, you've got a dangling-reference risk:

```cpp
const auto& [x, y] = std::make_pair(1, 2);
// x, y are valid within this scope—safe.
// But if &x is stored outside, it dangles after the scope ends.
```

### Can't be a return value directly

Structured-binding names can't be used directly as a function return. If you want to return the unpacked values, you have to repack:

```cpp
auto [x, y] = get_point();
// can't: return x, y; must repack
return std::make_pair(x, y);

// or just return the function's result
return get_point();
```

### Can't be a class member declaration

You can't use structured binding in a class member declaration:

```cpp
class MyClass {
    auto [x, y] = get_point();  // compile error
};
```

If you need to store unpacked values, use a struct or `pair`/`tuple` members instead.

------

## Run online

Run the structured-binding examples online and see unpacking for `pair`, `tuple`, arrays, and structs:

<OnlineCompilerDemo
  title="Structured Binding: Unpacking pair, tuple, arrays, and structs"
  source-path="code/examples/vol2/11_structured_bindings.cpp"
  description="Run online and observe the unpacking effect of structured binding on pair, tuple, arrays, and structs."
  allow-run
/>

## Wrapping up

That's the full coverage of types structured binding handles: `pair`, `tuple`, native arrays, structs with public members, plus custom types that implement the tuple-like protocol. The semantics are entirely decided by the modifier in front of `auto`—`auto` copies, `auto&` references, `const auto&` is read-only, `auto&&` forwards.

What I actually use day to day is range-based for over a map (`for (const auto& [k, v] : m)`) and catching multi-return functions. Pair it with the if/switch initializers in the next chapter and your code shrinks another size.

## References

- [cppreference: Structured binding declaration](https://en.cppreference.com/w/cpp/language/structured_binding)
- [Structured bindings in C++17, 8 years later - C++ Stories](https://www.cppstories.com/2025/structured-bindings-cpp26-updates/)
- [Adding structured bindings to your classes - Sy Brand](https://tartanllama.xyz/structured-bindings/)
