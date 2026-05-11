# A First Look at const

When writing code, some things simply should not be changed—configuration parameters should not be accidentally overwritten once set, an array's declared capacity should not change, and physical constants like pi go without saying. If we relied solely on "discipline" to ensure these values remain untouched, it would be like walking blindfolded at night. Sooner or later, a slip of the finger would alter a critical value, and we would spend half a day tracking down a baffling bug.

C++ provides us with a safety lock: `const`. Its core idea is simple—if something should not change, we explicitly tell the compiler and let it watch over it for us. Any code that attempts to modify a `const` value gets blocked right at compile time. Catching the problem during compilation is obviously far more reliable than discovering corrupted data in production. (Rust even flips this entirely: if you don't say something is mutable, it isn't! So variables are `const` by default!)

## Locking Down a Variable—Basic const Usage

Let's start with the simplest scenario. Suppose we have a maximum buffer capacity that should remain unchanged throughout the program's execution:

```cpp
const int kMaxBufferSize = 1024;
```

Once we add `const`, this variable becomes read-only—we must give it an initial value at declaration, and the compiler will reject any subsequent attempt to modify it. Let's try it:

```cpp
const int kMaxBufferSize = 1024;
kMaxBufferSize = 2048;  // 编译错误！
```

The compiler will give a very clear error:

```text
error: assignment of read-only variable 'kMaxBufferSize'
```

This is the core value of `const`—it turns "I shouldn't change this value" from a discipline-based convention into a compiler-enforced rule. You might ask, isn't this just using the compiler as a bodyguard? Exactly, and this bodyguard never nods off.

### What's the Real Difference Between const and #define?

If you have a C background, you might say, "I can already do this with `#define`." It's true that `#define` produces a similar effect, but there are a few key differences between the two.

First, a `const` variable has an explicit type. The `int` in `const int MAX_SIZE = 1024;` tells the compiler this is an integer. If we accidentally assign it to a `char`, the compiler can perform type checking and even issue a warning. `#define`, on the other hand, is just simple text replacement. The preprocessor doesn't care about types at all—it faithfully replaces every `MAX_SIZE` with `1024`, regardless of whether `1024` is an integer or a floating-point number.

Second, `const` variables follow normal scoping rules. A `const` variable declared inside a function is only visible within that function, while a `const` variable declared globally has internal linkage by default (meaning other `.cpp` files cannot see it). Once a `#define` is expanded, it takes effect from the point of definition to the end of the file, with no scope restrictions at all—this easily leads to name collisions in large projects.

Finally, during debugging, a `const` variable is just a normal variable; you can see its name and value in the debugger. A `#define`, however, gets replaced during preprocessing, so the debugger only sees a bare number like `1024`, and you have no idea where that 1024 came from.

So our conclusion is: in C++, prefer `const` or `constexpr` (which we will cover later) to define constants, and leave `#define` for scenarios that truly require conditional compilation.

Regarding naming conventions, constants in this tutorial uniformly use the `kCamelCase` style, such as `kMaxBufferSize`, `kPi`, and `kDefaultTimeout`. The `k` prefix is a fairly common constant naming convention in the C++ community, making it immediately obvious that the value should not be modified.

## const and Pointers—The Most Confusing Part

Using `const` to modify a plain variable is straightforward, but when `const` meets pointers, things get interesting. Many developers get confused in this area, including myself when I first started learning. Don't worry—let's break it down step by step.

The core question is: does `const` modify the pointer itself, or the data the pointer points to? The answer depends on where `const` appears. C++ has three `const` and pointer combinations, and we will look at each one.

### Pointer to const: `const int* p`

```cpp
int value = 42;
const int* p = &value;
```

Here, `const` modifies `int`, meaning modifying the data through `p` is not allowed. But the pointer `p` itself can change—it can point to a different address. You can think of it as a "well-behaved pointer that promises not to modify the target data through itself."

```cpp
int x = 10;
int y = 20;
const int* p = &x;

*p = 100;   // 编译错误！不能通过 const int* 修改数据
p = &y;     // 没问题，指针本身可以指向别的地方
```

Note a detail: although we cannot modify the value of `value` through `p`, `value` itself is not `const`. Modifying it directly with `value = 20;` is perfectly legal—`const int* p` just means "I won't modify it through this pointer," not that the target data is truly immutable.

### const pointer: `int* const p`

```cpp
int value = 42;
int* const p = &value;
```

This time, `const` modifies the pointer variable `p` itself. This means once the pointer is initialized, it is locked onto that address and cannot point anywhere else. However, modifying the target data through `p` is perfectly allowed.

```cpp
int x = 10;
int y = 20;
int* const p = &x;

*p = 100;   // 没问题，可以修改数据
p = &y;     // 编译错误！指针本身是 const 的，不能改指向
```

You can think of it as a "stubborn pointer"—it latches onto an address and won't budge, but it can freely modify the contents at that address.

### Both const: `const int* const p`

```cpp
int value = 42;
const int* const p = &value;
```

This syntax stacks both constraints together: the pointer itself cannot change where it points, and the data cannot be modified through the pointer. This pattern is actually quite common in function parameters—when passing a pointer to a function where we neither want the function to change the pointer's target nor modify the data, we write it this way.

### Reading Right to Left—A Practical Reading Trick

Many developers find these three combinations hard to remember. Here is a classic reading technique: **read the declaration from right to left**. Let's take `const int* const p` as an example:

- Start from the variable name `p`, and read left
- `const` → p is a constant
- `*` → constant pointer
- `int` → pointing to int type
- `const` → this int is constant

Put together: `p` is a constant pointer, pointing to a constant int.

Now look at `const int* p`: `p` is a pointer (`*`), pointing to a constant int (`const int`)—data cannot be modified, pointer can be modified.

`int* const p`: `p` is a constant (`const`) pointer (`*`), pointing to an int—pointer cannot be modified, data can be modified.

Practice with a few more examples, and you will quickly develop an intuition for it.

> **Pitfall Warning**: Interviews and exams love to test the differences between these three declarations. If you can't tell them apart on the spot, don't guess—use the right-to-left reading method and break it down step by step. It's far more reliable than relying on memory. Additionally, `const int* p` and `int const* p` are actually two completely equivalent ways to write the same thing; `const` can go before or after `int`. But `int* const p` is different—`const` has moved to the right of the `*`, modifying the pointer. This positional difference is the key.

This isn't the only pitfall. Many beginners assume that `const int* p` means `value` itself has become a constant—it hasn't. `value` is still a plain variable, and you can directly modify `value`. `const int* p` means "I won't modify it through this pointer"; it is an access constraint, not a constraint on the target data itself.

## const and References

Now that we have covered pointers, let's look at references. Combining `const` with references is much simpler than with pointers, because references themselves are not allowed to be rebound—they are bound to a variable from birth. So there is only one scenario for `const` combined with references:

```cpp
int x = 42;
const int& ref = x;
```

`ref` is an alias for `value`, but we cannot modify `value` through `ref`. Similar to `const int* p`, this just means "I won't modify it through `ref`"; `value` itself can still be freely modified.

This "const reference" has an extremely important use case in real-world development—function parameters. Imagine you have a function that needs to receive a `std::string` parameter:

```cpp
void print(std::string s)
{
    std::cout << s << std::endl;
}
```

Every time we call `printMessage`, a string copy occurs. If the string is long, or if this function is called frequently, this copy overhead becomes non-negligible. Changing it to a `const` reference solves this:

```cpp
void print(const std::string& s)
{
    std::cout << s << std::endl;
}
```

`const std::string&` means: receive a reference (no copy), but promise not to modify it. This avoids the copy overhead while guaranteeing safety to the caller. This `const T&` parameter pattern appears extremely frequently in C++, and we will encounter it repeatedly in later chapters. For now, just keep it in mind.

## constexpr—Let the Compiler Do the Math

So far, the `const` we have discussed simply means "this value will not change at runtime." But the values of some constants are already determined at compile time—for example, `3.14 * 2` definitely equals `6.28`, and there is absolutely no need to wait until the program runs to calculate it. C++11 introduced `constexpr` to explicitly tell the compiler: "You can calculate this value at compile time."

```cpp
constexpr int kSquare = 5 * 5;           // 编译期就算好了，值为 25
constexpr int kBufferSize = 1024 * 64;   // 同样在编译期计算
```

The relationship between `constexpr` and `const` can be summarized in one sentence: a `constexpr` value is always `const` (a compile-time constant certainly cannot be modified), but a `const` value is not necessarily `constexpr` (a read-only value determined at runtime also counts as `const`). For example:

```cpp
int x = 10;
const int cx = x;          // const 但不是 constexpr，因为 x 的值运行时才知道
constexpr int kVal = 42;   // constexpr，同时也是 const
```

The real power of `constexpr` is that it can be applied to functions. A `constexpr` function means: if the arguments passed in are all values that can be determined at compile time, the return value of this function can also be calculated at compile time:

```cpp
constexpr int square(int x)
{
    return x * x;
}

constexpr int kResult = square(5);  // 编译期就算好了，kResult = 25, 不相信让AI告诉你如何objdump或者dumpbin看汇编，这里不教了
```

Values calculated at compile time have a major advantage: they can be used in places that require constant expressions, such as array sizes:

```cpp
constexpr int kArraySize = square(3);  // 9
int data[kArraySize];                   // 合法，因为 kArraySize 是编译期常量
```

If `size` were just a plain `const`, this line might not compile on some compilers (depending on whether the `const` variable is treated as a constant expression). Using `constexpr` leaves no ambiguity.

Here we are just getting an initial taste of `constexpr`. It is one of the most important features of modern C++—in C++14, it allowed more complex logic inside functions; in C++17, the restrictions were further relaxed; and in C++20, `consteval` (which must be executed at compile time) and `constinit` were introduced. We will have a dedicated chapter later to dive deep into compile-time computation. For now, just remember: if your constant value can be determined at compile time, prefer `constexpr`.

> **Pitfall Warning**: A `constexpr` function is not guaranteed to execute at compile time. The compiler only forces compile-time calculation in scenarios that "require a compile-time constant" (such as array sizes or template parameters). In other cases, the compiler might choose to calculate at compile time, or it might choose to calculate at runtime—this depends on the compiler's optimization strategy and the function's complexity. If you need to force compile-time execution, C++20's `consteval` is the correct choice.

## Comprehensive Practice—const_demo.cpp

Reading about it is one thing; doing it is another. Let's string together all the `const` usages we discussed above and write a complete example program. This program won't have overly complex logic, but it will cover every `const` combination and verify the compiler's behavior.

```cpp
// const_demo.cpp —— 演示 const 变量、指针、引用和 constexpr 的各种用法

#include <iostream>

/// @brief constexpr 函数：计算平方
/// @param x 被平方的值
/// @return x 的平方
constexpr int square(int x)
{
    return x * x;
}

int main()
{
    // --- const 变量 ---
    const int kMaxSize = 100;
    // kMaxSize = 200;  // 取消注释会编译错误
    std::cout << "kMaxSize = " << kMaxSize << std::endl;

    // --- constexpr ---
    constexpr int kArraySize = square(5);  // 编译期计算，结果为 25
    std::cout << "kArraySize = " << kArraySize << std::endl;

    // --- 指向常量的指针 ---
    int a = 10;
    int b = 20;
    const int* p_to_const = &a;
    // *p_to_const = 100;  // 取消注释会编译错误
    p_to_const = &b;       // 没问题，指针可以改指向
    std::cout << "*p_to_const = " << *p_to_const << std::endl;

    // --- 常量指针 ---
    int* const const_p = &a;
    *const_p = 100;        // 没问题，可以改数据
    // const_p = &b;       // 取消注释会编译错误
    std::cout << "*const_p = " << *const_p << std::endl;

    // --- 两个都 const ---
    const int* const double_const = &a;
    // *double_const = 1;  // 编译错误
    // double_const = &b;  // 编译错误
    std::cout << "*double_const = " << *double_const << std::endl;

    // --- const 引用 ---
    int x = 42;
    const int& ref = x;
    // ref = 100;           // 编译错误
    x = 100;               // 直接改 x 是可以的
    std::cout << "ref = " << ref << std::endl;  // 输出 100

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o const_demo const_demo.cpp
./const_demo
```

Expected output:

```text
kMaxSize = 100
kArraySize = 25
*p_to_const = 20
*const_p = 100
*double_const = 100
ref = 100
```

You can uncomment those "compile error" lines one by one and see what error messages the compiler gives. Actually experiencing how the compiler intercepts these operations leaves a much deeper impression than just reading about it.

## Try It Yourself

Now that we have covered the theory, it's your turn to get your hands dirty. The three exercises below will help you test your understanding of `const`. We recommend writing, compiling, and running each one completely.

### Exercise 1: Declare const Pointers and Predict Behavior

Write out the following declarations, then for each pointer, try to (1) modify the data the pointer points to, and (2) modify the pointer's own target address. Before compiling, predict which operations the compiler will reject, and then verify your predictions.

- `const int* p1`
- `int* const p2`
- `const int* const p3`

### Exercise 2: Convert #define to constexpr

Below is a piece of C-style code using `#define`. Replace all macro constants with `constexpr` variables, and write a `constexpr` function `calculateArea` to compute the area of a circle.

```cpp
#define PI 3.14159265
#define MAX_RADIUS 100.0
#define MIN_RADIUS 0.1
```

### Exercise 3: Write a Function Using const Reference Parameters

Write a function `printSum` that receives two `int` parameters and prints their sum. Then call it in the `main` function. Think about this: for a small type like `int`, is there a performance difference between using `const int&` and using `int` directly as the parameter? What types of parameters are best suited for passing with `const&`?

## Summary

In this chapter, we centered on the `const` keyword and walked through the most common "read-only" mechanisms in C++. A `const` variable must be initialized at declaration and cannot be modified afterward; it is safer, carries more type information, and is easier to debug than `#define`. The combinations of `const` and pointers are the most error-prone area—`const int*` is a "pointer to const" (data cannot be modified, pointer can be modified), and `int* const` is a "const pointer" (pointer cannot be modified, data can be modified). Reading from right to left is an effective way to distinguish them. `const` references are extremely common in function parameters, and the `const T&` pattern avoids copies while ensuring safety. `constexpr` is a stricter form of constant—it requires the value to be calculable at compile time, which makes programs run faster and enables use in scenarios that require constant expressions, such as array sizes.

In the next chapter, we will enter the world of value categories—what exactly are lvalues and rvalues, and why can move semantics make programs run faster? These concepts might sound a bit abstract, but once you understand `const`, you will find that many of the underlying ideas are quite similar.
