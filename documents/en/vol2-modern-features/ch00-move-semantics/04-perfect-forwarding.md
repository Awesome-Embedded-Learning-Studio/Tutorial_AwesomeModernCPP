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
  source_hash: b3de2606d1f5c62240f79c14607c40f294f0a731eb7191e5e65a08238353c72d
  translated_at: '2026-05-26T11:18:43.487448+00:00'
  engine: anthropic
  token_count: 3548
---
# Perfect Forwarding: Preserving Value Categories Exactly

If you have ever written a template function that receives a parameter and passes it to another function, you have likely run into this dilemma: when passing an lvalue, you want the receiver to get an lvalue, and when passing an rvalue, you want it to get an rvalue. Sounds simple, right? But before C++11, this was nearly impossible — you either had to write two overloads (one taking an lvalue reference, one taking an rvalue reference), or simply accept everything by const reference, losing the rvalue information and the performance benefits of move semantics. What a pain — efficiency and performance could not coexist!

Fortunately, perfect forwarding in C++11 was designed to solve exactly this problem. It allows us to write a single template that forwards a parameter's value category to the target function exactly as it was received.

In a nutshell: previously, passing parameters to other functions always required writing both `const T&` and `T&&`, but now we don't need to — we simply use `std::forward` to pass them through.

## Starting with a Real Problem

Suppose we are writing a simple factory function to create `std::string` objects:

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

Version one accepts lvalues, but passing an rvalue also results in a copy — because you received it via const reference, discarding the "this is an rvalue" information. Version two accepts rvalues and correctly moves them, but passing an lvalue causes a compilation error — because an rvalue reference cannot bind to an lvalue.

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

What about two parameters? Four overloads (`const&` + `const&`, `const&` + `&&`, `&&` + `const&`, `&&` + `&&`, i.e., 2 x 2). With three parameters, it is eight. That is a disaster — in real-world development, you deal with tons of members, and writing code like this will blow up. Clearly, this approach does not scale.

## Universal References — Not All `T&&` Are Rvalue References

Scott Meyers coined the term "universal reference" for this special `T&&`, while the C++ standard calls it a "forwarding reference." It looks identical to an rvalue reference (honestly, I am not entirely sure why they made it look exactly the same — if any C++ expert could explain, I would love to learn!), but its behavior is completely different.

The key distinction lies in the **context of type deduction**. A plain rvalue reference `T&&` can only bind to rvalues — that is fixed. But `T&&` in template argument deduction automatically adjusts based on the passed argument — when an lvalue is passed in, `T` is deduced as an lvalue reference type, and `T&&` collapses into an lvalue reference via reference collapsing; when an rvalue is passed in, `T` is deduced as a non-reference type, and `T&&` is an rvalue reference.

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

The appearance of a universal reference requires two conditions, both of which must be met: first, the type must be deduced through a template parameter (`T` in `template<typename T>`); second, the declaration form must be exactly `T&&`, with no const or other qualifiers added. If you write `const T&&`, it is a plain const rvalue reference, not a universal reference. If you write `std::vector<T>&&`, it is also not a universal reference — although `T` is deduced, `std::vector<T>&&` as a whole is not in the form of `T&&`.

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

`auto&&` follows the same deduction rules — if the initializer is an lvalue, `auto&&` is an lvalue reference; if it is an rvalue, `auto&&` is an rvalue reference. This is common in range-based for loops and lambda captures.

## Reference Collapsing — The Final Result of Four Combinations

This section borrows heavily from *Effective Modern C++*:

The reason universal references work is that **reference collapsing** is doing the heavy lifting behind the scenes. When the compiler deduces `T&&`, a "reference to a reference" situation can arise — for example, if `T` is deduced as `int&`, then `T&&` becomes `int& &&`. C++ does not allow writing "reference to a reference" directly, but in the context of template deduction, the compiler collapses it according to four rules:

`T& &` collapses to `T&`, `T& &&` collapses to `T&`, `T&& &` collapses to `T&`, and `T&& &&` collapses to `T&&`.

There is no need to memorize these four rules — just remember one simple pattern: **as long as one of them is an lvalue reference (`&`), the result is an lvalue reference**. Only when both are rvalue references (`&&`) does the result become an rvalue reference.

Let us verify this with a concrete deduction process. When an lvalue `int x = 42` is passed in, `T` is deduced as `int&`, so `T&&` becomes `int& &&`, which collapses to `int&` per the second rule — the parameter type is an lvalue reference. When an rvalue `42` is passed in, `T` is deduced as `int` (a non-reference type), so `T&&` is simply `int&&` — the parameter type is an rvalue reference. No collapsing occurs, because there was no "reference to a reference" to begin with.

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

Reference collapsing does not only appear in function templates. Deduction of `auto&&`, instantiation of `typedef` and `using` aliases, and certain uses of `decltype` all trigger reference collapsing. However, universal references in function templates are the most common scenario.

## std::forward — Conditional Type Casting

Alright, here is the important part — if you only care about how to use it! Once you understand universal references and reference collapsing, `std::forward` is quite simple. Its job is: **when the passed argument is an rvalue, cast the parameter to an rvalue reference; when it is an lvalue, keep it as an lvalue reference**. In essence, it is a conditional, smarter, and more powerful `static_cast`. (In a word: hey, this little thing remembers whether I passed an lvalue or an rvalue, and passes it through to somewhere else.)

We can implement a simplified version ourselves to understand the principle:

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

These two overloads, combined with reference collapsing, accomplish the "conditional cast" logic. When an lvalue is passed in, `T` is deduced as `U&` (where `U` is the actual type), so `remove_reference_t<T>&&` becomes `U& &&`, which collapses to `U&` — returning an lvalue reference. When an rvalue is passed in, `T` is deduced as `U`, so `remove_reference_t<T>&&` is simply `U&&` — returning an rvalue reference.

The key insight is that the "conditionality" of `std::forward` does not come from the logic of `std::forward` itself, but from **the template parameter `T` carrying the value category information of the original argument**. When a universal reference receives an lvalue, `T` is deduced as `U&`, and this `&` acts like a seal, stamping the "this is an lvalue" information into the type. `std::forward` "unseals" this information through `remove_reference_t` and reference collapsing.

## Applications of Perfect Forwarding in the Standard Library

Perfect forwarding is everywhere in the C++ standard library. The most classic examples are `std::make_shared` and `std::make_unique` — they accept arbitrary parameters and forward them unchanged to the constructor of the object managed by the `std::shared_ptr`/`std::unique_ptr`.

```cpp
// std::make_unique 的简化实现
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
```

Here, `Args&&... args` is a universal reference parameter pack. Each `Args` is deduced independently, so if you pass an lvalue and an rvalue, their respective value categories are all preserved. `std::forward<Args>(args)...` forwards each parameter to the constructor of `T` according to its original value category.

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

Another classic example is `std::vector::emplace_back`. It does not take an existing object; instead, it takes constructor arguments and constructs a new element in-place within the vector's memory — this is more efficient than `push_back` because even the move is eliminated.

```cpp
std::vector<std::string> words;
words.emplace_back("hello");          // 直接在 vector 中构造 std::string("hello")
words.emplace_back(std::string("hi")); // 传入右值，移动构造

std::string word = "world";
words.emplace_back(std::move(word));   // 传入右值，移动构造
```

## Common Mistakes — What Not to Forward

`std::forward` is powerful, but using it in the wrong place introduces subtle bugs. The most important rule is: **only use `std::forward` on universal references**.

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

In a non-template regular function, the parameter type is fixed — there is no scenario where "the type is decided as lvalue or rvalue based on the passed argument." Using `std::forward` on a fixed-type parameter like this just adds confusion and makes the code's intent unclear.

```cpp
// 错误 2：多次 forward 同一个参数
template<typename T>
void double_forward(T&& x)
{
    target(std::forward<T>(x));  // 第一次 forward
    target(std::forward<T>(x));  // 危险！如果 x 是右值，第一次已经"偷走"了
}
```

If `T&&` is an rvalue reference, the first `std::forward` converts `param` to an rvalue and passes it to `foo` — `foo` may have already stolen `param`'s resources. When you forward a second time, `param` is already in a "valid but unspecified" state, and you are passing out an rvalue that may be empty. This is the so-called "use-after-move" — although the compiler will not report an error, the runtime behavior is unpredictable.

```cpp
// 错误 3：在返回语句中用 std::forward + decltype(auto)
template<typename T>
decltype(auto) bad_return(T&& x)
{
    return (std::forward<T>(x));  // 危险！可能返回悬空引用
}
```

Here, `decltype(auto)` deduces the return type based on the `std::forward<T>(param)` expression, so the return type depends on the result of `std::forward`. When you pass an rvalue, `T` is deduced as a non-reference type (e.g., `int`), and `std::forward<T>(param)` returns `int&&` — the deduced return type of `wrapper` is `int&&`. But this rvalue reference points to the function parameter `param`, which is destroyed when the function returns. The caller receives a reference pointing to memory that no longer exists — a classic dangling reference, and GCC's `-Wreturn-local-addr` will warn about this.

When an lvalue is passed in, `T` is deduced as `int&` (for example), and `std::forward<T>(param)` returns `int&` via reference collapsing — the reference chain ultimately points to the caller's original variable, which is still alive, so it is safe. But the problem is that this function template is safe for lvalues and dangerous for rvalues, and `decltype(auto)` cannot reflect this distinction in the signature, making it very easy to misuse during maintenance.

If you truly need to forward in a return statement, ensure the return type is a value type (`auto` rather than `decltype(auto)`), so that in the rvalue scenario a move constructor is triggered instead of returning a reference. The `get_or_compute` in the caching wrapper from the previous section is a correct example: it returns `T` (a fixed type, not a forwarded one), and only uses `std::forward` on the arguments.

## Practical Example — A Generic Caching Wrapper

Let us use perfect forwarding to write a practical example: a generic caching wrapper template that can cache the results of any function call and perfectly forwards all arguments.

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

The `Args&&... args` in `get_or_compute` is a universal reference parameter pack. When you pass `"hello"` and `42`, `Args` is deduced as `const char (&)[6]` and `int&` (roughly understood as `const char* &` and `int&`). `std::forward<Args>(args)...` forwards these arguments unchanged to the constructor of `std::pair<Key, T>`, and the constructor receives parameter types and value categories exactly as if you had passed them directly.

When passing `std::string("world")`, `Args` is deduced as `std::string` (non-reference), and `std::forward` converts it to an rvalue reference — the `std::string` parameter of `std::pair`'s constructor is initialized via move construction, avoiding a deep copy of the string. This is the power of perfect forwarding: one template automatically handles all combinations of value categories.

## Hands-on Experiment — Verifying Reference Collapsing

To deepen our understanding, let us write a small program that uses `typeid` to verify the results of reference collapsing:

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

This set of output perfectly confirms the reference collapsing rules: when an lvalue is passed in (whether const or not), `T` is deduced as a reference type, and `T&&` collapses to an lvalue reference. When an rvalue is passed in, `T` is deduced as a non-reference type, and `T&&` is simply an rvalue reference. The const information is also propagated through `T` — although this simplified program does not distinguish between const and non-const, `T` does indeed contain the const qualifier, and `std::forward` will correctly preserve it.

## Run Online

Run the reference collapsing example online to verify the type deduction rules of universal references:

<OnlineCompilerDemo
  title="完美转发：万能引用与引用折叠"
  source-path="code/examples/vol2/04_perfect_forwarding.cpp"
  description="在线运行并观察传入左值和右值时模板参数 T 的推导结果。"
  allow-run
/>

## Summary

The three core components of perfect forwarding form a precise collaborative chain: **universal references** (`T&&`) deduce the type of `T` based on the passed argument, encoding the value category information into the type; **reference collapsing** handles the theoretically invalid "reference to a reference" situation, ensuring the final type matches intuition — as long as an lvalue reference is involved, the result is an lvalue reference; **`std::forward`** restores the value category information encoded in `T` through `remove_reference_t` and reference collapsing, achieving exact forwarding.

Remember a few practical rules: only use `std::forward` on universal references, never forward the same parameter twice, and do not forward an rvalue parameter in a function with a `decltype(auto)` return type (it will return a dangling reference). In the next article, we will look at the complete application of move semantics in practice — from STL containers to custom types, and see how these theoretical concepts translate into tangible performance improvements.
