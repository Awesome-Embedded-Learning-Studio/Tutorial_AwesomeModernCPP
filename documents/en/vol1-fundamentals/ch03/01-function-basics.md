---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: Master C++ function definitions, declarations, parameter passing, and
  return values, and understand scope and lifetime.
difficulty: beginner
order: 1
platform: host
prerequisites:
- range-for 循环
reading_time_minutes: 13
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Function Basics
translation:
  source: documents/vol1-fundamentals/ch03/01-function-basics.md
  source_hash: b447aed7a66cd70cb041ac794e26bb441a6da9208d246454779b0c0fefec5551
  translated_at: '2026-06-16T05:56:41.878952+00:00'
  engine: anthropic
  token_count: 2133
---
# Function Basics

Kids, I have actually seen people write code where the entire program is ten thousand lines long with only a `main()` function, with all the code piled together like spaghetti. Obviously, this person doesn't quite understand functions (beginners excepted).

What is it like to read this? Variables are everywhere, logic is inextricably tangled, and modifying a feature requires reading the entire text for fear that pulling one thread will move the whole body. Honestly, you can't even show this kind of code to others; after a week, even you won't understand it. The joke is that only God can understand it now (though perhaps even God won't understand it in another week).

Functions are the core tool to solve this problem. They allow us to encapsulate a piece of code that completes a specific task into a named unit. When we need it, we simply call it by name without worrying about the internal implementation details. In this chapter, starting from the most basic concepts, we will thoroughly clarify the basics of function definition, parameter passing, return values, and scope.

## First Step — Declaration and Definition

Before writing a function, we need to understand two concepts: **declaration** and **definition**. A declaration tells the compiler "such a function exists," providing only the function name, return type, and parameter list, without the function body. A definition, on the other hand, provides the complete implementation.

```cpp
// 声明（也叫函数原型/prototype）
int add(int a, int b);

// 定义（包含函数体）
int add(int a, int b)
{
    return a + b;
}
```

The semicolon at the end of the declaration replaces the function body. When the compiler sees this declaration, it knows that `add` is a function that takes two `int` parameters and returns an `int`. The compiler doesn't care how it is implemented for now—as long as the linker can find the actual definition later.

Why do we need to distinguish between the two? Because the C++ compiler processes code line by line, from top to bottom. If `main()` calls `add()`, but the definition of `add` is written after `main`, the compiler doesn't know what `add` is when processing `main` and will report an error. The solution is to place a declaration at the beginning of the file so the compiler knows about this function in advance:

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

What if we group a large number of these declarations together? That's exactly what header files are for! This "declare first, define later" pattern is crucial in real-world projects. As we will see when we cover header files, declarations are typically placed in `.h` files to be shared across multiple source files, while definitions reside in `.cpp` files. For now, just remember one rule: **the compiler must see a declaration (or definition) of a function before it is used.**

> ⚠️ **Warning**
> Forgetting to write a declaration, or placing the function definition after the call site, is one of the most common compilation errors for beginners. The error message usually looks like `error: use of undeclared identifier 'xxx'`. When you see this, your first instinct should be to check the location of the function definition—either move the definition above the call site, or add a declaration at the beginning of the file.

## Step 2 — Return Type and the `return` Statement

Every C++ function has a return type, written before the function name, which tells the compiler what kind of value the function will produce after execution. The `return` statement is used to send a value back to the caller and simultaneously end the function's execution.

```cpp
int max(int a, int b)
{
    if (a > b) {
        return a;   // 返回 a，函数立即结束
    }
    return b;       // 返回 b
}
```

Functions can have multiple `return` statements, but only one executes per call—once `return` executes, the remaining code is skipped. In the `max` function above, both paths guarantee a `return`, so there are no issues.

If a function does not need to return a value, specify the return type as `void`. A `void` function can omit the `return` statement, returning automatically when the body finishes execution, or it can use a bare `return;` to exit early:

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

This is particularly useful for functions with long return types or in template code. However, there is a constraint: all `return` statements must return the same type. If one path returns an `int` and another returns a `double`, the compiler will issue an error.

> ⚠️ **Warning**
> Forgetting a `return` statement in a non-`void` function is a classic bug. The compiler might issue a warning, but not necessarily an error—if the control flow reaches the end of the function without encountering a `return`, the behavior is **undefined behavior** (UB). The function might return a garbage value, or the program might crash entirely; it's purely luck of the draw. Therefore, we must build a good habit: ensure every non-`void` function has a `return` statement on all execution paths.

## Step 3 — Parameters and Arguments

Functions receive external data via **parameters**. The variables declared in the function signature are called formal parameters (parameters), while the specific values passed during the call are called actual parameters (arguments):

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

Functions can have any number of parameters, or none at all. When there are no parameters, we leave the parentheses empty (in C++, empty parentheses are equivalent to `void`: `int foo()` has the same meaning as `int foo(void)`).

For functions with multiple parameters, arguments and parameters correspond **by position**—the first argument is passed to the first parameter, the second to the second, and so on. C++ does not support named parameter calls like Python, so we must ensure the parameter order is aligned correctly:

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

The type of an argument must match the parameter, or be implicitly convertible. For example, if the parameter is a `double`, passing an `int` is valid (an implicit conversion occurs), but doing the reverse might result in precision loss. By default, parameters are **passed by value**—the function receives a copy of the argument, so modifying the copy does not affect the original data. We will discuss pass-by-reference and pass-by-pointer in detail in the next chapter.

## Step 4 — Local Scope and Lifetime

Variables declared inside a function body are called **local variables**. Their scope is limited to the inside of that function. In other words, variables are visible from the opening `{` to the closing `}`; outside this range, the variables no longer exist:

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

Local variables are stored on the **stack**. When a function is called, the system allocates space on the stack for its local variables; when the function returns, this space is reclaimed, and the variables are immediately destroyed. This process is automatic, so we do not need to manage it manually.

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

Even different code blocks within the same function can have variables with the same name. The inner block will **shadow** the variable in the outer block—however, in actual development, we do not recommend this because it hurts readability.

> ⚠️ **Warning**
> Returning a **reference** or **pointer** to a local variable is a serious error, and the compiler might not necessarily catch it for you. Local variables are destroyed after the function returns, so the memory referenced or pointed to becomes invalid—this is the classic "dangling reference" problem:
>
>

```cpp
> int& dangerous()
> {
>     int local = 42;
>     return local;  // 严重错误：返回局部变量的引用
> }   // local 在这里被销毁，引用指向的内存已无效
> ```

>
> The program might run perfectly fine during debugging, but suddenly crashes when compiled in Release mode or when the data volume increases. These intermittent bugs are much harder to track down than consistent crashes. The rule is simple: **never return a reference or a pointer to a local variable**. Returning by value is safe—it copies the data to the caller.

## Step 5 — A First Look at Function Overloading

C++ allows us to define multiple functions with the same name, provided their parameter lists are different (different number of parameters, or different parameter types). This is called **function overloading**:

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

The compiler automatically selects the best matching version based on the types of the arguments passed during the call—`add(3, 4)` calls the `int` version, while `add(3.5, 2.1)` calls the `double` version. This significantly improves code readability and consistency, as callers do not need to memorize a bunch of different names like `add_int` or `add_double`.

There are many details to the complete rules of function overloading, such as overload resolution priorities and ambiguity handling. We will discuss these in depth in later chapters. For now, it is enough to be aware of this concept.

## Practical Exercise — functions.cpp

We will integrate the concepts we have learned into a complete program, demonstrating function declarations, definitions, return value handling, and local scopes:

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

**Output:**

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

In this program, `factorial` is a **recursive function**—it calls itself within its own body. The idea behind recursion is to break down `n!` into `n * (n-1)!`, returning 1 directly when `n <= 1` as the termination condition. Recursion is a powerful programming technique, but it comes at a cost—every recursive call allocates space for new local variables on the stack. Think about it: if we call ourselves excessively, meaning the "recursion is too deep," we will cause a **stack overflow**! Therefore, in actual engineering, unless a loop is truly difficult to write and we are absolutely certain the nesting depth will remain shallow, we might consider recursion. Otherwise, it is strictly prohibited. At the very least, if I had dared to do this in my early days, I would have definitely been scolded. We will discuss the choice between recursion and iteration more deeply in later chapters.

One point worth noting is that the parameter type of the `print_result` function is `const std::string&` instead of `std::string`. Here, `&` indicates pass-by-reference, avoiding the overhead of copying the string, while `const` indicates that the function will not modify this string. Although the details of pass-by-reference will be formally explained in the next chapter, this pattern is extremely common in actual code, so just get used to seeing it for now.

## Run Online

Run the comprehensive function basics example online to observe function declarations, recursion, and parameter passing:

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

Write a function `bool is_prime(int n)` to determine whether a positive integer `n` is a prime number. Pay attention to edge cases: numbers less than two are not prime, while two is prime. Hint: we only need to check if there is any number between two and `sqrt(n)` that divides `n`.

```text
is_prime(2)  → true
is_prime(17) → true
is_prime(18) → false
is_prime(1)  → false
```

### Exercise 3: Returning Multiple Values with `struct`

A C++ function can only return one value, but we can bundle multiple values into a `struct` and return that instead. Define a `struct DivResult` containing a quotient and a remainder, then write a function `divmod` that returns both:

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

In this chapter, we learned the fundamental mechanisms of C++ functions from scratch. A function declaration tells the compiler "this function exists," while the definition provides the concrete implementation—the compiler must see either the declaration or the definition before we can use the function. The return type determines the type of value the function produces, and every execution path in a non-`void` function must have a `return` statement. Arguments are passed by position and correspond one-to-one, defaulting to value copies. The scope of local variables is limited to the function body, and they are automatically destroyed when the function returns—this is exactly why we must never return a reference or a pointer to a local variable.

Function overloading allows us to handle different argument types using the same name, with the compiler automatically selecting the most appropriate version. Additionally, we encountered recursion for the first time—a programming technique where a function calls itself—and demonstrated its basic usage with factorial calculation.

These concepts form the skeleton of functions. Next, we will dive into the details of parameter passing—the working mechanisms and appropriate use cases for pass-by-value, pass-by-reference, and pass-by-pointer. These are the factors that truly determine program performance and correctness.
