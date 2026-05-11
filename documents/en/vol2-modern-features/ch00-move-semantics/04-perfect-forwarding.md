---
title: 'Perfect forwarding: preserving the exact passing of value categories'
description: Understand reference collapsing and universal references, master the
  correct use of std::forward
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
---
# Perfect Forwarding: Preserving Value Categories Exactly

If you've ever written a template function that takes a parameter and passes it to another function, you've likely run into this dilemma: when passing an lvalue, you want the receiver to get an lvalue; when passing an rvalue, you want the receiver to get an rvalue. Sounds simple, right? But before C++11, this was nearly impossible — you either wrote two overloads (one taking an lvalue reference, one taking an rvalue reference), or you accepted everything by const reference and lost the rvalue information, sacrificing the performance benefits of move semantics. What a headache — you couldn't have both efficiency and performance!

Fortunately, C++11's perfect forwarding was designed to solve exactly this problem. It lets us write a single template that forwards a parameter's value category to the target function exactly as it was received.

In a nutshell: previously, passing parameters to other functions always required writing both `const T&` and `T&&`, but now we don't need to — we simply use `std::forward` to forward (or pass through) the arguments.

## Starting with a Real Problem

Suppose we're writing a simple factory function to create `std::string` objects:

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

Version one accepts lvalues, but passing an rvalue still results in a copy — because you received it by const reference, losing the "this is an rvalue" information. Version two accepts rvalues and correctly moves them, but passing an lvalue causes a compilation error — because an rvalue reference cannot bind to an lvalue.

To support both cases, you'd need two overloads:

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

What about two parameters? Four overloads (`const&` + `const&`, `const&` + `&&`, `&&` + `const&`, `&&` + `&&`, i.e., 2 × 2). With three parameters, it's eight. That's a disaster — in real-world development with tons of members to handle, writing code like this would be completely unmaintainable. Clearly, this approach doesn't scale.

## Forwarding References — Not All `T&&` Are Rvalue References

Scott Meyers coined the term "universal reference" for this special `T&&`, while the C++ standard calls it a "forwarding reference." It looks identical to an rvalue reference (honestly, I'm not entirely sure why they made it look exactly the same — if any C++ expert could explain, I'd love to learn!), but its behavior is completely different.

The key distinction lies in the **context of type deduction**. A plain rvalue reference `std::string&&` can only bind to rvalues — that's fixed. But in template argument deduction, `T&&` automatically adjusts based on the passed argument — pass in an lvalue, and `T` is deduced as an lvalue reference type, with `T&&` collapsing into an lvalue reference via reference collapsing; pass in an rvalue, and `T` is deduced as a non-reference type, making `T&&` an rvalue reference.

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

For a forwarding reference to appear, two conditions must be met, without exception: first, the type must involve template argument deduction (the `T` in `template<typename T>`); second, the declaration form must be exactly `T&&`, with no const or other qualifiers added. If you write `const T&&`, it's a plain const rvalue reference, not a forwarding reference. If you write `std::vector<T>&&`, it's also not a forwarding reference — although `T` is deduced, the overall `std::vector<T>&&` is not in the `T&&` form.

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

`auto&&` follows the same deduction rules — if `some_expression` is an lvalue, `universal` is an lvalue reference; if it's an rvalue, `universal` is an rvalue reference. This is common in range-based for loops and lambda captures.

## Reference Collapsing — The Final Result of Four Combinations

This section draws heavily from *Effective Modern C++*:

The reason forwarding references work is thanks to the heavy lifting of **reference collapsing**. When the compiler deduces `T&&`, a "reference to a reference" situation can arise — for example, if `T` is deduced as `std::string&`, then `T&&` becomes `std::string& &&`. C++ doesn't allow writing "reference to a reference" directly, but in the context of template deduction, the compiler collapses it according to four rules:

`T& &` collapses to `T&`, `T& &&` collapses to `T&`, `T&& &` collapses to `T&`, and `T&& &&` collapses to `T&&`.

There's no need to memorize these four rules — just remember one simple pattern: **as long as one is an lvalue reference (`&`), the result is an lvalue reference**. Only when both are rvalue references (`&& &&`) does the result become an rvalue reference.

Let's verify this with a concrete deduction process. When passing an lvalue `name`, `T` is deduced as `std::string&`, so `T&&` becomes `std::string& &&`, which collapses to `std::string&` per the second rule — the parameter type is an lvalue reference. When passing an rvalue `std::string("Bob")`, `T` is deduced as `std::string` (a non-reference type), so `T&&` is simply `std::string&&` — the parameter type is an rvalue reference. No collapsing occurs, because there was no "reference to a reference" to begin with.

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

Reference collapsing doesn't only appear in function templates. Deduction of `auto&&`, instantiation of `typedef` and `using` aliases, and certain uses of `decltype` all trigger reference collapsing. However, forwarding references in function templates are the most common scenario.

## `std::forward` — Conditional Type Casting

Okay, here's the important part — if you only care about how to use it! Once you understand forwarding references and reference collapsing, `std::forward` is quite simple. Its job is: **when the passed argument is an rvalue, cast the parameter to an rvalue reference; when it's an lvalue, keep it as an lvalue reference**. Essentially, it's a conditional, much smarter version of `static_cast`. (In a word: hey, this little thing remembers whether I passed an lvalue or an rvalue, and passes it through as-is.)

We can implement a simplified version ourselves to understand the underlying principle:

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

These two overloads, combined with reference collapsing, achieve the "conditional cast" logic. When an lvalue is passed, `T` is deduced as `U&` (where U is the actual type), `static_cast<T&&>` becomes `static_cast<U& &&>`, which collapses to `U&` — returning an lvalue reference. When an rvalue is passed, `T` is deduced as `U`, and `static_cast<T&&>` becomes `static_cast<U&&>` — returning an rvalue reference.

The key insight is that the "conditionality" of `std::forward` doesn't come from `std::forward`'s own logic, but from **the template parameter `T` carrying the original argument's value category information**. When a forwarding reference receives an lvalue, `T` is deduced as `U&`, and this `&` acts like a stamp, imprinting the "this is an lvalue" information into the type. `std::forward` then "decodes" this stamp through `static_cast<T&&>` and reference collapsing.

## Perfect Forwarding in the Standard Library

Perfect forwarding is everywhere in the C++ standard library. The most classic examples are `std::make_unique` and `std::make_shared` — they accept arbitrary arguments and forward them verbatim to the constructor of the object managed by `unique_ptr`/`shared_ptr`.

```cpp
// std::make_unique 的简化实现
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
```

Here, `Args&&... args` is a forwarding reference parameter pack. Each `Args` is deduced independently, so if you pass an lvalue and an rvalue, their respective value categories are both preserved. `std::forward<Args>(args)...` forwards each argument to `T`'s constructor according to its original value category.

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

Another classic example is `std::vector::emplace_back`. Instead of taking an existing object, it accepts constructor arguments and constructs a new element in-place within the vector's memory — this is more efficient than `push_back` because it even eliminates the move.

```cpp
std::vector<std::string> words;
words.emplace_back("hello");          // 直接在 vector 中构造 std::string("hello")
words.emplace_back(std::string("hi")); // 传入右值，移动构造

std::string word = "world";
words.emplace_back(std::move(word));   // 传入右值，移动构造
```

## Common Mistakes — What Not to Forward

`std::forward` is powerful, but using it in the wrong place introduces subtle bugs. The most important rule is: **only use `std::forward` on forwarding references**.

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

In a non-template regular function, the parameter types are fixed — there's no scenario where "the type is decided as lvalue or rvalue based on the passed argument." Using `std::forward` on a fixed-type parameter like this just adds confusion and makes the code's intent unclear.

```cpp
// 错误 2：多次 forward 同一个参数
template<typename T>
void double_forward(T&& x)
{
    target(std::forward<T>(x));  // 第一次 forward
    target(std::forward<T>(x));  // 危险！如果 x 是右值，第一次已经"偷走"了
}
```

If `x` is an rvalue reference, the first `std::forward<T>(x)` converts `x` to an rvalue and passes it to `target` — `target` may have already stolen `x`'s resources. When you forward a second time, `x` is already in a "valid but unspecified" state, and you're passing out an rvalue that may be empty. This is the classic "use-after-move" — although the compiler won't report an error, the runtime behavior is unpredictable.

```cpp
// 错误 3：在返回语句中用 std::forward + decltype(auto)
template<typename T>
decltype(auto) bad_return(T&& x)
{
    return (std::forward<T>(x));  // 危险！可能返回悬空引用
}
```

Here, `decltype(auto)` deduces the return type based on the `return` expression, so the return type depends on the result of `std::forward<T>(x)`. When you pass an rvalue, `T` is deduced as a non-reference type (e.g., `std::string`), and `std::forward<std::string>(x)` returns `std::string&&` — the deduced return type of `decltype(auto)` is `std::string&&`. But this rvalue reference points to the function parameter `x`, which is destroyed when the function returns. The caller receives a reference pointing to memory that no longer exists — a classic dangling reference, and GCC's `-Wdangling-reference` will warn about this.

When an lvalue is passed, `T` is deduced as `U&` (e.g., `std::string&`), and `std::forward<std::string&>(x)` returns `std::string&` via reference collapsing — the reference chain ultimately points back to the caller's original variable, which is still alive, so it's safe. But the problem is that this function template is safe for lvalues yet dangerous for rvalues, and `decltype(auto)` can't express this distinction in its signature, making it very easy to misuse during maintenance.

If you really need to forward in a return statement, make sure the return type is a value type (`T` rather than `decltype(auto)`), so the rvalue scenario triggers move construction instead of returning a reference. The `emplace_get` in the cache wrapper from the previous section is a correct example: it returns `Value&` (a fixed type, not forwarded), and only uses `std::forward` on the arguments.

## Practical Example — A Generic Cache Wrapper

Let's use perfect forwarding to write a practical example: a generic cache wrapper template that caches the results of any function call and perfectly forwards all arguments.

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

The `Args&&... args` in `emplace_get` is a forwarding reference parameter pack. When you pass `("first", 100)`, `Args` is deduced as `const char (&)[6]` and `int` (roughly understood as `const char*` and `int`). `std::forward<Args>(args)...` forwards these arguments verbatim to `ExpensiveData`'s constructor, and the constructor receives parameter types and value categories exactly as if you had passed them directly.

When you pass `std::move(label)`, `Args` is deduced as `std::string` (non-reference), and `std::forward` converts it to an rvalue reference — `ExpensiveData`'s `std::string` parameter is initialized via move construction, avoiding a deep copy of the string. This is the power of perfect forwarding: one template automatically handles all combinations of value categories.

## Hands-on Experiment — Verifying Reference Collapsing

To deepen our understanding, let's write a small program that uses `std::is_same_v` to verify the results of reference collapsing:

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

This set of output perfectly confirms the reference collapsing rules: when passing an lvalue (whether const or not), `T` is deduced as a reference type, and `T&&` collapses to an lvalue reference. When passing an rvalue, `T` is deduced as a non-reference type, and `T&&` is simply an rvalue reference. The const information is also propagated through `T` — although this simplified program doesn't distinguish between const and non-const, `T` does contain the const qualifier, and `std::forward` correctly preserves it.

## Summary

The three core components of perfect forwarding form a precise collaborative chain: **forwarding references** (`T&&`) deduce the type of `T` based on the passed argument, encoding the value category information into the type; **reference collapsing** handles the theoretically invalid "reference to a reference" situation, ensuring the final type matches intuition — as long as an lvalue reference is involved, the result is an lvalue reference; and **`std::forward`** uses `static_cast<T&&>` and reference collapsing to decode the value category information embedded in `T`, achieving exact forwarding.

Remember a few practical rules: only use `std::forward` on forwarding references, never forward the same parameter twice, and don't forward rvalue parameters in functions with `decltype(auto)` return types (it will return a dangling reference). In the next article, we'll look at the complete application of move semantics in practice — from STL containers to custom types — and see how these theoretical concepts translate into tangible performance gains.
