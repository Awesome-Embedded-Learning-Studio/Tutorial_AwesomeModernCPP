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
  source_hash: 448b2e84f096cc31d24096c32df1f673fa61eda36029fb8c09f135add6088348
  translated_at: '2026-11-22T16:36:41+00:00'
  engine: glm-5.3-refresh
  token_count: 15101
---
# Perfect Forwarding: Keeping Value Categories Intact

Anyone who has written a template function has probably hit this dilemma: receive an argument, pass it along to another function, and you want the callee to see an lvalue when the caller passed an lvalue, and an rvalue when the caller passed an rvalue. Sounds simple, right? Before C++11, though, this was nearly impossible: either you wrote two overloads (one taking an lvalue reference, one taking an rvalue reference), or you just took everything by const reference, threw away the information that "this is an rvalue", and lost the performance benefits of move semantics along with it. Good grief—efficiency and performance just refused to coexist. Annoying!

But fear not: C++11 also brought us perfect forwarding, which solves exactly this problem. It lets us write a single template that forwards an argument's value category, untouched, to the target function.

In one sentence: handing a parameter off somewhere else used to mean writing both a `const T&` version and a `T&&` version; not anymore—forward (or if you like, pass through) with `std::forward`.

## Starting from a Real Problem

Suppose we're writing a simple factory function that creates `std::string` objects:

```cpp
// 版本一：按 const 引用接收
std::string make_string(const std::string& s)
{
    return std::string(s);  // 总是拷贝构造
}

// 版本二：按右值引用接收
std::string make_string(std::string&& s)
{
    return std::string(std::move(s));  // 总是移动构造
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

> Just like not all mineral water... sorry, wrong room.

The great Scott Meyers himself gave this special `T&&` a name: "universal references"<RefLink :id="1" preview="Scott Meyers, Effective Modern C++, Item 24 — distinguish universal references from rvalue references" />; the C++ standard's term is "forwarding references"<RefLink :id="2" preview="cppreference References — forwarding references and reference collapsing" />. They look exactly like rvalue references (honestly, I'm a little puzzled as to why too—if any C++ heavyweight out there can explain why they had to look identical, I'm all ears!), but they behave completely differently.

The key difference lies in **the context of type deduction**. An ordinary rvalue reference like `std::string&&` binds only to rvalues—that's fixed. But a `T&&` in the context of template argument deduction adjusts itself based on the argument you pass: pass an lvalue, and `T` is deduced as an lvalue reference type, so `T&&` becomes an lvalue reference through reference collapsing; pass an rvalue, and `T` is deduced as a non-reference type, leaving `T&&` a plain rvalue reference.

```cpp
template<typename T>
void identify(T&& arg)
{
    // arg 到底是左值引用还是右值引用？取决于调用时传入的实参
}

std::string name = "Alice";

identify(name);              // 传左值，T = std::string&，T&& = std::string&
identify(std::string("Bob")); // 传右值，T = std::string，T&& = std::string&&
```

A universal reference requires two necessary conditions—miss either and it isn't one: first, the type must be deduced through a template parameter (the `T` in `template<typename T>`); second, the declaration must have exactly the form `T&&`, with no const or other qualifiers added. Write `const T&&` and you have an ordinary const rvalue reference, not a universal reference. Write `std::vector<T>&&` and that isn't one either: `T` does get deduced, but `std::vector<T>&&` as a whole is not of the form `T&&`.

```cpp
template<typename T>
void forwarding(T&& x);      // 万能引用 ✓

template<typename T>
void not_forwarding(const T&& x);  // const 右值引用，不是万能引用 ✗

template<typename T>
void also_not(std::vector<T>&& x); // vector 右值引用，不是万能引用 ✗

// auto&& 也是万能引用（C++11 之后）
auto&& universal = some_expression;  // 万能引用 ✓
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
    // 使用 type_traits 来查看推导后的类型
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
    show_type(name);                // T = std::string&, 输出"左值引用"
    show_type(std::string("Bob"));  // T = std::string, 输出"右值引用"
    show_type(std::move(name));     // T = std::string, 输出"右值引用"
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
// 简化版 std::forward 的实现
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
// std::make_unique 的简化实现
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
    // std::move(name) 是右值 → name 被移动进 User 的构造函数
    // 42 是右值 → int 没有"移动"的概念，就是值传递

    auto user2 = std::make_unique<User>("Bob", 100);
    // "Bob" 是 const char* 右值 → 用于构造 std::string 参数
    return 0;
}
```

Another classic example is `std::vector::emplace_back`. It takes constructor arguments rather than ready-made objects, constructing the new element in place, directly in the vector's memory—more efficient than `push_back`, saving even the move.

```cpp
std::vector<std::string> words;
words.emplace_back("hello");          // 直接在 vector 中构造 std::string("hello")
words.emplace_back(std::string("hi")); // 传入右值，移动构造

std::string word = "world";
words.emplace_back(std::move(word));   // 传入右值，移动构造
```

## Common Mistakes—What Not to forward

Powerful as `std::forward` is, using it in the wrong place introduces subtle bugs. The most important rule: **use `std::forward` only on universal references**.

```cpp
// 错误 1：对非万能引用使用 std::forward
void process(const std::string& s)
{
    // s 不是万能引用！它是 const 左值引用，固定类型
    // std::forward<const std::string&>(s) 永远返回 const 左值引用
    // 在这里用 std::forward 没有任何意义，还容易误导读者
    consume(std::forward<const std::string&>(s));  // 不要这样做
    consume(s);  // 直接传就好
}
```

In an ordinary non-template function, the parameter types are fixed—there is no "decide lvalue or rvalue based on the argument passed in" going on. Using `std::forward` on such a fixed-type parameter is pure troublemaking; it only blurs the intent of the code.

```cpp
// 错误 2：多次 forward 同一个参数
template<typename T>
void double_forward(T&& x)
{
    target(std::forward<T>(x));  // 第一次 forward
    target(std::forward<T>(x));  // 危险！如果 x 是右值，第一次已经"偷走"了
}
```

If `x` is an rvalue reference, the first `std::forward<T>(x)` turns `x` into an rvalue to pass to `target`, and `target` may already have stolen `x`'s resources. By the time you forward again, `x` is in a "valid but unspecified" state, and you're handing out an rvalue that may already be hollowed out. This is the so-called use-after-move: the compiler won't report an error, but the runtime behavior is unpredictable.

```cpp
// 错误 3：在返回语句中用 std::forward + decltype(auto)
template<typename T>
decltype(auto) bad_return(T&& x)
{
    return (std::forward<T>(x));  // 危险！可能返回悬空引用
}
```

Here `decltype(auto)` deduces the return type from the `return` expression, so the return type depends on the result of `std::forward<T>(x)`. The deduction itself is unambiguous: pass an lvalue, and `T` is deduced as `U&` (say, `std::string&`); `std::forward<std::string&>(x)` returns `std::string&` through reference collapsing, and the reference chain ultimately points at the caller's original variable—as long as that variable stays alive, this path is safe. Pass an xvalue such as `std::move(u)`, and the returned `std::string&&` likewise points at `u`, an object owned by the caller, living and dying with `u` itself.

The real danger sits with prvalues: pass `std::string{}`, and `T` is deduced as a non-reference type, `std::forward<std::string>(x)` returns `std::string&&`, and the return type `decltype(auto)` deduces is `std::string&&`. Note that this reference does not point at the function parameter `x`—a reference parameter is merely an alias—but at the temporary object materialized from the argument (C++20 [class.temporary]). That temporary lives until **the end of the full expression containing the function call** (usually the semicolon at the end of that statement), not until the moment the function returns. So `auto s = bad_return(std::string{});` is safe: `s` finishes being move-constructed from the temporary before the semicolon. The genuine dangle happens when you store the return value as a reference and then use it across statements, e.g. `auto&& r = bad_return(std::string{});`—once the semicolon arrives, the temporary is destroyed and `r` is left dangling. The annoying part is that the compiler cannot help here: `-Wdangling-reference` watches for the shape "returns a reference to a local lvalue inside the function", and the forwarding-reference-plus-`decltype(auto)` combo is outside its range—tested in practice (GCC 16, the warning explicitly enabled), it passes silently. Only a pair of human eyes can catch it.

So the accurate portrait of this function template is: safe for lvalues and xvalues; for prvalues, whether it is safe depends on how the caller catches the return value—and `decltype(auto)` cannot express any of these distinctions in its signature, which makes it very easy to misuse during maintenance.

If you truly need to forward in a return statement, make sure the return type is a value type (`T` rather than `decltype(auto)`), so that in the rvalue case a move construction is triggered instead of a reference being returned. The `emplace_get` in the cache wrapper in the next section is a correct example: it returns `Value&` (a fixed type, not something forwarded), and uses `std::forward` only on its parameters.

## A General Example—A Generic Cache Wrapper

Let's use perfect forwarding to write a practical example: a general-purpose cache wrapper template that can cache the results of arbitrary function calls and perfectly forwards all arguments.

```cpp
// perfect_forwarding.cpp -- 完美转发演示
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>
#include <map>
#include <functional>
#include <memory>

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

To deepen our understanding, let's write a small program that uses `std::is_lvalue_reference_v` to verify the outcome of reference collapsing:

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
</ReferenceCard>
