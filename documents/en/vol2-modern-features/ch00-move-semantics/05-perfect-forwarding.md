---
title: 'Perfect Forwarding: Keeping Value Categories Intact'
description: Understand reference collapsing and universal references, and master the correct use of std::forward
chapter: 0
order: 5
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
  - 'Chapter 0: Rvalue References: From Copy to Move'
  - 'Chapter 0: Move Construction and Move Assignment'
related:
  - 'Move Semantics in Practice: Standard Library Containers and Performance Benchmarks'
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/05-perfect-forwarding.md
  source_hash: 4e5b74f73e719899a4d1e2436e3a1d8ab4a1ab050faefb48868f4f0b1e0c658a
  translated_at: '2026-09-27T04:34:57+00:00'
  engine: anthropic
  token_count: 10500
---
# Perfect Forwarding: Keeping Value Categories Intact

Anyone who has written a template function has probably hit this dilemma: receive an argument, pass it along to another function, and you want the callee to see an lvalue when the caller passed an lvalue, and an rvalue when the caller passed an rvalue. Sounds simple, right? Before C++11, though, this was nearly impossible: either you wrote two overloads (one taking an lvalue reference, one taking an rvalue reference), or you just took everything by const reference, threw away the information that "this is an rvalue", and lost the performance benefits of move semantics along with it. Good grief—efficiency and performance just refused to coexist. Annoying!

But fear not: C++11 also brought us perfect forwarding, which solves exactly this problem. It lets us write a single template that forwards an argument's value category, untouched, to the target function.

In one sentence: handing a parameter off somewhere else used to mean writing both a `const T&` version and a `T&&` version; not anymore—forward (or if you like, pass through) with `std::forward`.

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

Version one accepts lvalues, but an rvalue passed in also gets copied—because you receive it by const reference, the information "this is an rvalue" is lost. Version two accepts rvalues and moves correctly, but passing an lvalue fails to compile outright, because an rvalue reference cannot bind to an lvalue.

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

Two parameters? Four overloads (`const&` + `const&`, `const& &&`, `&& const&`, `&& &&`—2×2). Three parameters? Eight. Doom—in a real project with a pile of members to handle, writing this way is guaranteed to blow up; clearly not sustainable.

## Universal References—Not Every T&& Is an Rvalue Reference

Scott Meyers gave this special `T&&` a name: "universal references"<RefLink :id="1" preview="Scott Meyers, Effective Modern C++, Item 24 — distinguish universal references from rvalue references" />; the C++ standard's term is "forwarding references"<RefLink :id="2" preview="cppreference References — forwarding references and reference collapsing" />. They look exactly like rvalue references (honestly, I'm a little puzzled as to why too—if any C++ heavyweight out there can explain why they had to look identical, I'm all ears!), but they behave completely differently.

The key difference lies in **the context of type deduction**. An ordinary rvalue reference like `std::string&&` binds only to rvalues—that's fixed. But a `T&&` in the context of template argument deduction adjusts itself based on the argument you pass: pass an lvalue, and `T` is deduced as an lvalue reference type, so `T&&` becomes an lvalue reference through reference collapsing; pass an rvalue, and `T` is deduced as a non-reference type, leaving `T&&` a plain rvalue reference.

```cpp
template<typename T>
void identify(T&& arg)
{
    // Is arg an lvalue reference or an rvalue reference? It depends on the argument passed at the call site
}

std::string name = "Alice";

identify(name);              // lvalue passed, T = std::string&, T&& = std::string&
identify(std::string("Bob")); // rvalue passed, T = std::string, T&& = std::string&&
```

A universal reference requires two necessary conditions—miss either and it isn't one: first, the type must be deduced through a template parameter (the `T` in `template<typename T>`); second, the declaration must have exactly the form `T&&`, with no const or other qualifiers added. Write `const T&&` and you have an ordinary const rvalue reference, not a universal reference. Write `std::vector<T>&&` and that isn't one either: `T` does get deduced, but `std::vector<T>&&` as a whole is not of the form `T&&`.

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

## Reference Collapsing—The Outcome of Four Combinations

This section draws heavily on *Effective Modern C++*<RefLink :id="1" preview="Scott Meyers, Effective Modern C++, Item 28 — understand reference collapsing" />:

Universal references work the way they do thanks to **reference collapsing**<RefLink :id="2" preview="cppreference Reference collapsing — four combinations, lvalue wins" />. When the compiler deduces `T&&`, a "reference to a reference" can appear—for instance, if `T` is deduced as `std::string&`, then `T&&` becomes `std::string& &&`. C++ doesn't allow you to write a "reference to a reference" directly, but in the context of template deduction, the compiler collapses it according to four rules:

`T& &` collapses to `T&`, `T& &&` collapses to `T&`, `T&& &` collapses to `T&`, and `T&& &&` collapses to `T&&`.

No need to memorize all four by rote; one compact rule is enough: **if either side is an lvalue reference (`&`), the result is an lvalue reference**. Only when both are rvalue references (`&& &&`) does the result stay an rvalue reference.

Let's verify with the concrete deduction process. When you pass the lvalue `name`, `T` is deduced as `std::string&`, so `T&&` becomes `std::string& &&`, which the second rule collapses to `std::string&`—the parameter type is an lvalue reference. When you pass the rvalue `std::string("Bob")`, `T` is deduced as `std::string` (a non-reference type), so `T&&` is just `std::string&&`—the parameter type is an rvalue reference, and no collapsing happens because there was never a "reference to a reference" to begin with.

```cpp
template<typename T>
void show_type(T&& arg)
{
    // Use type_traits to inspect the deduced type
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

Reference collapsing isn't confined to function templates. It also kicks in during `auto&&` deduction, in the instantiation of `typedef` and `using` aliases, and in certain uses of `decltype`. Still, universal references in function templates are by far the most common scenario.

## std::forward—A Conditional Cast

Alright, here's the real point (if all you care about is how to use it). Once you understand universal references and reference collapsing, `std::forward` is simple. Its job: **when what was passed in is an rvalue, cast the parameter to an rvalue reference; when it's an lvalue, keep it an lvalue reference unchanged**. In essence, it's a conditional, smarter `static_cast`<RefLink :id="3" preview="cppreference std::forward — conditional cast that restores the original value category" />. (In one line: hey, this little thing remembers whether you passed an lvalue or an rvalue, and passes it along to elsewhere exactly as it came.)

Where exactly the value category gets lost, and how `std::forward` keeps it intact, has been turned into an animation: you can play it, pause it, or single-step through it with the step button, following each of the two paths—lvalue and rvalue—once through:

<Anim id="perfect-forwarding" />

We can implement a simplified version ourselves to understand how it works:

```cpp
// A simplified implementation of std::forward
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

These two overloads, together with reference collapsing, carry out the "conditional cast" logic. When an lvalue is passed, `T` is deduced as `U&` (U being the actual type), so `static_cast<T&&>` is `static_cast<U& &&>`, which collapses to `U&`—an lvalue reference is returned. When an rvalue is passed, `T` is deduced as `U`, so `static_cast<T&&>` is `static_cast<U&&>`—an rvalue reference is returned.

The key insight: the "conditional" part of `std::forward` comes from **the template parameter `T` carrying the value-category information of the original argument**—it isn't in `std::forward`'s own logic. When a universal reference receives an lvalue, `T` is deduced as `U&`, and that `&` acts like a stamp, imprinting the information "this is an lvalue" into the type. `std::forward` "un-stamps" it through `static_cast<T&&>` and reference collapsing.

## Perfect Forwarding in the Standard Library

Perfect forwarding is everywhere in the C++ standard library. The classic examples are `std::make_unique` and `std::make_shared`: they accept arbitrary arguments and forward them, untouched, to the constructor of the object managed by the `unique_ptr`/`shared_ptr`.

```cpp
// A simplified implementation of std::make_unique
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
```

Here `Args&&... args` is a pack of universal references. Each `Args` is deduced independently, so if you pass one lvalue and one rvalue, each of their value categories is preserved. `std::forward<Args>(args)...` forwards every argument to `T`'s constructor with its original value category.

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

Another classic example is `std::vector::emplace_back`. It takes constructor arguments rather than ready-made objects, constructing the new element in place, directly in the vector's memory—more efficient than `push_back`, saving even the move.

```cpp
std::vector<std::string> words;
words.emplace_back("hello");          // constructs std::string("hello") directly in the vector
words.emplace_back(std::string("hi")); // rvalue passed, move-constructed

std::string word = "world";
words.emplace_back(std::move(word));   // rvalue passed, move-constructed
```

## Common Mistakes—What Not to forward

Powerful as `std::forward` is, using it in the wrong place introduces subtle bugs. The most important rule: **use `std::forward` only on universal references**.

```cpp
// Mistake 1: using std::forward on a non-universal reference
void process(const std::string& s)
{
    // s is not a universal reference! It's a const lvalue reference, a fixed type
    // std::forward<const std::string&>(s) always returns a const lvalue reference
    // Using std::forward here is pointless, and it easily misleads readers
    consume(std::forward<const std::string&>(s));  // don't do this
    consume(s);  // just pass it directly
}
```

In an ordinary non-template function, the parameter types are fixed—there is no "decide lvalue or rvalue based on the argument passed in" going on. Using `std::forward` on such a fixed-type parameter is pure troublemaking; it only blurs the intent of the code.

```cpp
// Mistake 2: forwarding the same parameter multiple times
template<typename T>
void double_forward(T&& x)
{
    target(std::forward<T>(x));  // first forward
    target(std::forward<T>(x));  // dangerous! If x is an rvalue, the first one already "stole" it
}
```

If `x` is an rvalue reference, the first `std::forward<T>(x)` turns `x` into an rvalue to pass to `target`, and `target` may already have stolen `x`'s resources. By the time you forward again, `x` is in a "valid but unspecified" state, and you're handing out an rvalue that may already be hollowed out. This is the so-called use-after-move: the compiler won't report an error, but the runtime behavior is unpredictable.

```cpp
// Mistake 3: std::forward + decltype(auto) in a return statement
template<typename T>
decltype(auto) bad_return(T&& x)
{
    return (std::forward<T>(x));  // dangerous! May return a dangling reference
}
```

Here `decltype(auto)` deduces the return type from the `return` expression, so the return type depends on the result of `std::forward<T>(x)`. When you pass in an rvalue, `T` is deduced as a non-reference type (say, `std::string`), `std::forward<std::string>(x)` returns `std::string&&`, and the return type `decltype(auto)` deduces is `std::string&&`. But this rvalue reference points at the function parameter `x`, which is destroyed the moment the function returns. The reference the caller gets points at memory that no longer exists—a textbook dangling reference, and GCC's `-Wdangling-reference` will warn about it<RefLink :id="4" preview="GCC Warning Options — -Wdangling-reference" />.

When an lvalue is passed, `T` is deduced as `U&` (say, `std::string&`), and `std::forward<std::string&>(x)` returns `std::string&` through reference collapsing; the reference chain ultimately points at the caller's original variable, which is still alive, so that path is safe. The problem is that this function template is safe for lvalues and dangerous for rvalues, while `decltype(auto)` cannot express that distinction in its signature—during maintenance, it's very easy to misuse.

If you truly need to forward in a return statement, make sure the return type is a value type (`T` rather than `decltype(auto)`), so that in the rvalue case a move construction is triggered instead of a reference being returned. The `emplace_get` in the cache wrapper in the next section is a correct example: it returns `Value&` (a fixed type, not something forwarded), and uses `std::forward` only on its parameters.

## A General Example—A Generic Cache Wrapper

Let's use perfect forwarding to write a practical example: a general-purpose cache wrapper template that can cache the results of arbitrary function calls and perfectly forwards all arguments.

```cpp
// perfect_forwarding.cpp -- a perfect forwarding demo
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>
#include <map>
#include <functional>
#include <memory>

/// @brief A simple cache wrapper
/// Perfectly forwards function arguments while preserving value category information
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

/// @brief The "expensive" operation being wrapped
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

/// @brief A general-purpose forwarding wrapper—demonstrates the core usage of perfect forwarding
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

    // First call: cache miss, constructs a new value
    // An rvalue string and an integer are passed in
    cache.emplace_get("alpha", "first", 100);

    // Second call: same key, cache hit
    cache.emplace_get("alpha", "first", 200);

    // New key, an rvalue string is passed in (single-argument constructor)
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
    // Demonstrates the effect of perfect forwarding in constructor argument passing
    auto data = std::make_unique<ExpensiveData>("gamma", 42);
    std::cout << "  data: " << data->label() << " = " << data->value() << "\n\n";

    std::cout << "=== 程序结束 ===\n";
    return 0;
}
```

This complete code is right below—click "Try it yourself" to run it directly:

<OnlineCompilerDemo
  title="Hands-On Experiment: perfect_forwarding.cpp"
  source-path="code/examples/vol2/18_perfect_forwarding.cpp"
  description="Run the three demos—the cache wrapper, the forwarding wrapper, and the make_unique factory—online. Note that the second lookup of the same key takes the cache-hit path and constructs nothing."
  run-options="-std=c++17"
  allow-run
/>

The `Args&&... args` in `emplace_get` is a universal-reference parameter pack. When you pass in `("first", 100)`, `Args` is deduced as `const char (&)[6]` and `int` (loosely understood as `const char*` and `int`). `std::forward<Args>(args)...` forwards these arguments, untouched, to `ExpensiveData`'s constructor: the parameter types and value categories the constructor receives are exactly the same as if you had passed them to it directly.

When `std::move(label)` is passed in, `Args` is deduced as `std::string` (non-reference), `std::forward` turns it into an rvalue reference, and `ExpensiveData`'s `std::string` parameter is initialized via move construction, avoiding a deep copy of the string. Such is the power of perfect forwarding: one template, automatically handling every combination of value categories.

## Hands-On Experiment—Verifying Reference Collapsing

To deepen our understanding, let's write a small program that uses `std::is_same_v` to verify the outcome of reference collapsing:

```cpp
// ref_collapsing.cpp -- reference collapsing verification
// Standard: C++17

#include <iostream>
#include <type_traits>
#include <string>

template<typename T>
void show_deduction(T&& /* arg */)
{
    // The deduction result of T
    if constexpr (std::is_lvalue_reference_v<T>) {
        std::cout << "  T = 左值引用类型\n";
    } else {
        std::cout << "  T = 非引用类型（右值）\n";
    }

    // The final type of T&& (after reference collapsing)
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

This verification program is right below—click "Try it yourself" to run it directly:

<OnlineCompilerDemo
  title="Hands-On Verification: ref_collapsing.cpp"
  source-path="code/examples/vol2/04_perfect_forwarding.cpp"
  description="Verify reference collapsing online: when an lvalue is passed in (const or not), T&& collapses to an lvalue reference; when an rvalue is passed in, T&& stays an rvalue reference."
  run-options="-std=c++17"
  allow-run
/>

This set of output confirms the reference collapsing rules perfectly: when an lvalue is passed in (const or not), `T` is deduced as a reference type and `T&&` collapses to an lvalue reference. When an rvalue is passed in, `T` is deduced as a non-reference type and `T&&` is an rvalue reference. The const information travels through `T` as well: although this simplified program doesn't distinguish const from non-const, `T` genuinely contains the const qualifier, and `std::forward` preserves it correctly.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Scott Meyers"
    title="Effective Modern C++: 42 Specific Ways to Improve Your Use of C++11 and C++14"
    publisher="O'Reilly Media"
    :year="2014"
    chapter="Item 24: universal references; Item 28: reference collapsing"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="References"
    chapter="Reference collapsing; Forwarding references"
    url="https://en.cppreference.com/w/cpp/language/reference"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="std::forward"
    url="https://en.cppreference.com/w/cpp/utility/forward"
  />
  <ReferenceItem
    :id="4"
    author="GCC"
    title="Warning Options (-Wdangling-reference)"
    publisher="gcc.gnu.org"
    url="https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html"
  />
</ReferenceCard>
