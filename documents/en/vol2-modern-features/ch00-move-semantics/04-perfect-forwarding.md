---
title: 'Perfect Forwarding: Preserving Exact Value Category Propagation'
description: Understand reference collapsing and universal references, and master
  the correct use of `std::forward`
chapter: 0
order: 4
tags:
- host
- cpp-modern
- intermediate
- 移动语义
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
reading_time_minutes: 18
prerequisites:
- 'Chapter 0: 右值引用'
- 'Chapter 0: 移动构造与移动赋值'
related:
- 移动语义实战
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/04-perfect-forwarding.md
  source_hash: f088cbb899ea3c805d83f68b9a0b1bb7872dcb14c8dbe1f2850172711f6043f3
  translated_at: '2026-07-16T00:00:00+00:00'
  engine: manual
  token_count: 3600
---
# Perfect Forwarding: Preserving Exact Value Category Propagation

Anyone who has written a template function that takes an argument and hands it off to another function has probably hit this dilemma: when an lvalue goes in, you want the other side to receive an lvalue; when an rvalue goes in, you want it to receive an rvalue. Sounds simple, right? But before C++11 this was nearly impossible. You either wrote two overloads (one taking an lvalue reference, one an rvalue reference), or just took everything by const reference and lost the "this is an rvalue" information along with the performance benefit of move semantics. Ugh, efficiency and performance couldn't both be had, annoying!

But don't worry, perfect forwarding, which arrived in the same C++11, is here to solve it. It lets us write a single template that forwards a parameter's value category, untouched, to the target function.

In one sentence: you used to always have to write both `const T&` and `T&&` to pass an argument along somewhere, not anymore, use `std::forward` to forward (or pass through) it.

## Starting from a Real Problem

Say we're writing a simple factory function to create `std::string` objects:

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

Version 1 accepts lvalues, but it also copies when you pass an rvalue, because the const reference reception threw away the "this is an rvalue" information. Version 2 accepts rvalues and moves correctly, but passing an lvalue is a hard compile error, because an rvalue reference won't bind to an lvalue.

To support both, you have to write two overloads:

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

Two parameters? Four overloads (const& + const&, const& &&, && const&, && &&, that's 2×2). Three parameters, eight. Doomed. Real projects have a pile of members to handle, writing it this way will absolutely blow up, it obviously doesn't scale.

## Universal References—Not Every T&& Is an Rvalue Reference

Scott Meyers gave this special `T&&` the name "universal reference"; the C++ standard term is "forwarding reference". It looks exactly like an rvalue reference (huh, I can't quite wrap my head around why either, any C++ veteran care to explain why they have to look identical? I'm all ears!), but it behaves completely differently.

The key difference is the **type-deduction context**. An ordinary rvalue reference `std::string&&` only binds to rvalues, that's fixed. But a `T&&` in template parameter deduction adjusts itself to the argument: pass an lvalue, and `T` is deduced as an lvalue reference type, with `T&&` collapsing into an lvalue reference; pass an rvalue, and `T` is deduced as a non-reference type, so `T&&` is an rvalue reference.

```cpp
template<typename T>
void identify(T&& arg)
{
    // is arg an lvalue reference or an rvalue reference? Depends on the argument at the call
}

std::string name = "Alice";

identify(name);              // lvalue, T = std::string&, T&& = std::string&
identify(std::string("Bob")); // rvalue, T = std::string, T&& = std::string&&
```

A universal reference has two necessary conditions, both required: first, the type must come through template parameter deduction (the `T` in `template<typename T>`); second, the declared form must be exactly `T&&`, no const or other modifiers. Write `const T&&` and it's an ordinary const rvalue reference, not a universal reference. Write `std::vector<T>&&` and it isn't one either; `T` is deduced, but `std::vector<T>&&` as a whole isn't in the `T&&` form.

```cpp
template<typename T>
void forwarding(T&& x);      // universal reference ✓

template<typename T>
void not_forwarding(const T&& x);  // const rvalue reference, not universal ✗

template<typename T>
void also_not(std::vector<T>&& x); // vector rvalue reference, not universal ✗

// auto&& is also a universal reference (since C++11)
auto&& universal = some_expression;  // universal reference ✓
```

`auto&&` follows the same deduction rules: if `some_expression` is an lvalue, `universal` is an lvalue reference; if it's an rvalue, `universal` is an rvalue reference. This shows up a lot in range-based for loops and lambda captures.

## Reference Collapsing—The Final Result of the Four Combinations

This section draws heavily from *Effective Modern C++*:

Universal references work because of **reference collapsing**. When the compiler deduces `T&&`, a "reference to a reference" can show up, for example if `T` is deduced as `std::string&`, then `T&&` becomes `std::string& &&`. C++ doesn't let you write "reference to a reference" directly, but in a template-deduction context the compiler collapses it according to four rules:

`T& &` collapses to `T&`, `T& &&` collapses to `T&`, `T&& &` collapses to `T&`, `T&& &&` collapses to `T&&`.

No need to memorize all four, one compact rule is enough: **if either one is an lvalue reference (`&`), the result is an lvalue reference**. Only when both are rvalue references (`&& &&`) is the result an rvalue reference.

Let's verify with the concrete deduction. Pass the lvalue `name`, and `T` is deduced as `std::string&`, so `T&&` becomes `std::string& &&`, which collapses by the second rule into `std::string&`, an lvalue reference parameter. Pass the rvalue `std::string("Bob")`, and `T` is deduced as `std::string` (a non-reference), so `T&&` is just `std::string&&`, an rvalue reference parameter. No collapsing happens, because there was no "reference to a reference" to begin with.

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

Reference collapsing doesn't only show up in function templates. The deduction of `auto&&`, the instantiation of `typedef` and `using` aliases, and some uses of `decltype` all trigger it. But the universal reference in function templates is the most common case.

## std::forward—A Conditional Cast

Alright, here's the important part (if you only care about how to use it). Once you understand universal references and reference collapsing, `std::forward` is straightforward. Its job: **when the argument was an rvalue, cast the parameter to an rvalue reference; when it was an lvalue, leave the lvalue reference alone**. It's essentially a conditional, smarter `static_cast`. (In one sentence, hey, this little thing remembers whether you passed an lvalue or an rvalue and passes it through unchanged.)

We can roll our own simplified version to understand how it works:

```cpp
// a simplified std::forward
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

These two overloads, together with reference collapsing, carry out the "conditional cast" logic. Pass an lvalue, and `T` is deduced as `U&` (U being the actual type), so `static_cast<T&&>` is `static_cast<U& &&>`, collapsing to `U&`, returning an lvalue reference. Pass an rvalue, and `T` is deduced as `U`, so `static_cast<T&&>` is `static_cast<U&&>`, returning an rvalue reference.

The key insight: `std::forward`'s "conditionality" comes from **the template parameter `T` carrying the original argument's value-category information**, not from `std::forward`'s own logic. When a universal reference receives an lvalue, `T` is deduced as `U&`, and that `&` is like a stamp that imprints "this is an lvalue" into the type. `std::forward` "unstamps" it via `static_cast<T&&>` and reference collapsing.

## Perfect Forwarding in the Standard Library

Perfect forwarding is everywhere in the C++ standard library. The classic examples are `std::make_unique` and `std::make_shared`, which take arbitrary arguments and forward them unchanged to the constructor of the object managed by the `unique_ptr`/`shared_ptr`.

```cpp
// a simplified std::make_unique
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
```

Here `Args&&... args` is a parameter pack of universal references. Each `Args` is deduced independently, so if you pass one lvalue and one rvalue, each one's value category is preserved. `std::forward<Args>(args)...` forwards every parameter to `T`'s constructor according to its original value category.

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
    // 42 is an rvalue → int has no "move", it's just pass-by-value

    auto user2 = std::make_unique<User>("Bob", 100);
    // "Bob" is a const char* rvalue → used to construct the std::string parameter
    return 0;
}
```

Another classic is `std::vector::emplace_back`. What it takes is constructor arguments, not a ready-made object, and it constructs the new element in place inside the vector's memory, more efficient than `push_back` since even a move is saved.

```cpp
std::vector<std::string> words;
words.emplace_back("hello");          // construct std::string("hello") directly in the vector
words.emplace_back(std::string("hi")); // pass an rvalue, move-construct

std::string word = "world";
words.emplace_back(std::move(word));   // pass an rvalue, move-construct
```

## Common Mistakes—What Not to forward

`std::forward` is powerful, but using it in the wrong place introduces subtle bugs. The most important rule: **only use `std::forward` on universal references**.

```cpp
// mistake 1: using std::forward on a non-universal reference
void process(const std::string& s)
{
    // s is NOT a universal reference! It's a const lvalue reference, a fixed type
    // std::forward<const std::string&>(s) always returns a const lvalue reference
    // using std::forward here is pointless and misleading
    consume(std::forward<const std::string&>(s));  // don't do this
    consume(s);  // just pass it
}
```

In a non-template plain function, the parameter type is fixed, there's no "decide lvalue or rvalue from the argument" going on. Using `std::forward` on a fixed-type parameter just muddies the code's intent.

```cpp
// mistake 2: forwarding the same parameter twice
template<typename T>
void double_forward(T&& x)
{
    target(std::forward<T>(x));  // first forward
    target(std::forward<T>(x));  // dangerous! if x is an rvalue, the first one already "stole" it
}
```

If `x` is an rvalue reference, the first `std::forward<T>(x)` turns `x` into an rvalue and passes it to `target`, which may have already stolen `x`'s resources. On the second forward, `x` is in a "valid but unspecified" state, and you're sending out an rvalue that may already be empty. That's the so-called "use-after-move", the compiler won't complain, but the runtime behavior is unpredictable.

```cpp
// mistake 3: using std::forward + decltype(auto) in a return statement
template<typename T>
decltype(auto) bad_return(T&& x)
{
    return (std::forward<T>(x));  // dangerous! may return a dangling reference
}
```

Here `decltype(auto)` deduces the return type from the `return` expression, so the return type depends on the result of `std::forward<T>(x)`. When you pass an rvalue, `T` is deduced as a non-reference type (say `std::string`), `std::forward<std::string>(x)` returns `std::string&&`, and `decltype(auto)` deduces the return type as `std::string&&`. But this rvalue reference points at the function parameter `x`, which is destroyed when the function returns. The caller gets a reference to memory that no longer exists, a classic dangling reference, and GCC's `-Wdangling-reference` will warn about it.

When you pass an lvalue, `T` is deduced as `U&` (say `std::string&`), `std::forward<std::string&>(x)` returns `std::string&` via reference collapsing, and the reference chain ultimately points at the caller's original variable, which is still alive, so it's safe. The problem is that this template is safe for lvalues and dangerous for rvalues, while `decltype(auto)` can't express that distinction in the signature, so it's very easy to misuse during maintenance.

If you really do need to forward in a return statement, make sure the return type is a value type (`T`, not `decltype(auto)`), so the rvalue case triggers a move construction instead of returning a reference. The `emplace_get` in the cache wrapper from the previous section is a correct example: it returns `Value&` (a fixed type, not a forwarded one) and only uses `std::forward` on the parameters.

## Worked Example: A Generic Cache Wrapper

Let's write a practical example with perfect forwarding: a generic cache wrapper template that caches the result of any function call and perfectly forwards all arguments.

```cpp
// perfect_forwarding.cpp -- 完美转发演示
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>
#include <map>
#include <functional>

/// @brief 一个简单的缓存包装器
/// 完美转发函数参数，同时保持值类别信息
template<typename Key, typename Value>
class Cache
{
    std::map<Key, Value> storage_;

public:
    /// @brief 查找或插入：如果 key 不存在则用 args 构造 Value
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

/// @brief 被包装的"昂贵"操作
class ExpensiveData
{
    std::string label_;
    int value_;

public:
    /// @brief 从字符串和整数构造
    ExpensiveData(std::string label, int value)
        : label_(std::move(label))
        , value_(value)
    {
        std::cout << "  [ExpensiveData] 构造: " << label_
                  << " = " << value_ << "\n";
    }

    /// @brief 从字符串构造（重载）
    explicit ExpensiveData(std::string label)
        : label_(std::move(label))
        , value_(0)
    {
        std::cout << "  [ExpensiveData] 构造(仅标签): " << label_ << "\n";
    }

    const std::string& label() const { return label_; }
    int value() const { return value_; }
};

/// @brief 通用的转发包装器——演示完美转发的核心用法
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

    // 第一次调用：缓存未命中，构造新值
    // 传入右值字符串和整数
    cache.emplace_get("alpha", "first", 100);

    // 第二次调用：同样的 key，缓存命中
    cache.emplace_get("alpha", "first", 200);

    // 新 key，传入右值字符串（单参数构造）
    std::string label = "beta";
    cache.emplace_get("beta", std::move(label));
    // label 已被移动，不要再使用

    std::cout << "  缓存大小: " << cache.size() << "\n\n";

    std::cout << "=== 2. 转发包装器 ===\n";
    auto add = [](int a, int b) -> int {
        return a + b;
    };

    int x = 10;
    int result = invoke_and_log(add, x, 20);
    std::cout << "  结果: " << result << "\n\n";

    std::cout << "=== 3. make_unique 风格的工厂 ===\n";
    // 演示完美转发在构造函数参数传递中的效果
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

The `Args&&... args` in `emplace_get` is a universal reference parameter pack. When you pass `("first", 100)`, `Args` is deduced as `const char (&)[6]` and `int` (loosely, `const char*` and `int`). `std::forward<Args>(args)...` forwards these parameters unchanged to `ExpensiveData`'s constructor, and the parameter types and value categories the constructor sees are exactly what they'd be if you passed them to it directly.

When you pass `std::move(label)`, `Args` is deduced as `std::string` (non-reference), and `std::forward` turns it into an rvalue reference. `ExpensiveData`'s `std::string` parameter is then initialized via move construction, avoiding a deep copy of the string. That's the power of perfect forwarding: one template, automatically handling every combination of value categories.

## Hands-On Experiment: Verifying Reference Collapsing

To cement the understanding, let's write a small program that uses `std::is_same_v` to verify the result of reference collapsing:

```cpp
// ref_collapsing.cpp -- 引用折叠验证
// Standard: C++17

#include <iostream>
#include <type_traits>
#include <string>

template<typename T>
void show_deduction(T&& /* arg */)
{
    // T 的推导结果
    if constexpr (std::is_lvalue_reference_v<T>) {
        std::cout << "  T = 左值引用类型\n";
    } else {
        std::cout << "  T = 非引用类型（右值）\n";
    }

    // T&& 的最终类型（经过引用折叠）
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

This output perfectly confirms the reference collapsing rules: when you pass an lvalue (const or not), `T` is deduced as a reference type and `T&&` collapses to an lvalue reference. When you pass an rvalue, `T` is deduced as a non-reference type and `T&&` is an rvalue reference. The const-ness also travels through `T`; even though this simplified program doesn't distinguish const from non-const, `T` really does carry the const modifier, and `std::forward` preserves it correctly.

## Run Online

Run the reference collapsing example online and verify the deduction rules of universal references:

<OnlineCompilerDemo
  title="Perfect Forwarding: Universal References and Reference Collapsing"
  source-path="code/examples/vol2/04_perfect_forwarding.cpp"
  description="Run online and observe the deduced template parameter T when passing lvalues and rvalues."
  allow-run
/>
