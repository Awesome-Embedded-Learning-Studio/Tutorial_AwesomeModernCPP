---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the rules of function overloading and the usage of default parameters,
  understand the overload resolution mechanism, and avoid common conflicts between
  the two.
difficulty: beginner
order: 3
platform: host
prerequisites:
- 参数传递方式
reading_time_minutes: 14
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Overloading and Default Parameters
translation:
  source: documents/vol1-fundamentals/ch03/03-overloading-default.md
  source_hash: ff6bb794202ea32a2572fa68f8fb1aa89728a5fd604812ce30e17a4a115d5b17
  translated_at: '2026-06-16T03:42:23.076112+00:00'
  engine: anthropic
  token_count: 2145
---
# Overloading and Default Parameters

In the previous chapter, we clarified the various methods of parameter passing—pass by value, pass by pointer, and pass by reference. Now, a new question arises: suppose we want to write a `print` function to print integers, floating-point numbers, and strings. These three tasks are essentially "printing," but the rules of C dictate that every function must have a unique name. So, you would have to write `print_int`, `print_float`, `print_str`—coming up with names is exhausting enough, and you still have to figure out which one to call.

C++ says: the same concept does not need different names. **Function overloading** allows functions with the same name to exhibit different behaviors based on their arguments, while **default parameters** make those arguments that are "almost always passed the same value" completely transparent. These two features are essential skills for designing good interfaces, and in this chapter, we will master them completely.

## Step 1 — Understanding Function Overloading

The core rule of function overloading is very simple: multiple functions can share the same name as long as their **parameter lists** differ—either in the types of parameters or in the number of parameters. Note that the return type is not a factor—the compiler will not distinguish overloads based solely on return type. This confuses many beginners who think "returning `int` versus returning `float` should count as different functions," but it really doesn't, because the call site might completely ignore the return value, and the compiler cannot see the return type in that context.

Let's look at the most basic example:

```cpp
void print(int value)    { std::cout << "Int: " << value << '\n'; }
void print(float value)  { std::cout << "Float: " << value << '\n'; }
void print(const char* value) { std::cout << "Str: " << value << '\n'; }
```

When calling, the compiler automatically selects the corresponding version based on the type of the actual argument:

```cpp
print(42);        // Calls print(int)
print(3.14f);     // Calls print(float)
print("Hello");   // Calls print(const char*)
```

To achieve the same effect in C, you would need three functions with three names, and every time you called them, you would have to decide which one to use. In contrast, the advantage of overloading in API design is obvious—the caller only needs to remember one name.

Differences in the number of parameters can also constitute overloading. This pattern is very common in real-world engineering—peripheral initialization functions often need to provide both a "recommended configuration" and a "fully customizable" entry point:

```cpp
// Use default clock configuration
void UART_Init(uint32_t baudrate);

// Fully custom configuration
void UART_Init(uint32_t baudrate, uint32_t clock_src, uint32_t stop_bits);
```

## Step 2 — Understanding Overload Resolution

On the surface, calling an overloaded function seems as simple as "writing a name and passing an argument." But in reality, the compiler executes a very strict decision-making process behind the scenes—**overload resolution**. Whenever a function with multiple overloaded versions is called, the compiler collects all candidate functions with matching names and evaluates them one by one: **which one is the "best fit"?** It is important to emphasize that the compiler does not understand your business semantics; it mechanically scores according to language rules to select the version with the highest match.

In the absence of templates, the compiler's judgment criteria can be understood as a "match priority chain" from strong to weak. At the top is **exact match**—the type of the actual argument exactly matches the formal parameter; if no exact match is found, **promotion** is considered, such as `float` to `double` or `char` to `int`; further down is **standard conversion**, such as `int` to `float`; finally, user-defined conversions are considered. This order is critical—as long as a feasible match is found at a certain level, the subsequent rules will not be considered at all.

Let's use the most common example to demonstrate. Suppose both `void print(int)` and `void print(double)` are defined:

```cpp
void print(int value)    { std::cout << "Int: " << value << '\n'; }
void print(double value) { std::cout << "Double: " << value << '\n'; }

print(10);     // Which one?
print(10.0);   // Which one?
print(10.5f);  // Which one?
```

When calling `print(10)`, the literal `10` is itself an `int`, which is an exact match for `print(int)`, while `print(double)` requires a conversion from `int` to `double`. An exact match has overwhelming dominance over any form of conversion, so `print(int)` will ultimately be called. Conversely, in `print(10.0)`, `10.0` is a `double`, so the exact match occurs on `print(double)`.

A slightly more confusing situation is `print(10.5f)`. The type of `10.5f` is `float`, and we do not have a `print(float)` overload. At this point, the compiler compares two possible paths: promoting `float` to `double`, or converting `float` to `int`. The former is a standard promotion between floating-point types, considered more natural and safe; the latter involves truncation semantics and has a lower priority. Therefore, `print(double)` will still be called. This also reflects a fact: **overload resolution is not "least character matching," but "most reasonable type path matching."**

The real headache often arises when the rules cannot determine a winner. For example, if both `void print(int, double)` and `void print(double, int)` exist, when you call `print(10, 10.5)`, the matching cost for both candidate functions is exactly the same—for the first version, one parameter is an exact match and the other requires a standard conversion; for the second version, the situation is exactly symmetrical. The compiler will not try to guess your intent; it will directly determine that the call is ambiguous and terminate with a compilation error.

> ⚠️ **Warning**
> Overload ambiguity is not always as obvious as the example above. When you define multiple overloaded versions and implicit conversion relationships exist between parameters (such as `int` and `double`, `float` and `int`), ambiguity may pop up in unexpected places. The most reliable approach is: **when designing interfaces, avoid distinguishing overloads solely by parameter order or subtle type differences**. Once ambiguity occurs, make the types explicit, or simply use different function names.

Behind this lies a very important design philosophy of C++: as long as there are equally feasible choices that cannot be compared for superiority, the compiler would rather refuse to compile than make a decision for the programmer. This is also the underlying tone of C++'s strong type system—clarity always trumps convenience.

## Step 3 — Mastering Default Parameters

In real-world engineering, "the more parameters, the better" is not true for functions. Often, a function's parameters will always include a mix of roles: core mandatory parameters that differ with every call; high-frequency configurations that are almost always fixed; and advanced options that are adjusted only in rare scenarios. If every call is forced to write out every parameter without omission, the code is not only verbose but also quickly obscures the truly important information.

Default parameters exist precisely to solve this problem—**for those parameters for which you have already decided on "default behavior," just don't make the caller worry about them**.

```cpp
// baudrate: mandatory, others have defaults
void UART_Init(uint32_t baudrate,
               uint32_t timeout = 1000,
               bool parity_check = false);
```

The most common calling form retains only the one parameter you truly care about:

```cpp
UART_Init(115200);  // Uses default timeout (1000) and parity (false)
```

From an interface design perspective, this is a very gentle means of forward compatibility: you can continuously append new optional capabilities to the right side of the function without breaking existing code.

The syntax of default parameters seems simple, but the rules are actually very strict, and many people fall into traps.

**Rule 1: Default parameters must appear continuously from right to left.** When processing a function call, the compiler can only determine which values use defaults by "omitting trailing parameters." You cannot skip intermediate parameters—if you want to pass a value to the third parameter, all preceding parameters must be explicitly given. Therefore, the order of parameters is crucial when designing function signatures: **put the parameters that most often need customization on the left, and the parameters that almost never change on the right**.

```cpp
// Correct: defaults are on the right
void LED_Set(bool state, int brightness = 100);

// Error: 'brightness' has a default but 'state' does not
// void LED_Set(int brightness = 100, bool state);
```

**Rule 2: Default parameters can only be specified once, and should be placed in the declaration.** This is particularly important in projects where header files and source files are separated. The default value is part of the interface, not an implementation detail—if you write default parameters again in the `.cpp` file, the compiler will think you are trying to redefine the rule and will directly report an error.

```cpp
// Header (.h) - Specify defaults here
void UART_Init(uint32_t baudrate, uint32_t timeout = 1000);

// Source (.cpp) - Do NOT specify defaults here
void UART_Init(uint32_t baudrate, uint32_t timeout) {
    // Implementation...
}
```

> ⚠️ **Warning**
> Writing default values in the declaration and then writing them again in the definition—this error is very common among beginners, and the error message is sometimes not very intuitive, making it quite difficult to locate. Remember: **write default parameters in the declaration, not in the definition**.

## Step 4 — Overloading or Default Parameters, How to Choose

Both function overloading and default parameters can make interfaces more flexible, but their applicable scenarios do not completely overlap. The choice of which one to use depends on the specific problem you face.

When you need to **handle parameters of different types**, function overloading is the only choice—default parameters cannot do this. `print(int)` and `print(const char*)` have completely different parameter types and behaviors; this can only be achieved through overloading.

When you need to **reduce the number of parameters and provide default behavior**, default parameters are the more concise choice. `UART_Init(baud)` and `UART_Init(baud, timeout)` do the same thing, just with different levels of detail; using default parameters is the most natural approach.

But the situation that requires the most vigilance is **mixing the two**. If function overloading and default parameters are designed poorly, they can produce very tricky ambiguity problems. Look at this classic negative example:

```cpp
void LED_Set(bool state);              // Version 1
void LED_Set(bool state, int brightness = 100); // Version 2

LED_Set(true); // Ambiguous! Matches both Version 1 and Version 2
```

When the compiler faces `LED_Set(true)`, it finds that both versions can match—the first is an exact match, and the second is also an exact match (only the second parameter uses a default value). The cost is identical on both sides, the compiler cannot make a choice, and it directly reports an ambiguity error.

> ⚠️ **Warning**
> Overloading and default parameters overlapping on the same interface is an almost guaranteed problem. My suggestion is: for the same function name, either use only overloading (multiple versions with different parameter types) or use only default parameters (one version with some parameters having default values), but do not mix the two. If you really need to support both "different types" and "different numbers of parameters," consider encapsulating the logic for different types into different function names—although this looks less "elegant" than overloading, it at least avoids ambiguity.

## Hands-on Practice — overload.cpp

Let's integrate the previous usage into a complete program to demonstrate multiple `print` overloads, the practical application of default parameters, and a deliberately created ambiguity error and its fix:

```cpp
#include <iostream>
#include <string>

// 1. Basic Overloading: Handling different types
void print(int value) {
    std::cout << "[Int] " << value << '\n';
}

void print(double value) {
    std::cout << "[Double] " << value << '\n';
}

void print(const std::string& value) {
    std::cout << "[String] " << value << '\n';
}

// 2. Default Parameters: Handling optional arguments
// Design principle: put frequently changed args on the left
void log_message(const std::string& msg,
                 int level = 0,      // 0: Info
                 bool timestamp = false) {
    if (timestamp) std::cout << "[Time] ";
    std::cout << "[Level " << level << "] " << msg << '\n';
}

// 3. Ambiguity Demonstration (Commented out to prevent compilation error)
// void display(int i) { std::cout << "Int: " << i << '\n'; }
// void display(int i, double d = 0.0) { std::cout << "Int, Double: " << i << ", " << d << '\n'; }
// display(42); // Error: ambiguous

int main() {
    // --- Test Overloading ---
    std::cout << "=== Function Overloading ===" << '\n';
    print(42);          // Matches print(int)
    print(3.14159);     // Matches print(double)
    print("Hello C++"); // Matches print(string) - const char* converted to string

    // --- Test Default Parameters ---
    std::cout << "\n=== Default Parameters ===" << '\n';

    // Use all defaults
    log_message("System started");

    // Override level, use default timestamp
    log_message("Warning detected", 2);

    // Override all
    log_message("Critical failure", 3, true);

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 overload.cpp -o overload && ./overload
```

Output:

```text
=== Function Overloading ===
[Int] 42
[Double] 3.14159
[String] Hello C++

=== Default Parameters ===
[Level 0] System started
[Level 2] Warning detected
[Time] [Level 3] Critical failure
```

If you define both `display(int)` and `display(int, double d = 0.0)` from the ambiguity example above and call `display(42)`, the compiler will directly report an error:

```text
error: call of 'display' is ambiguous
```

The solution is what we demonstrated—split the two versions into different function names, or remove one of the overloads and use default parameters instead (keeping only one version), so that the semantics at the call site are no longer ambiguous.

## Run Online

Run the comprehensive example of function overloading and default parameters online:

<OnlineCompilerDemo
  title="Function Overloading and Default Parameters"
  source-path="code/examples/vol1/11_overloading_default.cpp"
  description="Run online and observe the type matching of function overloading and the filling behavior of default parameters."
  allow-run
/>

## Try It Yourself

### Exercise 1: The `max` Overload Family

Write a set of overloaded functions `max`, accepting two `int`s, two `double`s, and two `const char*`s (compare lexicographically and return the pointer to the larger one). Call them in `main` and print the results.

```cpp
// TODO: Implement max(int, int), max(double, double), max(const char*, const char*)
int main() {
    // TODO: Test your overloads
}
```

### Exercise 2: Log Function with Default Parameters

Write a `log` function with the signature `void log(const std::string& msg, int level = 0, bool verbose = false)`. Call it with different combinations of arguments and observe the behavior of default parameters.

### Exercise 3: Compilable or Ambiguous?

Can the following code compile? If so, which `func` will be called? Think it through before verifying on the machine:

```cpp
void func(long l) { std::cout << "long\n"; }
void func(double d) { std::cout << "double\n"; }

int main() {
    func(3.14f); // float literal
}
```

Hint: The type of `3.14f` is `float`. What conversion levels do `float` -> `long` and `float` -> `double` belong to? Do integral promotion and integral conversion have the same priority in overload resolution?

## Summary

In this chapter, we learned two important tools for function interface design in C++. Function overloading allows functions with the same name to exhibit different behaviors based on differences in parameter types and counts. The compiler decides which version to call through a strict set of overload resolution rules—exact match takes precedence over promotion, promotion takes precedence over standard conversion, and when two candidate functions are evenly matched, the compiler directly reports an ambiguity error. Default parameters allow callers to omit trailing parameters that are "almost always the same value"; the rule is that defaults must appear continuously from right to left and are specified only once in the declaration. Each has its domain of expertise—overloading handles "different types," default parameters handle "optional parameters"—but mixing them can easily produce ambiguity and requires extreme caution.

In the next chapter, we will look at `inline` and `constexpr` functions—when the overhead of a function call itself becomes a problem, what means does C++ give us to eliminate it.
