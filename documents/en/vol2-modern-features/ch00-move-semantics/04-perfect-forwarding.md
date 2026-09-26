---
title: "Perfect Forwarding: Preserving Value Categories Exactly"
description: "Understand reference collapsing and universal references, and master the correct use of std::forward"
chapter: 0
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 移动语义
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 18
prerequisites:
  - "Chapter 0: Rvalue References: From Copy to Move"
  - "Chapter 0: Move Construction and Move Assignment"
related:
  - "Move Semantics in Practice: From STL to Custom Types"
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/04-perfect-forwarding.md
  source_hash: b4a6fa3e31eb9c4a532eb3e334d60c6dcf4f11942d8bc01a89cfb556f0a4cab6
  translated_at: '2026-09-25T14:30:57+00:00'
  engine: anthropic
  token_count: 9800
---
# Perfect Forwarding: Preserving Value Categories Exactly

Anyone who has written a template function has probably hit this dilemma: take an argument, then hand it off to another function — when an lvalue goes in, you want the other side to receive an lvalue; when an rvalue goes in, you want it to receive an rvalue. Sounds simple, right? But before C++11 this was nearly impossible: you either wrote two overloads (one taking an lvalue reference, one taking an rvalue reference), or you just took everything by const reference and threw away the "this is an rvalue" information — along with the performance benefits of move semantics. Man, you just couldn't have both efficiency and performance — so annoying!

But don't worry — perfect forwarding, which arrived alongside C++11, is here to solve exactly this problem. It lets us write a single template that forwards a parameter's value category, untouched, to the target function.

In one sentence: handing an argument off somewhere else used to mean writing both a `const T&` and a `T&&` overload. Not anymore — use `std::forward` to forward it (or, if you like, pass it through).

## Starting from a Real Problem

Suppose we're writing a simple factory function that creates `std::string` objects:

```cpp
// Version 1: take by const reference
std::string make_string(const std::string& s)
{
    return std::string(s);  // always copy-constructs
}

// Version 2: take by rvalue reference
std::string make_string(std::string&& s)
{
    return std::string(std::move(s));  // always move-constructs
}
```

Version 1 accepts lvalues, but it also copies when you pass an rvalue: receiving by const reference throws away the "this is an rvalue" information. Version 2 accepts rvalues and moves correctly, but passing an lvalue is a straight compile error, because an rvalue reference cannot bind to an lvalue.

To support both cases, you have to write two overloads:

```cpp
std::string make_string(const std::string& s)
{
    return std::string(s);
}

std::string make_string(std::string&& s)
{
    return std::string(std::move(s));
}
```

Two parameters? Four overloads (const& + const&, const& &&, && const&, && && — that's 2×2). Three parameters, eight. Doomed. Real projects have piles of members to handle; written this way the code will absolutely blow up. Clearly this doesn't scale.

## Universal References—Not Every T&& Is an Rvalue Reference

Scott Meyers gave this special `T&&` the name "universal reference"; the C++ standard term is "forwarding reference". It looks exactly like an rvalue reference (honestly, I can't quite figure out why either — if any C++ guru out there can explain why they simply had to look identical, I'm all ears!), but it behaves completely differently.

The key difference is the **type deduction context**. An ordinary rvalue reference `std::string&&` binds only to rvalues — that's fixed. But a `T&&` undergoing template parameter deduction adjusts itself to the argument: pass an lvalue, and `T` is deduced as an lvalue reference type, with `T&&` collapsing into an lvalue reference; pass an rvalue, and `T` is deduced as a non-reference type, so `T&&` is an rvalue reference.

```cpp
template<typename T>
void identify(T&& arg)
{
    // is arg an lvalue reference or an rvalue reference? It depends on the argument passed at the call
}

std::string name = "Alice";

identify(name);              // lvalue: T = std::string&, T&& = std::string&
identify(std::string("Bob")); // rvalue: T = std::string, T&& = std::string&&
```

A universal reference has two necessary conditions, and neither can be missing: first, the type must go through template parameter deduction (the `T` in `template<typename T>`); second, the declared form must be exactly `T&&`, with no const or other modifiers. Write `const T&&` and it's an ordinary const rvalue reference, not a universal reference. Write `std::vector<T>&&` and it isn't one either — `T` does get deduced, but `std::vector<T>&&` as a whole isn't in the `T&&` form.

```cpp
template<typename T>
void forwarding(T&& x);      // universal reference ✓

template<typename T>
void not_forwarding(const T&& x);  // const rvalue reference, not a universal reference ✗

template<typename T>
void also_not(std::vector<T>&& x); // vector rvalue reference, not a universal reference ✗

// auto&& is also a universal reference (since C++11)
auto&& universal = some_expression;  // universal reference ✓
```

`auto&&` follows the same deduction rules: if `some_expression` is an lvalue, `universal` is an lvalue reference; if it's an rvalue, `universal` is an rvalue reference. This shows up all the time in range-based for loops and lambda captures.

## Reference Collapsing—The Final Result of the Four Combinations

This section draws heavily on *Effective Modern C++*:

Universal references work because of **reference collapsing**. When the compiler deduces `T&&`, a "reference to a reference" can appear — for example, if `T` is deduced as `std::string&`, then `T&&` becomes `std::string& &&`. C++ doesn't let you write "reference to a reference" directly, but in a template deduction context the compiler collapses it according to four rules:

`T& &` collapses to `T&`, `T& &&` collapses to `T&`, `T&& &` collapses to `T&`, and `T&& &&` collapses to `T&&`.

No need to memorize all four; one compact rule is enough: **if either one is an lvalue reference (`&`), the result is an lvalue reference**. Only when both are rvalue references (`&& &&`) is the result an rvalue reference.

Let's verify with the concrete deduction process. Pass the lvalue `name`, and `T` is deduced as `std::string&`, so `T&&` becomes `std::string& &&`, which by the second rule collapses to `std::string&` — the parameter type is an lvalue reference. Pass the rvalue `std::string("Bob")`, and `T` is deduced as `std::string` (a non-reference type), so `T&&` is just `std::string&&` — the parameter type is an rvalue reference. No collapsing happens, because there was never a "reference to a reference" to begin with.

```cpp
template<typename T>
void show_type(T&& arg)
{
    // use type_traits to inspect the deduced type
    using Decayed = std::decay_t<T>;

    if constexpr (std::is_lvalue_reference_v<T>) {
        std::cout << "  左值引用\n";
    } else {
        std::cout << "  右值引用（或非引用）\n";
    }
}

int main()
{
    std::string name = "Alice";
    show_type(name);                // T = std::string&, prints "左值引用"
    show_type(std::string("Bob"));  // T = std::string, prints "右值引用"
    show_type(std::move(name));     // T = std::string, prints "右值引用"
    return 0;
}
```

Reference collapsing doesn't happen only in function templates. The deduction of `auto&&`, the instantiation of `typedef` and `using` aliases, and certain uses of `decltype` all trigger it. Still, the universal reference in a function template is the most common case.

## std::forward—A Conditional Cast

Alright, here's the part that actually matters (if all you care about is how to use it). Once you understand universal references and reference collapsing, `std::forward` is simple. Its job: **when the argument was an rvalue, cast the parameter to an rvalue reference; when it was an lvalue, keep the lvalue reference unchanged**. It is essentially a conditional, smarter `static_cast`. (In one sentence: hey, this little thing remembers whether you passed an lvalue or an rvalue, and passes it through to somewhere else unchanged.)

Where exactly the value category gets lost, and how `std::forward` preserves it intact, has been turned into an animation — you can play it, pause it, or step through it one frame at a time, walking both the lvalue path and the rvalue path:

<Anim id="perfect-forwarding" />

We can implement a simplified version ourselves to understand how it works:

```cpp
// a simplified implementation of std::forward
template<typename T>
constexpr T&& my_forward(std::remove_reference_t<T>& t) noexcept
{
    return static_cast<T&&>(t);
}

template<typename T>
constexpr T&& my_forward(std::remove_reference_t<T>&& t) noexcept
{
    static_assert(!std::is_lvalue_reference_v<T>,
                  "Cannot forward an rvalue as an lvalue");
    return static_cast<T&&>(t);
}
```

These two overloads, together with reference collapsing, carry out the "conditional cast" logic. Pass an lvalue, and `T` is deduced as `U&` (where `U` is the actual type), so `static_cast<T&&>` is `static_cast<U& &&>`, which collapses to `U&` — an lvalue reference is returned. Pass an rvalue, and `T` is deduced as `U`, so `static_cast<T&&>` is `static_cast<U&&>` — an rvalue reference is returned.

The key insight: `std::forward`'s "conditionality" comes from **the template parameter `T` carrying the original argument's value-category information** — it does not live in `std::forward`'s own logic. When a universal reference receives an lvalue, `T` is deduced as `U&`, and that `&` acts like a stamp imprinting "this is an lvalue" into the type. `std::forward` "unstamps" it through `static_cast<T&&>` and reference collapsing.

## Perfect Forwarding in the Standard Library

Perfect forwarding is everywhere in the C++ standard library. The classic examples are `std::make_unique` and `std::make_shared`: they accept arbitrary arguments and forward them, unchanged, to the constructor of the object the `unique_ptr`/`shared_ptr` manages.

```cpp
// a simplified implementation of std::make_unique
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
```

Here `Args&&... args` is a parameter pack of universal references. Each `Args` is deduced independently, so if you pass one lvalue and one rvalue, each keeps its own value category. `std::forward<Args>(args)...` forwards every parameter to `T`'s constructor according to its original value category.

```cpp
struct User {
    std::string name;
    int id;

    User(std::string n, int i) : name(std::move(n)), id(i) {}
};

int main()
{
    std::string name = "Alice";
    auto user = std::make_unique<User>(std::move(name), 42);
    // std::move(name) is an rvalue → name is moved into User's constructor
    // 42 is an rvalue → int has no concept of "moving"; it's just pass-by-value

    auto user2 = std::make_unique<User>("Bob", 100);
    // "Bob" is a const char* rvalue → used to construct the std::string parameter
    return 0;
}
```

Another classic example is `std::vector::emplace_back`. What it takes are constructor arguments, not a ready-made object; it constructs the new element in place in the vector's own memory, which is more efficient than `push_back` — even the move is saved.

```cpp
std::vector<std::string> words;
words.emplace_back("hello");          // constructs std::string("hello") directly in the vector
words.emplace_back(std::string("hi")); // an rvalue is passed; move-constructs

std::string word = "world";
words.emplace_back(std::move(word));   // an rvalue is passed; move-constructs
```

## Common Mistakes—What Not to forward

`std::forward` is powerful, but used in the wrong place it introduces subtle bugs. The most important rule: **use `std::forward` only on universal references**.

```cpp
// mistake 1: using std::forward on a non-universal reference
void process(const std::string& s)
{
    // s is NOT a universal reference! It's a const lvalue reference, a fixed type
    // std::forward<const std::string&>(s) always returns a const lvalue reference
    // using std::forward here is pointless and misleading to readers
    consume(std::forward<const std::string&>(s));  // don't do this
    consume(s);  // just pass it directly
}
```

In an ordinary non-template function, the parameter type is fixed; there is no "decide lvalue or rvalue from the argument" going on. Using `std::forward` on such a fixed-type parameter just adds noise and blurs the code's intent.

```cpp
// mistake 2: forwarding the same parameter twice
template<typename T>
void double_forward(T&& x)
{
    target(std::forward<T>(x));  // first forward
    target(std::forward<T>(x));  // dangerous! if x is an rvalue, the first call already "stole" it
}
```

If `x` is an rvalue reference, the first `std::forward<T>(x)` turns `x` into an rvalue and passes it to `target`, which may well have already stolen `x`'s resources. By the second forward, `x` is in a "valid but unspecified" state, and you're sending out an rvalue that may already be empty. This is the so-called "use-after-move": the compiler won't complain, but the runtime behavior is unpredictable.

```cpp
// mistake 3: std::forward + decltype(auto) in a return statement
template<typename T>
decltype(auto) bad_return(T&& x)
{
    return (std::forward<T>(x));  // dangerous! may return a dangling reference
}
```

Here `decltype(auto)` deduces the return type from the `return` expression, so the return type depends on the result of `std::forward<T>(x)`. When you pass an rvalue, `T` is deduced as a non-reference type (say `std::string`), `std::forward<std::string>(x)` returns `std::string&&`, and `decltype(auto)` deduces the return type as `std::string&&`. But that rvalue reference points at the function parameter `x`, which is destroyed the moment the function returns. The caller ends up with a reference to memory that no longer exists — a classic dangling reference, and GCC's `-Wdangling-reference` will warn about it.

When you pass an lvalue, `T` is deduced as `U&` (say `std::string&`); `std::forward<std::string&>(x)` returns `std::string&` via reference collapsing, and the reference chain ultimately points at the caller's original variable, which is still alive — so it's safe. The problem is that this function template is safe for lvalues and dangerous for rvalues, while `decltype(auto)` cannot express that distinction in the signature, so it is very easy to misuse during maintenance.

If you really do need to forward in a return statement, make sure the return type is a value type (`T`, not `decltype(auto)`), so the rvalue case triggers a move construction instead of returning a reference. The `emplace_get` in the cache wrapper in the previous section is a correct example: it returns `Value&` (a fixed type, not something forwarded), and uses `std::forward` only on the parameters.

## A Worked Example: A Generic Cache Wrapper

Let's use perfect forwarding to write a practical example: a generic cache wrapper template that can cache the result of any function call while perfectly forwarding all the arguments.

```cpp
// perfect_forwarding.cpp -- a perfect forwarding demo
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>
#include <map>
#include <functional>

/// @brief A simple cache wrapper
/// that perfectly forwards function arguments while preserving value-category information
template<typename Key, typename Value>
class Cache
{
    std::map<Key, Value> storage_;

public:
    /// @brief Find or insert: if the key doesn't exist, construct the Value from args
    template<typename... Args>
    Value& emplace_get(const Key& key, Args&&... args)
    {
        auto it = storage_.find(key);
        if (it != storage_.end()) {
            std::cout << "  [缓存命中] key = " << key << "\n";
            return it->second;
        }

        std::cout << "  [缓存未命中] key = " << key << "，构造新值\n";
        auto [new_it, inserted] = storage_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(key),
            std::forward_as_tuple(std::forward<Args>(args)...)
        );
        return new_it->second;
    }

    std::size_t size() const { return storage_.size(); }
};

/// @brief The wrapped "expensive" operation
class ExpensiveData
{
    std::string label_;
    int value_;

public:
    /// @brief Construct from a string and an integer
    ExpensiveData(std::string label, int value)
        : label_(std::move(label))
        , value_(value)
    {
        std::cout << "  [ExpensiveData] 构造: " << label_
                  << " = " << value_ << "\n";
    }

    /// @brief Construct from a string (overload)
    explicit ExpensiveData(std::string label)
        : label_(std::move(label))
        , value_(0)
    {
        std::cout << "  [ExpensiveData] 构造(仅标签): " << label_ << "\n";
    }

    const std::string& label() const { return label_; }
    int value() const { return value_; }
};

/// @brief A generic forwarding wrapper — demonstrating the core use of perfect forwarding
template<typename Func, typename... Args>
auto invoke_and_log(Func&& func, Args&&... args)
    -> std::invoke_result_t<Func, Args...>
{
    std::cout << "  [invoke_and_log] 调用前\n";
    auto result = std::invoke(
        std::forward<Func>(func),
        std::forward<Args>(args)...
    );
    std::cout << "  [invoke_and_log] 调用后\n";
    return result;
}

int main()
{
    std::cout << "=== 1. 缓存包装器 ===\n";
    Cache<std::string, ExpensiveData> cache;

    // first call: cache miss, construct a new value
    // pass an rvalue string and an integer
    cache.emplace_get("alpha", "first", 100);

    // second call: same key, cache hit
    cache.emplace_get("alpha", "first", 200);

    // new key, pass an rvalue string (single-argument construction)
    std::string label = "beta";
    cache.emplace_get("beta", std::move(label));
    // label has been moved from; don't use it again

    std::cout << "  缓存大小: " << cache.size() << "\n\n";

    std::cout << "=== 2. 转发包装器 ===\n";
    auto add = [](int a, int b) -> int {
        return a + b;
    };

    int x = 10;
    int result = invoke_and_log(add, x, 20);
    std::cout << "  结果: " << result << "\n\n";

    std::cout << "=== 3. make_unique 风格的工厂 ===\n";
    // demonstrate the effect of perfect forwarding in constructor argument passing
    auto data = std::make_unique<ExpensiveData>("gamma", 42);
    std::cout << "  data: " << data->label() << " = " << data->value() << "\n\n";

    std::cout << "=== 程序结束 ===\n";
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o perfect_forwarding perfect_forwarding.cpp
./perfect_forwarding
```

Expected output:

```text
=== 1. 缓存包装器 ===
  [缓存未命中] key = alpha，构造新值
  [ExpensiveData] 构造: first = 100
  [缓存命中] key = alpha
  [缓存未命中] key = beta，构造新值
  [ExpensiveData] 构造(仅标签): beta
  缓存大小: 2

=== 2. 转发包装器 ===
  [invoke_and_log] 调用前
  [invoke_and_log] 调用后
  结果: 30

=== 3. make_unique 风格的工厂 ===
  [ExpensiveData] 构造: gamma = 42
  data: gamma = 42

=== 程序结束 ===
```

The `Args&&... args` in `emplace_get` is a universal reference parameter pack. When you pass `("first", 100)`, `Args` is deduced as `const char (&)[6]` and `int` (loosely, think `const char*` and `int`). `std::forward<Args>(args)...` forwards these arguments untouched to `ExpensiveData`'s constructor, so the parameter types and value categories the constructor sees are exactly what they would be if you passed them to it directly.

When you pass `std::move(label)`, `Args` is deduced as `std::string` (non-reference), `std::forward` turns it into an rvalue reference, and `ExpensiveData`'s `std::string` parameter is initialized by move construction, avoiding a deep copy of the string. That's the power of perfect forwarding: one template, automatically handling every combination of value categories.

## Hands-On Experiment—Verifying Reference Collapsing

To deepen our understanding, let's write a small program that uses `std::is_same_v` to verify the results of reference collapsing:

```cpp
// ref_collapsing.cpp -- reference collapsing verification
// Standard: C++17

#include <iostream>
#include <type_traits>
#include <string>

template<typename T>
void show_deduction(T&& /* arg */)
{
    // the deduction result of T
    if constexpr (std::is_lvalue_reference_v<T>) {
        std::cout << "  T = 左值引用类型\n";
    } else {
        std::cout << "  T = 非引用类型（右值）\n";
    }

    // the final type of T&& (after reference collapsing)
    using ParamType = T&&;
    if constexpr (std::is_lvalue_reference_v<ParamType>) {
        std::cout << "  T&& = 左值引用\n\n";
    } else {
        std::cout << "  T&& = 右值引用\n\n";
    }
}

int main()
{
    std::string name = "Alice";
    const std::string cname = "Bob";

    std::cout << "传入非 const 左值:\n";
    show_deduction(name);
    // T = std::string&, T&& = std::string& && → std::string&

    std::cout << "传入 const 左值:\n";
    show_deduction(cname);
    // T = const std::string&, T&& = const std::string& && → const std::string&

    std::cout << "传入右值（临时对象）:\n";
    show_deduction(std::string("Charlie"));
    // T = std::string, T&& = std::string&&

    std::cout << "传入右值（std::move）:\n";
    show_deduction(std::move(name));
    // T = std::string, T&& = std::string&&

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o ref_collapsing ref_collapsing.cpp
./ref_collapsing
```

Output:

```text
传入非 const 左值:
  T = 左值引用类型
  T&& = 左值引用

传入 const 左值:
  T = 左值引用类型
  T&& = 左值引用

传入右值（临时对象）:
  T = 非引用类型（右值）
  T&& = 右值引用

传入右值（std::move）:
  T = 非引用类型（右值）
  T&& = 右值引用
```

This output perfectly confirms the reference collapsing rules: when you pass an lvalue (const or not), `T` is deduced as a reference type and `T&&` collapses to an lvalue reference. When you pass an rvalue, `T` is deduced as a non-reference type and `T&&` is an rvalue reference. The const information also travels through `T`: even though this simplified program doesn't distinguish const from non-const, `T` really does carry the const modifier, and `std::forward` preserves it correctly.

## Run It Online

Run the reference collapsing example online and verify the type deduction rules of universal references:

<OnlineCompilerDemo
  title="Perfect Forwarding: Universal References and Reference Collapsing"
  source-path="code/examples/vol2/04_perfect_forwarding.cpp"
  description="Run it online and observe how the template parameter T is deduced when lvalues and rvalues are passed."
  allow-run
/>
