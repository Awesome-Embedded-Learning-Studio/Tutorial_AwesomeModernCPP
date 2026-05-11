# decltype and Return Type Deduction

In the previous chapter, we covered the deduction rules of `auto` in detail—specifically, how it discards references and top-level `const` by default. But sometimes we need to preserve the exact type of an expression, including references and `const`. This is where `decltype` comes in.

The biggest difference between `auto` and `decltype` is this: `auto` deduces the type for a "new variable" based on an initializing expression (discarding references and `const`), whereas `decltype` "queries" the type of an existing expression (returning it exactly as-is). This distinction might seem simple, but it has many subtle implications in practice.

> In a nutshell: **decltype queries the exact type of an expression (preserving references and const), while decltype(auto) combines the conciseness of auto with the precision of decltype.**

------

## decltype Deduction Rules

### decltype(variable) vs decltype((variable))

The rules for `decltype` seem straightforward, but there is a very common pitfall: whether or not you add parentheses.

For an unparenthesized variable name, `decltype` returns the type as declared:

```cpp
int x = 42;
decltype(x) a = 100;      // int

const int& cr = x;
decltype(cr) b = x;        // const int&
```

But for a parenthesized variable name—`decltype((x))`—it returns the type of `x` as an expression (an lvalue expression), which is always an lvalue reference:

```cpp
int x = 42;
decltype((x)) c = x;       // int&（不是 int！）
```

The root of this difference lies in the C++ type system: `(x)` is not just a name, it is an expression. Since `(x)` evaluates to an lvalue, `decltype((x))` returns `T&`. An unparenthesized `x` is simply a variable name, so `decltype` directly looks up its declared type.

This "double-parentheses" rule is the most famous trap of `decltype` and a classic interview question. I stumbled over this when I first learned C++—I never expected that adding a pair of parentheses would change the type from `int` to `int&`.

### decltype Deduction for Function Calls

When the operand of `decltype` is a function call expression, it returns the exact type of the function's return value:

```cpp
int& get_ref() {
    static int x = 42;
    return x;
}

int get_val() {
    return 42;
}

decltype(get_ref()) a = get_ref();  // int&
decltype(get_val()) b = get_val();  // int
```

This stands in stark contrast to `auto`. For the return value of the exact same function, `auto` discards the reference and deduces `int`, while `decltype` preserves the reference and deduces `int&`.

### decltype Deduction for Expressions

For general expressions, `decltype` determines the type based on the expression's value category. If the expression is an lvalue, the result is a reference; if it is an rvalue, the result is a non-reference type:

```cpp
int x = 42;

decltype(x + 1) a = 0;    // int（x + 1 是右值）
decltype(x = 10) b = x;   // int&（赋值表达式返回左值引用）
decltype(++x) c = x;      // int&（前置 ++ 返回左值引用）
decltype(x++) d = 0;      // int（后置 ++ 返回右值）
```

------

## decltype(auto): Precisely Preserving Reference Semantics

C++14 introduced `decltype(auto)`, which combines the conciseness of `auto` (no need to explicitly write the type) with the precision of `decltype` (preserving references and `const`). During deduction, the compiler uses `decltype` rules to deduce the `auto` part.

### Basic Usage

```cpp
int x = 42;

auto a = (x);            // int（auto 丢弃引用）
decltype(auto) b = (x);  // int&（decltype 保留引用）
```

Notice the parentheses in the `return` statement—because `decltype` returns a reference for parenthesized expressions, `decltype(auto)` deduces `int&`. If you don't want a reference, don't use parentheses:

```cpp
decltype(auto) c = x;    // int（不加括号，decltype(x) 是 int）
```

### Application in Function Return Types

`decltype(auto)` is particularly useful for function return types, especially when you want to perfectly forward the reference semantics of a return value:

```cpp
class Container {
public:
    decltype(auto) operator[](std::size_t index) {
        return data_[index];  // data_[int] 返回 int&，decltype(auto) 保留
    }

    decltype(auto) operator[](std::size_t index) const {
        return data_[index];  // const 版本返回 const int&
    }

private:
    std::vector<int> data_;
};
```

If we used `auto` instead of `decltype(auto)`, the return type of `get` would become `int` (a copy), and we would no longer be able to modify the container's contents through `get`.

### ⚠️ The Danger of Dangling References

The precision of `decltype(auto)` is a double-edged sword. It can deduce a reference type, leading to a reference to a local variable being returned:

```cpp
decltype(auto) get_value() {
    int x = 42;
    return (x);   // 返回 int&，但 x 在函数结束后销毁——悬空引用！
}

decltype(auto) safe_get_value() {
    int x = 42;
    return x;     // 返回 int（不加括号），值拷贝，安全
}
```

The parentheses in `return (x);` cause `decltype` to treat `x` as an lvalue expression, deducing `int&`. After the function returns, `x` is destroyed, leaving the reference dangling. This is a highly subtle bug; compilers usually emit a warning, but not all compilers can detect it in every scenario.

My advice: when using `decltype(auto)` in a function return type, carefully examine your `return` statements—if you are returning a reference to a local variable (whether intentionally or not), it will lead to undefined behavior (UB). If you are just returning a value, using `auto` is safer.

------

## Trailing Return Types

### The C++11 Motivation

In C++11, if a function's return type depended on its parameter types, you had to use a trailing return type. The most common scenario was returning the result of an operation between two parameters:

```cpp
template<typename T, typename U>
auto add(T t, U u) -> decltype(t + u) {
    return t + u;
}
```

Why can't we put the return type at the front? Because at the position of the function signature, the parameters `a` and `b` haven't been declared yet, so the compiler doesn't know their types. Trailing return types defer the declaration of the return type until after the parameter list, allowing us to use the parameters in the return type.

### The C++14 Simplification

C++14 allows using `auto` directly as the return type, with the compiler deducing it from the `return` statement. In most cases, trailing return types are no longer needed:

```cpp
// C++14 简化版
template<typename T, typename U>
auto add(T t, U u) {
    return t + u;
}
```

However, if you need to precisely preserve reference semantics (for example, when a function might return a reference), you still need `decltype` or `decltype(auto)`.

### Lambda Return Types in C++11

In C++11, if a lambda's return type couldn't be automatically deduced, you had to explicitly specify a trailing return type:

```cpp
auto get_size = [](const std::vector<int>& v) -> std::size_t {
    return v.size();
};
```

After C++14, a lambda's return type can almost always be deduced automatically, eliminating the need for explicit specification.

------

## decltype in Templates

### Perfectly Forwarding Return Values

The most common use of `decltype` in templates is implementing perfect forwarding of return values—making a wrapper function return the exact same type (including references) as the wrapped function:

```cpp
template<typename Callable, typename... Args>
decltype(auto) perfect_forward(Callable&& f, Args&&... args) {
    return std::forward<Callable>(f)(std::forward<Args>(args)...);
}
```

This `wrapper` function precisely forwards the call result of `func`. If `func` returns `int&`, `wrapper` also returns `int&`; if `func` returns `int`, `wrapper` also returns `int` (after C++14, `auto` supports deducing `int` here).

### decltype in Type Traits

`decltype` is extremely useful when writing type traits. Combined with `decltype`, you can obtain the type of an expression without evaluating it:

```cpp
#include <type_traits>
#include <vector>

// 检查类型 T 是否有 push_back 方法
template<typename T, typename Arg>
struct has_push_back {
private:
    template<typename U>
    static auto test(int) -> decltype(
        std::declval<U>().push_back(std::declval<Arg>()),
        std::true_type{}
    );

    template<typename>
    static auto test(...) -> std::false_type;

public:
    static constexpr bool value = decltype(test<T>(0))::value;
};

static_assert(has_push_back<std::vector<int>, int>::value);
static_assert(!has_push_back<int, int>::value);
```

The trick here is SFINAE (Substitution Failure Is Not An Error): if `T` has a `size` method, the return type of the first `check` overload can be successfully deduced; otherwise, deduction fails, and the compiler selects the second `check` overload. `decltype` is used here to "probe" the validity of an expression without actually evaluating it.

### The Purpose of std::declval

`std::declval` is a utility function that can only be used in an unevaluated context. It returns an rvalue reference to `T` without requiring `T` to have a default constructor. This allows you to construct "hypothetical" objects in unevaluated contexts like `decltype`, `sizeof`, `noexcept`, and `static_assert` to probe type information:

```cpp
#include <utility>

// 不需要知道 Container 的默认构造函数
// 就能获取其迭代器类型
template<typename Container>
using iterator_t = decltype(std::declval<Container>().begin());

// 获取两个值相加的结果类型
template<typename T, typename U>
using add_result_t = decltype(std::declval<T>() + std::declval<U>());
```

⚠️ Note: `std::declval` can only be used in unevaluated contexts (such as `decltype`, `sizeof`, `noexcept`, and `static_assert`). If you call it in runtime code, it will trigger a compilation error because it has a declaration but no definition.

------

## Other Practical Tips for decltype

### Obtaining Member Types

`decltype` can be combined with `std::declval` to obtain the member types of a container or class without needing to know the container's specific type:

```cpp
extern std::vector<int> global_data;
using value_t = decltype(global_data)::value_type;  // int
using iter_t  = decltype(global_data)::iterator;    // std::vector<int>::iterator
```

The benefit of this approach is that when the type of `container` changes from `std::vector<int>` to `std::map<int, int>`, all type aliases obtained via `decltype` will update automatically.

### Using decltype in constexpr

C++11's `decltype` can be used in a `constexpr` context right away, because it is a purely compile-time operation:

```cpp
constexpr int x = 42;
constexpr decltype(x) y = x + 1;  // constexpr int
```

### Working with Range-Based For Loops

Sometimes you need to know the exact type of an element in a range-based for loop. Although `auto` is usually sufficient, `decltype` can come in handy in certain metaprogramming scenarios:

```cpp
template<typename Range>
void process_range(Range&& r) {
    for (auto&& elem : r) {
        // elem 的类型是什么？
        using elem_t = decltype(elem);
        process_element(std::forward<elem_t>(elem));
    }
}
```

------

## Summary

The core value of `decltype` lies in "precisely preserving the type of an expression" without discarding references and `const`. Its deduction rules can be summarized in three points: for an unparenthesized variable name, it returns the declared type; for a parenthesized variable name or an lvalue expression, it returns an lvalue reference; for an rvalue expression, it returns a non-reference type.

`decltype(auto)` is a convenience introduced in C++14 that allows function return type deduction to preserve reference semantics, but we must watch out for the dangling reference trap of `decltype(auto)`. Trailing return types were the only way to handle return types dependent on parameters in C++11, but after C++14, they are replaced by `auto` and `decltype(auto)` in most scenarios.

In templates and metaprogramming, `decltype` combined with `std::declval` is a foundational tool for building type traits and SFINAE constraints. Once you understand these concepts, you will feel much more confident when reading and writing generic code.

## References

- [cppreference: decltype specifier](https://en.cppreference.com/w/cpp/language/decltype)
- [Effective Modern C++ - Scott Meyers, Item 3](https://www.oreilly.com/library/view/effective-modern-c/9781491908419/)
- [decltype and std::declval - cppreference](https://en.cppreference.com/w/cpp/utility/declval)
