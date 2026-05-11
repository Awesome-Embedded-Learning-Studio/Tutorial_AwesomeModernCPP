# Function Templates

Suppose we want to write a `max` function that takes two values and returns the larger one. The logic is straightforward—we can do it in two lines of code. But if our program needs to compare `int`, `double`, and `std::string` at the same time, we would need to write three versions: one `max(int, int)`, one `max(double, double)`, and one `max(std::string, std::string)`. The logic of all three versions is exactly the same—just `(a > b) ? a : b`—with the only difference being the parameter types.

This kind of repetitive code—"same logic, different types"—is everywhere in real-world projects. Sorting, searching, swapping, printing arrays—almost every generic operation runs into it. C++ provides a mechanism that lets us write the logic only once, and then the compiler automatically generates the corresponding function versions for different types. This is the function template. Starting with this chapter, we officially enter the world of C++ generic programming.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Use `template<typename T>` syntax to write generic functions
> - [ ] Understand the template instantiation mechanism—the difference between implicit and explicit instantiation
> - [ ] Master type deduction rules, knowing when deduction fails and how to resolve it
> - [ ] Understand the basic concept of template specialization
> - [ ] Make reasonable choices between function overloading and templates

## template\<typename T\>—The Starting Point of Generics

Let's start with the simplest example and write a generic `max_value` function (the reason we don't call it `max` is that `std::max` already exists in the standard library, and using the same name can easily cause conflicts on certain compilers—especially on Windows, where `<windows.h>` defines a `max` macro, which is the real blood pressure booster).

```cpp
template <typename T>
T max_value(T a, T b)
{
    return (a > b) ? a : b;
}
```

`template <typename T>` tells the compiler: this is a template, and `T` is a type parameter. In the function definition that immediately follows, everywhere `T` appears will be replaced with an actual type during instantiation. When we call `max_value(3, 5)`, the compiler deduces that `T` is `int`, so it generates a `int max_value(int, int)` version of the function. Calling `max_value(1.0, 2.0)` generates a `double max_value(double, double)` version. The entire process is transparent to the caller.

### What is the Difference Between typename and class

In a template parameter list, `typename` and `class` are completely equivalent—`template <typename T>` and `template <class T>` express the same meaning, with no semantic difference. Early C++ only supported the `class` keyword; `typename` was introduced later to eliminate the misconception that "T must be a class." `T` can be any type—built-in types (`int`, `double`, pointers), custom classes, or even function pointers. Modern C++ style prefers `typename` because its semantics are more accurate and it reads more clearly.

### Multiple Type Parameters

In some scenarios, one type parameter is not enough. For example, if we want to write a function that converts a value of one type to another type:

```cpp
template <typename Dest, typename Source>
Dest cast_to(Source value)
{
    return static_cast<Dest>(value);
}
```

There is no upper limit to the number of template parameters, but in real-world projects, having more than two or three is quite rare. With each additional type parameter, the likelihood that the caller needs to specify it explicitly increases, and the readability of the code decreases.

## Template Instantiation—The Compiler "Writes Code" for You

A template itself is not code—it is a "code recipe." Only when you actually call the template function does the compiler "expand" the template into a concrete function definition based on the types of the call arguments. This process is called template instantiation. (Feels a bit like a macro, right? If I remember correctly, that was indeed its original, original purpose!)

```cpp
int x = max_value(3, 5);       // T = int, 生成 int max_value(int, int)
double y = max_value(1.0, 2.0); // T = double, 生成 double max_value(double, double)
```

With the two calls above, the compiler generates two completely independent functions. They each exist in the compiled binary file, with the same effect as hand-writing two overloaded functions. This is also the core cost of templates—code bloat. If you instantiate the same template with 20 different types, the compiler will generate 20 copies of the function code. For small functions, this is not a problem, but for large templates (like the full specializations of certain STL algorithms), the code size can increase significantly.

### Implicit Instantiation vs Explicit Instantiation

The approach we just saw, where "the compiler automatically deduces types from the call arguments and generates code," is called implicit instantiation, and it is the most common method. But sometimes we need to explicitly tell the compiler which type to use—this is explicit instantiation:

```cpp
int result = max_value<double>(3, 5.0);  // 显式指定 T = double
```

Here, `3` is `int`, and `5.0` is `double`; the two types are different, so the compiler cannot deduce `T` as both `int` and `double` at the same time—we will discuss this deduction conflict in detail in the next section. By adding `<double>` after the function name, we explicitly specify the type of `T`, and the compiler will implicitly convert `3` to `double` and then call the `max_value<double>` version.

There is also a rarer syntax—the explicit instantiation definition, which forces the compiler to generate code for a specific version right here, even if the current translation unit doesn't use it:

```cpp
template int max_value<int>(int, int);           // 显式实例化定义
template double max_value(double, double);       // 同上，省略模板参数列表
```

This syntax is occasionally used in library development: put the template implementation in a `.cpp` file, then explicitly instantiate the type versions that the library needs to export, so that user code doesn't need to see the template implementation. However, in everyday application development, we almost never need to write explicit instantiation definitions by hand.

## Type Deduction—How the Compiler Guesses T

When calling `max_value(3, 5)`, the compiler sees that the arguments `3` and `5` are both `int`, so it deduces `T = int`. This process is called template argument deduction. Deduction happens at compile time and has no runtime overhead.

The rules of deduction are simple to state: every template parameter must be uniquely determined. If the same `T` appears in multiple parameters, the types of those parameters—after stripping references and top-level `const`—must be exactly the same, otherwise deduction fails.

### Typical Scenarios of Deduction Failure

```cpp
auto r = max_value(3, 5.0);  // 编译错误！
```

This code will directly report an error. The reason is that the type of `3` is `int`, so the compiler deduces `T = int`; the type of `5.0` is `double`, so the compiler deduces `T = double`. The same `T` cannot equal both `int` and `double` at the same time—a deduction contradiction.

> **Pitfall Warning**: Error messages when template deduction fails are usually very long. The compiler will list all the overloads and template candidates it tried, and then tell you "none of them match." For beginners, this kind of dozens-of-lines error message is quite off-putting. The solution is to locate the last line of the error message—it will usually point out exactly which parameter's type doesn't match. Then work backwards from the call site and check whether the type of each argument is consistent.

There are three ways to resolve a deduction conflict. The first is to explicitly specify the template argument, just like the `max_value<double>(3, 5.0)` we saw earlier, forcing `T = double`, and `3` will be implicitly converted. The second is to manually convert the argument type: `max_value(static_cast<double>(3), 5.0)`. The third is to modify the template itself to use two independent type parameters—but this approach requires caution, as we will discuss shortly.

### The Pitfall of Two Type Parameters

Someone might think: since `int` and `double` cause a deduction conflict, let's just use two type parameters.

```cpp
template <typename T, typename U>
???.??? max_value_two(T a, U b)
{
    return (a > b) ? a : b;
}
```

The problem lies in the return type—if `T` is `int` and `U` is `double`, should the return value be `int` or `double`? Using `auto` lets the compiler deduce it itself; `(a > b) ? a : b` in C++ follows the type deduction rules of the ternary operator, where `int` and `double` will be promoted to `double`, so the return value is `double`. But this only works for simple cases. In more complex scenarios, you might need `std::common_type_t<T, U>` to obtain the common type of the two types:

```cpp
template <typename T, typename U>
auto max_value_two(T a, U b) -> std::common_type_t<T, U>
{
    return (a > b) ? a : b;
}
```

`std::common_type_t` is defined in `<type_traits>`, and it selects the most appropriate common type based on the implicit conversion rules of the two types. But honestly, when you encounter mixed-type comparisons in daily use, the simplest approach is still to explicitly specify one type or manually cast—no need to make things this complicated.

## Template Specialization—When the Generic Solution Doesn't Fit

Our `max_value` works fine for most types, but for `const char*` (C-style strings), it compares the addresses of the two pointers rather than the contents of the strings. This behavior is obviously not what we want.

Template specialization allows us to provide a dedicated implementation for a specific type:

```cpp
// 通用模板
template <typename T>
T max_value(T a, T b)
{
    return (a > b) ? a : b;
}

// const char* 的特化版本
template <>
const char* max_value<const char*>(const char* a, const char* b)
{
    return (std::strcmp(a, b) > 0) ? a : b;
}
```

`template <>` indicates that this is a full specialization—all template parameters have been determined. When calling `max_value("hello", "world")`, if the compiler deduces `T = const char*`, it will prefer the specialized version over the generic version.

Specialization is a fairly large topic, involving partial specialization, SFINAE (Substitution Failure Is Not An Error), `concept` constraints, and more. Here we only need to know of its existence and basic syntax—we will dive deeper into it in the class template chapter.

## Function Overloading vs Templates—When to Use Which

Both function overloading and function templates can achieve "same-named functions handling different types," but their mechanisms are completely different. Function overloading means manually writing a version for each type, and the compiler selects the best match based on the argument types. Function templates mean writing a generic "recipe," and the compiler automatically generates the corresponding version based on the call.

The principle for choosing is actually quite intuitive: if the processing logic for all types is exactly the same and only the types differ, use a template—one `max_value` template is much cleaner than 20 manually written overloaded functions. If the processing logic for different types has fundamental differences—for example, `print(int)` directly outputs a number, while `print(std::string)` needs quotes—then use overloading, where each version's logic is independent and clear.

### Overload Resolution When Mixed

Templates and overloading can coexist, and the compiler has a well-defined set of overload resolution rules: first, it collects all candidate functions (including ordinary overloads and the instantiated versions of templates), then ranks them according to the precision of the type match, and selects the best match. If multiple candidates have the same degree of match, an ambiguity error occurs.

```cpp
template <typename T>
T max_value(T a, T b)
{
    return (a > b) ? a : b;
}

// 普通重载：int 版本
int max_value(int a, int b)
{
    std::cout << "int overload\n";
    return (a > b) ? a : b;
}

int main()
{
    max_value(3, 5);       // 调用普通重载（精确匹配优先于模板）
    max_value(1.0, 2.0);   // 调用模板实例化（double 无重载版本）
    max_value<>(3, 5);     // 强制使用模板，跳过普通重载
}
```

When both an ordinary overload and a template instantiation exist, if their degrees of match are the same, the ordinary function takes priority over the template instantiation. If you want to force the use of the template, you can use empty angle brackets `max_value<>(...)`.

> **Pitfall Warning**: When mixing overloading and templates, the easiest pitfall to fall into is ambiguity. Suppose you write a template `template <typename T> T max_value(T, T)` and an overload `double max_value(double, int)`, and then call `max_value(1.0, 2)`—the compiler will find that the template can be deduced as `T = double` (the second argument `2` is implicitly converted to `double`), while the overloaded version is also an exact match (`double` and `int`). The degrees of match are about the same, so it reports an ambiguity error. The solution is to keep interfaces as simple as possible—if you use a template, don't add overloads with subtly different parameter types for the same interface.
>
> **Pitfall Warning**: Another common pitfall is the interaction between templates and C-style strings. When calling `max_value("hello", "world")`, `T` is deduced as `const char*`. If you haven't written a specialization for `const char*`, it compares pointer addresses rather than string contents, and the result depends entirely on where the strings are located in memory—it might be different every time you run it, and it's almost certainly not the result you expect.

## Practical Exercise—func_template.cpp

Now let's combine all the knowledge we've learned so far and write a complete example program. It includes three generic functions: `max_value`, `swap_value`, and `print_array`, instantiated with `int`, `double`, and `std::string` respectively.

```cpp
// func_template.cpp
// 编译: g++ -Wall -Wextra -std=c++17 func_template.cpp -o func_template

#include <cstring>
#include <iostream>
#include <string>
// ============================================================
// max_value：返回两个值中较大的一个
// ============================================================
template <typename T>
T max_value(T a, T b)
{
    return (a > b) ? a : b;
}

// const char* 特化：按字典序比较字符串内容
template <>
const char* max_value<const char*>(const char* a, const char* b)
{
    return (std::strcmp(a, b) > 0) ? a : b;
}
// ============================================================
// swap_value：交换两个值
// ============================================================
template <typename T>
void swap_value(T& a, T& b)
{
    T temp = a;
    a = b;
    b = temp;
}
// ============================================================
// print_array：打印数组内容
// ============================================================
template <typename T, std::size_t kSize>
void print_array(const T (&arr)[kSize])
{
    std::cout << "[";
    for (std::size_t i = 0; i < kSize; ++i) {
        std::cout << arr[i];
        if (i + 1 < kSize) {
            std::cout << ", ";
        }
    }
    std::cout << "]";
}
// ============================================================
// main
// ============================================================
int main()
{
    // --- max_value ---
    std::cout << "=== max_value ===\n";
    std::cout << "max_value(3, 7) = " << max_value(3, 7) << "\n";
    std::cout << "max_value(2.5, 1.3) = " << max_value(2.5, 1.3)
              << "\n";
    std::cout << "max_value(\"banana\", \"apple\") = "
              << max_value("banana", "apple") << "\n";

    // 显式实例化：混合类型
    std::cout << "max_value<double>(3, 5.7) = "
              << max_value<double>(3, 5.7) << "\n";

    // --- swap_value ---
    std::cout << "\n=== swap_value ===\n";
    int a = 10, b = 20;
    std::cout << "before: a=" << a << ", b=" << b << "\n";
    swap_value(a, b);
    std::cout << "after:  a=" << a << ", b=" << b << "\n";

    double x = 1.5, y = 2.5;
    std::cout << "before: x=" << x << ", y=" << y << "\n";
    swap_value(x, y);
    std::cout << "after:  x=" << x << ", y=" << y << "\n";

    std::string s1 = "hello", s2 = "world";
    std::cout << "before: s1=\"" << s1 << "\", s2=\"" << s2 << "\"\n";
    swap_value(s1, s2);
    std::cout << "after:  s1=\"" << s1 << "\", s2=\"" << s2 << "\"\n";

    // --- print_array ---
    std::cout << "\n=== print_array ===\n";
    int nums[] = {3, 1, 4, 1, 5, 9};
    std::cout << "int[]:    ";
    print_array(nums);
    std::cout << "\n";

    double vals[] = {1.1, 2.2, 3.3};
    std::cout << "double[]: ";
    print_array(vals);
    std::cout << "\n";

    std::string names[] = {"Alice", "Bob", "Charlie"};
    std::cout << "string[]: ";
    print_array(names);
    std::cout << "\n";

    return 0;
}
```

Let's break down a few key points. `print_array` uses an array reference parameter `const T (&arr)[kSize]`, which not only allows the compiler to deduce the array element type `T` but also deduce the array length `kSize`, so there's no need to pass an additional length argument.

The parameter of `swap_value` is a reference `T&`, so that it can modify the caller's variable. If the parameter were passed by value as `T a, T b`, only the copies would be swapped, and the caller would be completely unaware.

### Verification and Execution

```bash
g++ -Wall -Wextra -std=c++17 func_template.cpp -o func_template && ./func_template
```

Expected output:

```text
=== max_value ===
max_value(3, 7) = 7
max_value(2.5, 1.3) = 2.5
max_value("banana", "apple") = banana
max_value<double>(3, 5.7) = 5.7

=== swap_value ===
before: a=10, b=20
after:  a=20, b=10
before: x=1.5, y=2.5
after:  x=2.5, y=1.5
before: s1="hello", s2="world"
after:  s1="world", s2="hello"

=== print_array ===
int[]:    [3, 1, 4, 1, 5, 9]
double[]: [1.1, 2.2, 3.3]
string[]: [Alice, Bob, Charlie]
```

Let's verify a few key results: `max_value(3, 7)` correctly returns `7`; `max_value("banana", "apple")` goes through the `const char*` specialized version, comparing lexicographically, and since `"banana"` is greater than `"apple"`, it returns `"banana"`; the values before and after `swap_value` are correctly swapped; `print_array` correctly prints the contents of three different type arrays without any trailing commas.

## Exercises

### Exercise 1: Generic Search

Implement a generic function `find_index` that searches for a value in an array and returns its index; if not found, it returns `-1`. The function signature is roughly:

```cpp
template <typename T, std::size_t kSize>
int find_index(const T (&arr)[kSize], const T& target);
```

Requirement: test with three types: `int`, `double`, and `std::string`. Think about it: if `T` is a custom class, can this function work properly? What conditions must the custom class satisfy?

### Exercise 2: Generic Sorting

Implement a simple generic bubble sort function `bubble_sort` that sorts an array in place. You don't need to implement the comparison logic yourself—just use `operator>` or `operator<`. Requirement: be able to sort and print the results of `int`, `double`, and `std::string` arrays respectively.

### Exercise 3: Generic Accumulator

Implement a generic function `accumulate_all` that calculates the sum of all elements in an array. Think about the return type issue: if the array elements are `int`, the sum might exceed the range of `int`—how should you handle this? Hint: you can add a template parameter to serve as the accumulator type.

## Summary

In this chapter, we learned the core mechanism of C++ function templates. `template <typename T>` allows us to write the logic only once, and the compiler automatically generates the corresponding function versions for different types based on the calls. Template instantiation happens at compile time with no runtime overhead, but it produces code bloat. Type deduction requires that the same template parameter be deduced as the same type in all positions where it appears, otherwise deduction fails—at this point, you can use explicit template arguments, type conversions, or multiple type parameters to resolve it. Template specialization allows us to provide a dedicated implementation for specific types, making up for the shortcomings of the generic solution.

A few key takeaways: `typename` and `class` are equivalent in a template parameter list, but `typename` has clearer semantics; when mixing overloading and templates, watch out for ambiguity; `const char*` compares pointer addresses rather than string contents, so you should either write a specialization or use `std::string`.

In the next chapter, we move on to class templates—extending generic capabilities from functions to entire classes. Function templates let us write "type-independent functions," while class templates let us write "type-independent classes." Containers (`vector`, `map`), smart pointers (`unique_ptr`, `shared_ptr`), and even `std::string` are essentially class templates. Once you understand function templates, learning class templates will go much more smoothly—the core idea is the same, it's just that the scope expands from functions to classes.
