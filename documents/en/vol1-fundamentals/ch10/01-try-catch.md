---
title: "Exception Basics"
description: "Master the try/catch/throw syntax and the standard exception hierarchy"
chapter: 10
order: 1
difficulty: intermediate
reading_time_minutes: 14
platform: host
prerequisites:
  - "Template Specialization Basics"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/vol1-fundamentals/ch10/01-try-catch.md
  source_hash: a958e9ebcec867394f0a1e2f70046bc6ee1f82086a3371e4b118a2d0da93db9a
  translated_at: '2026-09-25T11:52:19+00:00'
  engine: anthropic
  token_count: 3800
---

# Exception Basics: There Is More to Error Reporting Than Return Values

Up to now, our error handling has basically come in two flavors: either signal failure with a return value (a function returning `-1` or `nullptr`), or just fire an `assert` and let the program blow up. Both approaches scrape by in small programs, but once a project grows, the cracks show: return-value error codes are easy for callers to ignore, and `assert` gets stripped out entirely by the compiler in Release builds. Even more annoying: when an error happens deep inside a nested call chain, we have to ferry the error code outward one layer at a time—every intermediate layer has to check it and deal with it, and the code quickly piles up with `if (error)` nested one inside another. (We've run into this thing so many times by now that it genuinely makes us want to throw up...)

C++'s exception mechanism exists precisely to solve this problem. It provides a **structured error propagation mechanism**: a function can throw an exception to report "something went wrong", and any caller along the call chain that is able to handle it can catch and process it—the functions in between neither need to know about it nor pass it along. In this chapter we start from the most basic `try`/`catch`/`throw` syntax, sort out the hierarchy of the standard exception classes, and finally write a complete hands-on program that strings all the knowledge points together.

## The throw, try, and catch Trio

The exception machinery has only three core keywords. `throw` raises an exception—the expression that follows it is the exception object, and it can be any copyable type. `try` marks a region of code where "something might go wrong". `catch` captures and handles exceptions thrown inside the `try` region. First, the shortest possible example:

```cpp
#include <iostream>
#include <stdexcept>

int main()
{
    try {
        throw std::runtime_error("Something went wrong");
    }
    catch (const std::runtime_error& e) {
        std::cout << "Caught: " << e.what() << "\n";
    }
    return 0;
}
```

The result of running it is `Caught: Something went wrong`. The `throw` creates a `std::runtime_error` object and throws it; execution in the `try` block stops immediately after the `throw`, and the program jumps to the matching `catch` block. `e.what()` returns the string passed in at construction. You might ask: why use `std::runtime_error` instead of just `throw 42` or `throw "oops"`? Technically you can (C++ allows throwing any type), but in real-world engineering, using the standard exception classes or custom exception classes is the better practice, because an exception object can carry rich error information, and the inheritance hierarchy enables hierarchical catching.

### Stack Unwinding: What Happens While the Exception Is in Flight

Once an exception is thrown, the program does not leap straight from `throw` to `catch`; in between, a very important process called **stack unwinding** takes place. Between the `throw` point and the nearest matching `catch`, every local object already constructed is destroyed in the **reverse order** of construction. This mechanism is the foundation that lets RAII guarantee resources never leak.

```cpp
#include <iostream>
#include <stdexcept>

struct Trace {
    const char* name_;
    explicit Trace(const char* n) : name_(n)
    { std::cout << "  Constructing: " << name_ << "\n"; }
    ~Trace()
    { std::cout << "  Destroying: " << name_ << "\n"; }
};

void inner()
{
    Trace t3("t3_in_inner");
    throw std::runtime_error("boom from inner");
}

void middle()
{
    Trace t2("t2_in_middle");
    inner();
}

int main()
{
    try {
        Trace t1("t1_in_main");
        middle();
    }
    catch (const std::exception& e) {
        std::cout << "  Caught: " << e.what() << "\n";
    }
    return 0;
}
```

Output:

```text
  Constructing: t1_in_main
  Constructing: t2_in_middle
  Constructing: t3_in_inner
  Destroying: t3_in_inner
  Destroying: t2_in_middle
  Destroying: t1_in_main
  Caught: boom from inner
```

`t3`, `t2`, `t1` are destroyed in the reverse order of construction—that is stack unwinding. The whole process requires no manual cleanup code from us; the language machinery guarantees everything.

During stack unwinding, if some destructor itself throws another exception (a new exception arises while an exception is already being handled), the program calls `std::terminate` outright—no recourse whatsoever. That is why destructors must **never** throw. Since C++11, all destructors are marked `noexcept` by default, but if we explicitly write `~MyClass() { throw ...; }` ourselves, the compiler will not stop us—the program just blows up at runtime. Keep this firmly in mind.

## The Standard Exception Hierarchy: The exception Family

The C++ standard library defines an exception class hierarchy rooted at `std::exception`. Getting familiar with this hierarchy pays off twice: we can pick the standard exception class that best expresses the error's semantics, and we can catch a whole family of exceptions through a base-class reference.

Let's look at `std::exception`: it is the base class of all standard exceptions and declares the virtual function `what()` that returns a `const char*` description. Its direct descendants split into two major branches. `std::logic_error` means "the program logic is wrong"—in theory detectable before the program even runs, such as an invalid argument being passed in; its subclasses include `std::invalid_argument` (illegal argument), `std::out_of_range` (index out of bounds), and `std::domain_error` (domain error; almost nobody uses it in practice). `std::runtime_error` means "a problem that only surfaces at runtime"—it can only appear once the program is actually running, such as a missing file or a network timeout; its subclasses include `std::overflow_error` and `std::underflow_error` (arithmetic overflow). In addition, `std::bad_alloc` inherits directly from `std::exception` and is thrown when `new` cannot allocate memory.

With this inheritance hierarchy, we can do **hierarchical catching**:

```cpp
#include <iostream>
#include <stdexcept>
#include <vector>

int main()
{
    try {
        std::vector<int> v = {1, 2, 3};
        std::cout << v.at(10) << "\n";  // at() throws out_of_range when out of bounds
    }
    catch (const std::out_of_range& e) {
        std::cout << "Out of range: " << e.what() << "\n";
    }
    catch (const std::logic_error& e) {
        std::cout << "Logic error: " << e.what() << "\n";
    }
    catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << "\n";
    }
    return 0;
}
```

Output:

```text
Out of range: vector::_M_range_check: __n (which is 10) >= this->size() (which is 3)
```

The matching rule for `catch` blocks is top-to-bottom: the first `catch` whose type matches gets executed, and the rest are skipped.

The order of the `catch` clauses matters—always put the most specific exception type first and the most general one last. If we put `catch (const std::exception&)` in the first position, every standard exception gets intercepted by it, and all the `catch` clauses after it become dead code. Worse, the compiler emits no warning whatsoever for this mistake; it only exposes itself at runtime.

## Throw by Value, Catch by const Reference

A best practice widely accepted in the C++ community: **throw by value, catch by const reference**. We throw by value because the value of the `throw` expression is copied (or moved) into a special storage area managed by the compiler; even if the original object is destroyed during stack unwinding, the exception object itself remains valid. Catching by `const` reference avoids **object slicing**: if we catch `std::exception` by value while what was actually thrown is a `std::runtime_error`, the derived part gets sliced off, and `what()` calls the base-class version instead of the derived-class version.

```cpp
// Wrong: catching by value slices
catch (std::exception e) {           // the runtime_error part is lost!
    std::cout << e.what() << "\n";   // the error message may be completely wrong
}

// Correct: catch by const reference
catch (const std::exception& e) {    // polymorphism fully preserved
    std::cout << e.what() << "\n";   // prints the original message correctly
}
```

The `const char*` pointer returned by `what()` points at a string stored inside the exception object; once the exception object is destroyed, that pointer dangles. So using `e.what()` inside the `catch` block is safe, but if we stash the return value and use it outside the `catch` block—good luck with that. The correct approach is to copy the content into a `std::string` inside the `catch` block.

## Multiple catch Blocks and Rethrowing

A `try` block can be followed by multiple `catch` blocks, each handling a different type of exception. Also, sometimes after a `catch` block catches an exception we find we cannot handle it, or we need to do some cleanup and then keep propagating it outward—that is when **rethrowing** comes in: a lone `throw;` (with no expression at all):

```cpp
#include <cstdio>
#include <iostream>
#include <stdexcept>

void wrapper()
{
    try {
        throw std::runtime_error("Runtime failure");
    }
    catch (const std::exception& e) {
        std::fprintf(stderr, "[wrapper] Logging: %s\n", e.what());
        throw;  // Rethrow the original exception, preserving full type information
    }
}

int main()
{
    try {
        wrapper();
    }
    catch (const std::runtime_error& e) {
        std::cout << "Caught: " << e.what() << "\n";
    }
    catch (...) {
        // Catch exceptions of all other types
        std::cout << "Caught unknown exception\n";
    }
    return 0;
}
```

Output:

```text
[wrapper] Logging: Runtime failure
Caught: Runtime failure
```

`throw;` and `throw e;` differ in essence: the former rethrows the **original exception object**, preserving its complete dynamic type information; the latter copies a brand-new exception object whose static type is the type of the `catch` parameter, and the derived-class information gets sliced off. So unless you genuinely intend to change the exception's type, always use `throw;`. `catch (...)` means "catch exceptions of any type"—it occasionally comes in handy in destructors or at library boundaries, but do not overuse it in day-to-day code: swallowing an exception without doing anything with it is the root of debugging nightmares.

## noexcept: A Promise Not to Throw

Starting with C++11 we have the `noexcept` keyword, used to declare that a function **does not throw exceptions**. This is not merely a comment for human readers: the compiler optimizes based on this promise (for example, omitting the bookkeeping code related to stack unwinding), and some standard library components also choose their implementation path depending on whether an operation is `noexcept`.

```cpp
int safe_computation(int a, int b) noexcept
{
    return a + b;  // pure computation, genuinely cannot throw
}
```

If a function marked `noexcept` really does throw from inside, the program immediately calls `std::terminate`—no stack unwinding, no chance for any `catch`, just instant death. So `noexcept` is not something to sprinkle on casually; we must be sure the function truly cannot throw, or that it swallows every possible exception internally with a `try-catch`. `noexcept` also accepts a boolean parameter: `noexcept(true)` is equivalent to `noexcept`, and `noexcept(false)` is equivalent to not writing it at all; the standard library's `std::swap` decides its own exception specification from the `noexcept` properties of the element type.

## Hands-On: exceptions.cpp

Now let's integrate the preceding knowledge points into one complete program that implements safe integer division and a file content parser.

```cpp
// exceptions.cpp
// A comprehensive demo of try/catch/throw, the standard exception hierarchy, and noexcept

#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

/// @brief Safe integer division; throws when the divisor is zero
int safe_divide(int dividend, int divisor)
{
    if (divisor == 0) {
        throw std::invalid_argument("Division by zero is not allowed");
    }
    return dividend / divisor;
}

/// @brief Parse the integer lines in a file
/// @throws std::runtime_error when the file cannot be opened
std::vector<int> parse_int_file(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    std::vector<int> result;
    std::string line;
    int line_num = 0;

    while (std::getline(file, line)) {
        ++line_num;
        try {
            std::size_t pos = 0;
            int value = std::stoi(line, &pos);
            if (pos != line.size()) {
                throw std::invalid_argument("Trailing characters");
            }
            result.push_back(value);
        }
        catch (const std::exception& e) {
            std::cerr << "[parse_int_file] Error at line "
                      << line_num << ": " << e.what() << "\n";
            throw;  // Rethrow and let the caller decide how to handle it
        }
    }
    return result;
}

/// @brief Format and print the parsed results (noexcept example)
void print_results(const std::vector<int>& values) noexcept
{
    std::cout << "Parsed " << values.size() << " values: ";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << values[i];
    }
    std::cout << "\n";
}

int main()
{
    // Safe division demo
    std::cout << "=== Safe Divide Demo ===\n";
    struct { int a, b; const char* label; } cases[] = {
        {10, 3, "normal"}, {7, 0, "zero"}, {-20, 4, "negative"},
    };
    for (const auto& tc : cases) {
        try {
            std::cout << "  " << tc.a << " / " << tc.b
                      << " = " << safe_divide(tc.a, tc.b) << "\n";
        }
        catch (const std::invalid_argument& e) {
            std::cout << "  " << tc.label << ": " << e.what() << "\n";
        }
    }

    // File parser demo
    std::cout << "\n=== File Parser Demo ===\n";
    const char* test_path = "/tmp/exception_test_data.txt";
    {
        std::ofstream out(test_path);
        out << "42\n100\nnot_a_number\n7\n";
    }
    try {
        auto values = parse_int_file(test_path);
        print_results(values);
    }
    catch (const std::exception& e) {
        std::cout << "  Caught: " << e.what() << "\n";
    }

    // Catch-all demo
    std::cout << "\n=== Catch-all Demo ===\n";
    try { throw 42; }
    catch (const std::exception&) { std::cout << "  Standard\n"; }
    catch (...) { std::cout << "  Unknown exception\n"; }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra exceptions.cpp -o exceptions && ./exceptions
```

Expected output:

```text
=== Safe Divide Demo ===
  10 / 3 = 3
  7 / 0 =   zero: Division by zero is not allowed
  -20 / 4 = -5

=== File Parser Demo ===
[parse_int_file] Error at line 3: stoi
  Caught: stoi

=== Catch-all Demo ===
  Unknown exception
```

Let's verify section by section. Safe division: `10 / 3` cleanly yields `3`; with `7 / 0`, `std::cout` has already printed `7 / 0 =` before `safe_divide` throws, so the error message follows that prefix; `-20 / 4` yields `-5`. File parsing: the third line of the test file, `"not_a_number"`, cannot be parsed by `std::stoi`; the `catch` block in `parse_int_file` prints the line-number context and rethrows with `throw;`, and main catches it—note that `print_results` is never called, because the exception breaks out of the parsing loop at line 3. The `catch(...)` part demonstrates the fallback catch for non-standard exception types. The exact `what()` message from `stoi` varies by compiler and standard library version (libstdc++, for example, may print `stoi` or `stoi: no conversion`).

`std::stoi` throws `std::invalid_argument` (no conversion possible) or `std::out_of_range` (value outside the range of `int`) when parsing fails. Both of these inherit from `std::logic_error`. If we need to distinguish the two cases inside a `catch` block, we should use two separate `catch` handlers rather than uniformly swallowing everything with `catch (const std::exception&)`—the latter loses the concrete type information of the error and makes debugging harder.

## Practice Time

### Exercise 1: Safe Array Access

Write a function `int safe_get(const std::vector<int>& v, std::size_t index)` that throws `std::out_of_range` when `index` is out of bounds, with an error message containing the requested index and the vector's actual size. Test both a normal access and an out-of-bounds access in `main`.

### Exercise 2: A String-to-Number Parser

Write a function `std::vector<double> parse_doubles(const std::string& input)` that parses a comma-separated string (such as `"1.5,2.7,3.14"`) into a vector of `double`s. Requirements: report invalid number formats with `std::invalid_argument`, and report empty input with `std::runtime_error`. On the calling side, handle the two kinds of exceptions with separate `try`/`catch` handlers and print friendly messages.

### Exercise 3: The noexcept Operator

Write two functions: `void safe_calc(int x) noexcept` that does a simple computation, and `void risky_calc(int x)` that throws `std::invalid_argument` when `x` is negative. Then in `main`, use the two compile-time operators `noexcept(safe_calc)` and `noexcept(risky_calc)` to check their `noexcept` status and print the results.
