---
title: Function Basics
description: Master C++ function definition, declaration, parameter passing, and return
  values, and understand scope and lifetime.
chapter: 3
order: 1
difficulty: beginner
reading_time_minutes: 15
platform: host
prerequisites:
- range-for 循环
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
cpp_standard:
- 11
- 14
- 17
- 20
translation:
  source: documents/vol1-fundamentals/ch03/01-function-basics.md
  source_hash: 2900f0ca3e5ff4ce6e303da4140691fc7a48305d31bbf5ca17d86336a544b28c
  translated_at: '2026-05-26T10:45:59.629535+00:00'
  engine: anthropic
  token_count: 2137
---
# Function Basics

Kids, I've seen it before—someone writes a ten-thousand-line program with just `main()` single function from top to bottom, with all the code piled together like spaghetti. Obviously, this person doesn't really understand functions (beginners excluded).

What's it like to read this? Variables are everywhere, logic is tangled up, and changing one feature means reading the entire file for fear of pulling one thread and unraveling the whole thing. Frankly, forget about showing this code to anyone else—even after a week, you won't understand it yourself. The joke goes that only God can read it (though maybe after another week, even God won't be able to).

Functions are the core tool for solving this problem. They let us wrap a piece of code that accomplishes a specific task into a named unit. When we need it, we simply call the name without worrying about the internal implementation details. In this chapter, starting from the most basic concepts, we will thoroughly nail down the fundamentals: function definition styles, parameter passing, return values, and scope.

## Step One — Function Declaration and Definition

Before writing a function, we need to understand two concepts: **declaration** and **definition**. A declaration tells the compiler "a function like this exists"—it only provides the function name, return type, and parameter list, without a function body. A definition provides the complete implementation.

```cpp
// 声明（也叫函数原型/prototype）
int add(int a, int b);

// 定义（包含函数体）
int add(int a, int b)
{
    return a + b;
}
```

The semicolon at the end of the declaration takes the place of the function body. When the compiler sees the declaration, it knows that `add` is a function that takes two `int` parameters and returns a `int`. As for how it's implemented internally, the compiler doesn't care for now—as long as it can find the actual definition at link time.

So why distinguish between the two? Because the C++ compiler processes code line by line, top to bottom. If `main()` calls `add()`, but `add`'s definition is written after `main`, the compiler won't know what `add` is when processing `main`, and it will throw an error directly. The solution is to put a declaration at the top of the file so the compiler knows about the function in advance:

```cpp
#include <iostream>

// 先声明，告诉编译器这些函数存在
int add(int a, int b);
int multiply(int a, int b);

int main()
{
    std::cout << add(3, 4) << std::endl;       // 编译器知道 add 的签名
    std::cout << multiply(3, 4) << std::endl;  // 编译器知道 multiply 的签名
    return 0;
}

// 定义放在后面，完全没问题
int add(int a, int b)
{
    return a + b;
}

int multiply(int a, int b)
{
    return a * b;
}
```

And what do we call it when a bunch of these declarations are grouped together? That's exactly what a header file is! This "declare first, define later" pattern is crucial in real projects—as we'll see when we get to header files, declarations are usually placed in `.h` files to be shared across multiple source files, while definitions go in `.cpp` files. For now, just remember one principle: **the compiler must see a function's declaration (or definition) before the function is used**.

> ⚠️ **Pitfall Warning**
> Forgetting to write a declaration and placing the function definition after the call site is one of the most common compilation errors beginners encounter. The error message is usually `error: use of undeclared identifier 'xxx'`. When we see this, our first reaction should be to check the function definition's location—either move the definition above the call site, or add a declaration at the top of the file.

## Step Two — Return Types and the return Statement

Every C++ function has a return type, written before the function name, telling the compiler what type of value the function will produce when it finishes executing. The `return` statement sends a value back to the caller and simultaneously ends the function's execution.

```cpp
int max(int a, int b)
{
    if (a > b) {
        return a;   // 返回 a，函数立即结束
    }
    return b;       // 返回 b
}
```

A function can have multiple `return` statements, but each call will only execute one of them—once `return` executes, all subsequent code is skipped. For the `max` function above, both paths guarantee that a `return` will happen, so there's no problem.

If a function doesn't need to return any value, we write the return type as `void`. A `void` function can omit the `return` statement, and it will automatically return when the function body finishes executing; we can also write a bare `return;` to exit early:

```cpp
void print_greeting(const std::string& name)
{
    if (name.empty()) {
        return;  // 提前退出，不打印任何内容
    }
    std::cout << "Hello, " << name << "!" << std::endl;
}
```

C++14 introduced a very practical feature: **return type deduction**. By writing `auto` in the function's return type position, the compiler automatically deduces the return type based on the `return` statement:

```cpp
auto add(int a, int b)
{
    return a + b;  // 编译器推导出返回类型为 int
}
```

This is especially convenient for functions where the return type is verbose to write or in template code. But there's a limitation: all `return` statements must return the same type. If one path returns a `int` and another returns a `double`, the compiler will report an error.

> ⚠️ **Pitfall Warning**
> Forgetting to write a `return` in a non-`void` function is a classic bug. The compiler might give a warning but won't necessarily error—if control flow reaches the end of the function without encountering a `return`, the behavior is **undefined behavior**. The function might return a garbage value, or the program might crash outright—it's entirely up to luck. So, we must build a good habit: every execution path in a non-`void` function must have a `return`.

## Step Three — Parameters and Arguments

Functions receive externally passed data through **parameters**. Variables declared in the function signature are called formal parameters (parameters), and the actual values passed in during the call are called actual arguments (arguments):

```cpp
//          形参
//            ↓    ↓
int add(int a, int b)
{
    return a + b;
}

int main()
{
    //       实参
    //        ↓    ↓
    int result = add(3, 4);  // a 接收 3, b 接收 4
    return 0;
}
```

A function can have any number of parameters, or none at all. When there are no parameters, we leave the parentheses empty (in C++, empty parentheses and `void` are equivalent: `int foo()` and `int foo(void)` mean the same thing).

In a multi-parameter function, arguments and parameters correspond **by position** one-to-one—the first argument goes to the first parameter, the second to the second, and so on. C++ doesn't support named parameter calls like Python, so the parameter order must line up correctly:

```cpp
void print_info(const std::string& name, int age, double height)
{
    std::cout << name << ", " << age << " 岁, "
              << height << " cm" << std::endl;
}

int main()
{
    // 按位置传递，顺序不能搞错
    print_info("Alice", 20, 165.5);
    return 0;
}
```

The argument types need to match the parameter types, or be implicitly convertible. For example, if the parameter is `double`, passing a `int` is valid (an implicit conversion will occur), but the reverse might lose precision. By default, parameters are **passed by value**—the function internally receives a copy of the argument, and modifying the copy doesn't affect the original data. We'll discuss pass-by-reference and pass-by-pointer in detail in the next chapter.

## Step Four — Local Scope and Lifetime

Variables declared inside a function body are called **local variables**, and their scope is limited to the inside of that function. In other words, from the point of `{` to the closing `}`, the variable is visible; outside this range, the variable no longer exists:

```cpp
int compute(int x)
{
    int result = x * 2;  // result 是局部变量
    return result;
}   // result 在这里被销毁

int main()
{
    int r = compute(5);
    // std::cout << result;  // 编译错误！result 不在作用域内
    return 0;
}
```

Local variables are stored on the **stack**. When a function is called, the system allocates space on the stack for its local variables; when the function returns, this space is reclaimed and the variables are immediately destroyed. This process is automatic—we don't need to manage it manually.

Different functions can use variables with the same name without interfering with each other, because each has its own independent scope:

```cpp
void func_a()
{
    int value = 10;  // func_a 的 value
    std::cout << "func_a: " << value << std::endl;
}

void func_b()
{
    int value = 20;  // func_b 的 value，跟 func_a 的毫无关系
    std::cout << "func_b: " << value << std::endl;
}
```

Even different code blocks within the same function can have variables with the same name, where the inner block **shadows** the outer block's variable—though in actual development, we don't recommend doing this because it hurts readability.

> ⚠️ **Pitfall Warning**
> Returning a **reference** or **pointer** to a local variable is a serious error, and the compiler might not always catch it for you. Local variables are destroyed after the function returns, so the memory the reference or pointer points to is already invalid—this is the classic "dangling reference" problem:
>
> ```cpp
> int& dangerous()
> {
>     int local = 42;
>     return local;  // 严重错误：返回局部变量的引用
> }   // local 在这里被销毁，引用指向的内存已无效
> ```
>
> The program might run perfectly fine while you're debugging, but suddenly crash when you switch to a Release build or when the data volume increases. This kind of intermittent bug is harder to track down than a consistent crash. The rule is simple: **never return a reference or pointer to a local variable**. Returning by value is safe—it copies the data to the caller.

## Step Five — A First Glimpse of Function Overloading

C++ allows us to define multiple functions with the same name, as long as their parameter lists differ (different number of parameters, or different parameter types). This is called **function overloading**:

```cpp
int add(int a, int b)
{
    return a + b;
}

double add(double a, double b)
{
    return a + b;
}
```

The compiler automatically selects the best-matching version based on the types of arguments passed in at the call site—`add(3, 4)` calls the `int` version, and `add(3.5, 2.1)` calls the `double` version. This greatly helps code readability and consistency, as callers don't need to remember a bunch of different names like `add_int` and `add_double`.

There are quite a few details to the full rules of function overloading, such as overload resolution priority and ambiguity handling, which we'll dive into in later chapters. For now, just knowing that this exists is enough.

## Hands-On Practice — functions.cpp

Let's integrate the concepts we've learned into a complete program, demonstrating function declarations, definitions, return value handling, and local scope:

```cpp
// functions.cpp
// Platform: host
// Standard: C++17

#include <iostream>
#include <string>

// 函数声明（原型）
int add(int a, int b);
int max_of(int a, int b);
int factorial(int n);
bool is_even(int n);
void print_result(const std::string& label, int value);

// main 函数——程序入口
int main()
{
    // 加法
    int sum = add(15, 27);
    print_result("15 + 27", sum);

    // 取较大值
    int bigger = max_of(42, 17);
    print_result("max(42, 17)", bigger);

    // 阶乘
    int fact = factorial(6);
    print_result("6!", fact);

    // 判断奇偶
    int test_values[] = {0, 1, 2, 7, 10};
    for (int val : test_values) {
        std::cout << val << " 是"
                  << (is_even(val) ? "偶数" : "奇数")
                  << std::endl;
    }

    return 0;
}

// ---- 函数定义 ----

int add(int a, int b)
{
    return a + b;
}

int max_of(int a, int b)
{
    if (a > b) {
        return a;
    }
    return b;
}

/// @brief 计算 n 的阶乘（n!）
/// @param n 非负整数
/// @return n 的阶乘
int factorial(int n)
{
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

bool is_even(int n)
{
    return n % 2 == 0;
}

void print_result(const std::string& label, int value)
{
    std::cout << label << " = " << value << std::endl;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o functions functions.cpp
./functions
```

Output:

```text
15 + 27 = 42
max(42, 17) = 42
6! = 720
0 是偶数
1 是奇数
2 是偶数
7 是奇数
10 是偶数
```

In this program, `factorial` is a **recursive function**—it calls itself within its own function body. The idea behind recursion is to break `n!` down into `n * (n-1)!`, until `n <= 1` when it directly returns 1 as the base case. Recursion is a powerful programming technique, but it comes with a cost—each recursive call allocates new local variable space on the stack. Think about it: if we call ourselves like crazy, meaning "the recursion goes too deep," we'll cause a stack overflow! So in actual engineering work, unless a loop is really hard to write and we can be absolutely certain the nesting depth won't be too deep, we might consider recursion. Otherwise, it's strictly forbidden. At least when I started working, I'd definitely get scolded for pulling something like that. We'll discuss the choice between recursion and iteration more deeply in later chapters.

One point worth noting is that the parameter type of the `print_result` function is `const std::string&` rather than `std::string`. Here, `&` indicates pass-by-reference, avoiding the overhead of copying the string; `const` indicates that the function won't modify the string internally. Although the details of pass-by-reference won't be formally covered until the next chapter, this pattern is extremely common in real code, so it's good to get familiar with the sight of it early.

## Run Online

Run the function basics comprehensive example online to observe function declarations, recursion, and parameter passing:

<OnlineCompilerDemo
  title="Function Basics Comprehensive Exercise: Declarations, Recursive Factorial, Odd/Even Check"
  source-path="code/examples/vol1/08_function_basics.cpp"
  description="Run online and observe the actual behavior of function declarations, definitions, recursion, and various parameter passing methods."
  allow-run
/>

## Try It Yourself

### Exercise 1: Greatest Common Divisor

Write a function `int gcd(int a, int b)` that uses the Euclidean algorithm to calculate the greatest common divisor of two positive integers. The algorithm is simple: if `b` is 0, return `a`; otherwise, recursively call `gcd(b, a % b)`.

```text
gcd(48, 18)  → 6
gcd(100, 75) → 25
gcd(7, 3)    → 1
```

### Exercise 2: Prime Number Check

Write a function `bool is_prime(int n)` that determines whether a positive integer `n` is a prime number. Handle edge cases: numbers less than 2 are not prime, and 2 is prime. Hint: we only need to check whether any number in the range from 2 to `sqrt(n)` can evenly divide `n`.

```text
is_prime(2)  → true
is_prime(17) → true
is_prime(18) → false
is_prime(1)  → false
```

### Exercise 3: Returning Multiple Values with struct

A C++ function can only return one value, but we can pack multiple values into a `struct` and return that. Define a `struct DivResult` containing the quotient and remainder, then write a function `divmod` that returns both at the same time:

```cpp
struct DivResult {
    int quotient;
    int remainder;
};

DivResult divmod(int dividend, int divisor);
```

```text
divmod(17, 5) → 商: 3, 余: 2
divmod(100, 7) → 商: 14, 余: 2
```

## Summary

In this chapter, we learned the fundamental mechanics of C++ functions from scratch. A function declaration tells the compiler "this function exists," while a definition provides the concrete implementation—the compiler must see either a declaration or a definition before using a function. The return type determines the type of value the function produces, and every execution path in a non-`void` function must have a `return` statement. Parameters are passed by position, one-to-one, and are copied by value by default. Local variables are scoped to the function body and are automatically destroyed when the function returns—which is exactly why we must never return a reference or pointer to a local variable.

Function overloading lets us use the same name to handle different parameter types, and the compiler automatically selects the most appropriate version. In addition, we got our first taste of recursion—a programming technique where a function calls itself, demonstrated here with factorial calculation.

These are the bones of functions. Next, we'll dive into the details of parameter passing—the working mechanisms and applicable scenarios of pass-by-value, pass-by-reference, and pass-by-pointer. That's what truly determines a program's performance and correctness.
