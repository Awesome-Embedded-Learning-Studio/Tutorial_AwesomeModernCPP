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
- Range-based for Loops
reading_time_minutes: 15
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Function Basics
translation:
  source: documents/vol1-fundamentals/ch03/01-function-basics.md
  source_hash: 22e406eb74c37874a915fe613d166836b5d9a740f8f3a9bd728f07b66c7dce7f
  translated_at: '2026-09-25T10:29:01+00:00'
  engine: anthropic
  token_count: 4000
---
# Function Basics: Don't Pile Ten Thousand Lines into One main

Kids, I have genuinely seen this: someone writes a ten-thousand-line program that, from beginning to end, contains a single function — `main()` — with all the code piled together like spaghetti. Clearly, this person doesn't really get functions (complete beginners excepted).

What's it like to read that? Variables scattered everywhere, logic tangled into knots, and changing one feature means reading the whole file, terrified that pulling one thread drags the whole thing down. Never mind showing this code to other people — give it a week and even you won't understand it. As the joke goes, at this point only God can still make sense of it. (Though give it another week, and perhaps even God gives up.)

Functions are the core tool for solving this problem. They let us wrap a stretch of code that accomplishes a specific task into a named unit; when we need it, we just call it by name, without caring about the internal implementation details.

## Declaration and Definition

Before writing a function, we need to get two concepts straight: **declaration** and **definition**. A declaration tells the compiler "a function like this exists" — it gives only the function name, return type, and parameter list, with no function body. A definition supplies the complete implementation.

```cpp
// Declaration (also called a function prototype)
int add(int a, int b);

// Definition (includes the function body)
int add(int a, int b)
{
    return a + b;
}
```

The semicolon at the end of a declaration stands in for the function body. Once the compiler has seen the declaration, it knows `add` is a function that takes two `int` parameters and returns an `int`. How it's implemented inside doesn't concern the compiler for the moment — we just have to make sure the linker can find the real definition later.

So why distinguish the two? Because the C++ compiler works through the file line by line, top to bottom. If we call `add()` inside `main()` but the definition of `add` sits after `main`, the compiler doesn't yet know what `add` is while it's processing `main`, and errors out on the spot. **The fix is to place a declaration at the top of the file, so the compiler knows the function exists ahead of time:**

```cpp
#include <iostream>

// Declare first, telling the compiler these functions exist
int add(int a, int b);
int multiply(int a, int b);

int main()
{
    std::cout << add(3, 4) << std::endl;       // the compiler knows add's signature
    std::cout << multiply(3, 4) << std::endl;  // the compiler knows multiply's signature
    return 0;
}

// Definitions come afterwards — perfectly fine
int add(int a, int b)
{
    return a + b;
}

int multiply(int a, int b)
{
    return a * b;
}
```

> Wait — so what about a whole pile of functions? Do we write every declaration at the top? The answer is no need: we have `#include`. Hold on — let's gather all the declarations together and toss them into a file. From then on, no more manually copying function declarations back and forth everywhere! How nice.
>
> Congratulations — you've just invented the header file, hahahahaha!
>
> Jokes aside, we do have to say: this "declare first, define later" pattern matters enormously in real projects — as we'll see when we get to header files later, declarations usually live in a `.h` file shared by multiple source files, while definitions live in `.cpp` files. For now, just remember one principle: **before using a function, the compiler must have seen its declaration (or definition)**.

Forgetting the declaration while placing the definition after the call site is one of the compile errors newcomers hit most often. The message typically reads `error: use of undeclared identifier 'xxx'`. When we see this, our first reaction should be to check where the function definition sits: either move the definition above the call site, or add a declaration at the top of the file.

## Return Types and the return Statement

Every C++ function has a return type, written before the function name; it tells the compiler what type of value the function produces when it finishes running. We use the `return` statement to send a value back to the caller, and it ends the function's execution at the same time.

```cpp
int max(int a, int b)
{
    if (a > b) {
        return a;   // returns a; the function ends immediately
    }
    return b;       // returns b
}
```

A function can have multiple `return` statements, but any single call executes only one of them: once a `return` runs, everything after it is skipped. Both paths through this `max` are guaranteed to `return`; if we hold our functions to that standard while writing them, no path gets missed.

If a function doesn't need to return anything, we write its return type as `void`. A `void` function may omit the `return` — when the body finishes, it returns on its own — or write a bare `return;` to exit early:

```cpp
void print_greeting(const std::string& name)
{
    if (name.empty()) {
        return;  // exit early, print nothing
    }
    std::cout << "Hello, " << name << "!" << std::endl;
}
```

C++14 introduced a genuinely practical feature: **return type deduction**. Write `auto` where the return type would go, and the compiler deduces the return type from the `return` statement:

```cpp
auto add(int a, int b)
{
    return a + b;  // the compiler deduces the return type as int
}
```

This is especially convenient when the return type is long to write, or for functions inside template code. One restriction to remember, though: all `return` statements must return the same type. If one path returns `int` and another returns `double`, the compiler reports an error.

Forgetting the `return` in a non-`void` function is a classic bug. The compiler may warn about it, but it won't necessarily error — if control flow reaches the end of the function without meeting a `return`, the behavior is **undefined** (undefined behavior). The function might return a garbage value, or the program might simply crash — it all comes down to luck. So build the habit: every execution path of every non-`void` function must contain a `return`.

## Parameters and Arguments

Functions receive data from the outside through **parameters**. The variables we declare in the function signature are the formal parameters (parameters, for short), and the concrete values passed in at the call site are the actual arguments (arguments, for short):

```cpp
//          parameters
//            ↓    ↓
int add(int a, int b)
{
    return a + b;
}

int main()
{
    //       arguments
    //        ↓    ↓
    int result = add(3, 4);  // a receives 3, b receives 4
    return 0;
}
```

A function can take any number of parameters, or none at all. When there are none, we simply leave the parentheses empty (in C++, empty parentheses are equivalent to `void`: `int foo()` means the same thing as `int foo(void)`).

In a multi-parameter function, arguments pair up with parameters **by position**: the first argument goes to the first parameter, the second to the second, and so on. C++ does not support named-argument calls the way Python does, so the order we pass arguments in must line up:

```cpp
void print_info(const std::string& name, int age, double height)
{
    std::cout << name << ", " << age << " 岁, "
              << height << " cm" << std::endl;
}

int main()
{
    // Passed by position; don't get the order wrong
    print_info("Alice", 20, 165.5);
    return 0;
}
```

An argument's type must match the parameter's type, or be implicitly convertible to it. For example, if the parameter is `double`, passing an `int` is legal (an implicit conversion happens), but going the other way may lose precision. By default, arguments are **passed by value**: inside the function we hold a copy of the argument, and modifying the copy leaves the original data untouched. Pass by reference and pass by pointer — we'll discuss those in detail in the next article.

## Local Scope and Lifetime

Variables declared inside a function body are called **local variables**; their scope is limited to that function. In other words, we can use them from the opening `{` all the way to the closing `}`; outside that range, the variable no longer exists:

```cpp
int compute(int x)
{
    int result = x * 2;  // result is a local variable
    return result;
}   // result is destroyed here

int main()
{
    int r = compute(5);
    // std::cout << result;  // compile error! result is not in scope
    return 0;
}
```

Local variables live on the **stack**. When a function is called, the system allocates space on the stack for its local variables; when the function returns, that space is reclaimed and the variables are destroyed on the spot. The whole process is automatic — nothing for us to manage by hand.

We can use same-named variables in different functions and they won't interfere with each other, because each lives in its own independent scope:

```cpp
void func_a()
{
    int value = 10;  // func_a's value
    std::cout << "func_a: " << value << std::endl;
}

void func_b()
{
    int value = 20;  // func_b's value; completely unrelated to func_a's
    std::cout << "func_b: " << value << std::endl;
}
```

Even different blocks inside the same function can hold same-named variables; the inner block **shadows** the outer block's variable — though in real-world development we advise against doing this; the readability cost is just too high.

Returning a **reference** or **pointer** to a local variable is a serious error, and one the compiler can't necessarily catch for us. The local variable is destroyed once the function returns, so the memory the reference or pointer refers to is already invalid — this is the classic "dangling reference" problem:

```cpp
int& dangerous()
{
    int local = 42;
    return local;  // serious error: returning a reference to a local variable
}   // local is destroyed here; the memory the reference points to is invalid
```

The program may run perfectly while we're debugging, then suddenly crash once compiled in Release or once the data volume grows. These "sometimes fine, sometimes broken" bugs are harder to track down than crashes that reproduce every time. The rule is simple: **never return a reference or pointer to a local variable**. Returning by value is safe — it copies the result and hands it to the caller.

## A First Look at Function Overloading

C++ lets us define multiple functions with the same name, as long as their parameter lists differ (a different number of parameters, or different parameter types). This is called **function overloading**:

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

From the argument types supplied at the call site, the compiler automatically selects the best-matching version: `add(3, 4)` calls the `int` version, `add(3.5, 2.1)` calls the `double` version. This does a lot for readability and consistency — we don't have to memorize a pile of distinct names like `add_int` and `add_double`.

The full rules of function overloading carry plenty of detail — overload resolution priorities, ambiguity handling, and so on — which later chapters will dig into. For now, knowing that this mechanism exists is enough.

## Hands-On Practice — functions.cpp

Let's fold the points from earlier into one complete program, with demonstrations of function declarations, definitions, return-value handling, local scope, and the other concepts:

```cpp
// functions.cpp
// Platform: host
// Standard: C++17

#include <iostream>
#include <string>

// Function declarations (prototypes)
int add(int a, int b);
int max_of(int a, int b);
int factorial(int n);
bool is_even(int n);
void print_result(const std::string& label, int value);

// The main function — the program's entry point
int main()
{
    // Addition
    int sum = add(15, 27);
    print_result("15 + 27", sum);

    // Take the larger value
    int bigger = max_of(42, 17);
    print_result("max(42, 17)", bigger);

    // Factorial
    int fact = factorial(6);
    print_result("6!", fact);

    // Even/odd check
    int test_values[] = {0, 1, 2, 7, 10};
    for (int val : test_values) {
        std::cout << val << " 是"
                  << (is_even(val) ? "偶数" : "奇数")
                  << std::endl;
    }

    return 0;
}

// ---- Function definitions ----

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

/// @brief Computes the factorial of n (n!)
/// @param n A non-negative integer
/// @return The factorial of n
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

In this program, `factorial` is a **recursive function** — inside its own body, it calls itself. The idea of recursion is to break `n!` down into `n * (n-1)!`, until `n <= 1`, at which point it returns 1 directly as the termination condition. Recursion is pleasant to use, but it comes at a price: every recursive call allocates fresh space on the stack for its local variables. Think about it — if a function frantically calls itself and the recursion goes too deep, the stack overflows. So in real engineering, recursion only enters consideration when a loop is genuinely painful to write and we can absolutely guarantee it never nests too deeply; otherwise it's banned outright. At least back in my early working days, pulling a stunt like that would have gotten me scolded for sure. Later chapters will discuss the choice between recursion and iteration in more depth.

`print_result`'s parameter type is `const std::string&` rather than `std::string` — let's pause here for a second: the `&` means pass by reference, which avoids the cost of copying the string; the `const` means the function won't modify the string internally. The details of reference passing won't be formally covered until the next article, but this pattern shows up everywhere in real code — for now, just get familiar with the sight of it.

## Run It Online

You can also run this comprehensive example online and observe function declarations, recursion, and parameter passing:

<OnlineCompilerDemo
  title="Function Basics Comprehensive Drill: Declarations, Recursive Factorial, Even/Odd Check"
  source-path="code/examples/vol1/08_function_basics.cpp"
  description="Run it online and watch the actual behavior of function declarations, definitions, recursion, and several parameter-passing styles."
  allow-run
/>

## Try It Yourself

### Exercise 1: Greatest Common Divisor

Write a function `int gcd(int a, int b)` that computes the greatest common divisor of two positive integers using the Euclidean algorithm. The algorithm is simple: if `b` is 0, return `a`; otherwise recursively call `gcd(b, a % b)`.

```text
gcd(48, 18)  → 6
gcd(100, 75) → 25
gcd(7, 3)    → 1
```

::: details Reference Solution

```cpp
#include <iostream>

int gcd(int a, int b);
int main()
{
    int a = 0;
    int b = 0;
    int measure = 0;
    std::cout << "请输入两个正整数: (例如48 18)";

    if (!(std::cin >> a >> b))
    {
        std::cout << "错误：输入格式无效" << std::endl;
        return 1;
    }
    if (a < b)
    {
        int temp = 0;
        temp = a;
        a = b;
        b = temp;
    }
    measure = gcd(a, b);
    std::cout << a << "和" << b << "的最大公约数为: " << measure << std::endl;
    return 0;
}
int gcd(int a, int b)
{
    if (b == 0)
    {
        return a;
    }
    return gcd(b, a % b);
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
请输入两个正整数: (例如48 18)18 48
48和18的最大公约数为: 6
```

:::

### Exercise 2: Prime Checking

Write a function `bool is_prime(int n)` that determines whether a positive integer `n` is prime. Mind the edge cases: numbers less than 2 are not prime, and 2 is prime. Hint: you only need to check whether any number from 2 up to `sqrt(n)` divides `n` evenly.

```text
is_prime(2)  → true
is_prime(17) → true
is_prime(18) → false
is_prime(1)  → false
```

::: details Reference Solution

```cpp
#include <iostream>
#include <cmath>

bool is_prime(int a);
int main()
{
    int a = 0;
    std::cout << "请输入一个正整数: ";

    if (!(std::cin >> a))
    {
        std::cout << "错误：输入格式无效" << std::endl;
        return 1;
    }
    // std::boolalpha makes bool print as text
    std::cout << std::boolalpha << "is_prime(" << a << ") -> " << is_prime(a) << std::endl;
    return 0;
}
bool is_prime(int a)
{
    if (a < 2)
    {
        return false;
    }

    const int limit = static_cast<int>(std::sqrt(a));
    for (int i = 2; i <= limit; ++i)
    {
        if (a % i == 0)
        {
            return false;
        }
    }
    return true;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
请输入一个正整数: 5
is_prime(5) -> true
```

:::

### Exercise 3: Returning Multiple Values with struct

A C++ function can return only one value, but we can pack several values into a `struct` and return that. Define a `struct DivResult` holding the quotient and the remainder, then write a function `divmod` that returns both:

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

::: details Reference Solution

```cpp
#include <iostream>
#include <cmath>

struct DivResult
{
    int quotient;
    int remainder;
};
DivResult divmod(int dividend, int divisor);
int main()
{
    int dividend = 0;
    int divisor = 0;
    std::cout << "请输入被除数和除数(例如17 5): ";

    if (!(std::cin >> dividend >> divisor))
    {
        std::cout << "错误：输入格式无效" << std::endl;
        return 1;
    }
    if (divisor == 0)
    {
        std::cout << "除数不能为零" << std::endl;
        return 1;
    }

    const DivResult value = divmod(dividend, divisor);
    std::cout << "divmod(" << dividend << ", " << divisor << ") -> 商: "
              << value.quotient << ", 余: " << value.remainder << std::endl;

    return 0;
}
DivResult divmod(int dividend, int divisor)
{

    return {dividend / divisor, dividend % divisor};
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
请输入被除数和除数(例如17 5): 17 5
divmod(17, 5) -> 商: 3, 余: 2
```

:::
