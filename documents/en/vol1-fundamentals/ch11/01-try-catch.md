---
title: "Exception Basics"
description: "Master the try/catch/throw syntax and the standard exception hierarchy"
chapter: 11
order: 1
difficulty: intermediate
reading_time_minutes: 14
platform: host
prerequisites:
  - "Common STL Patterns"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/vol1-fundamentals/ch11/01-try-catch.md
  source_hash: bc67c23671aa8cc6c9ae651e5c3a5ed2e28f00abca12e37d7eada4f565ca25a9
  translated_at: '2026-09-27T04:03:53+00:00'
  engine: anthropic
  token_count: 7700
---

# Exception Basics: There Is More Than One Way to Report an Error

So far, the ways we've had for handling errors essentially come down to two, both inherited from C: either **use a return value to signal failure** (a function returns `-1` or `nullptr`), or just fire an `assert` and let the program blow up. These two approaches scrape by in small programs, but once the project scales up, the problems surface: return-code errors are easy for callers to ignore, and `assert` is stripped out entirely by the compiler in Release builds. What's more troublesome is that when an error occurs deep inside a nested call chain, **we have to ferry the error code outward one layer at a time, checking and handling it at every level, and the code quickly piles up layer-upon-layer `if (error)`**. (We've seen this thing so many times already, I'm honestly about to throw up...)

C++'s exception mechanism was born to solve exactly this problem. It provides a **structured error propagation mechanism**: a function can throw an exception directly to report that something went wrong, and any caller along the chain that is in a position to handle it can catch and process it—the functions in between neither need to know about it nor pass it along.

Sounds decent enough—let's go!

## The throw, try, catch Trio

The exception mechanism's core operations involve just three keywords. `throw` is responsible for throwing an exception—the expression after it is the exception object, and it can be any copyable type. `try` marks a region of code that "might go wrong". `catch` is responsible for catching and handling exceptions thrown inside the `try` region. Let's look at the shortest possible example first:

```cpp
#include <iostream>
#include <stdexcept>

int main()
{
    try {
        // ... you wrote a long stretch of code; at runtime—bam, oh dear, it blew up
        throw std::runtime_error("Something went wrong"); // Oops. Dropped an error. Something went wrong!
    }
    catch (const std::runtime_error& e) {
        std::cout << "Caught: " << e.what() << "\n";
    }
    return 0;
}
```

The result of running it is `Caught: Something went wrong`. The `throw` creates a `std::runtime_error` object and throws it; the program immediately abandons execution of the rest of the `try` block after the `throw` and jumps to the matching `catch` block. `e.what()` returns the string passed in at construction. You might ask: why use `std::runtime_error` instead of just `throw 42` or `throw "oops"`? Technically that works (C++ allows throwing any type), but in real engineering, using standard exception classes or custom exception classes is the better practice, because exception objects can carry rich error information, and the inheritance hierarchy can be leveraged for hierarchical catching.

### Stack Unwinding—What Happens as the Exception Flies By

Watch what happens after an exception is thrown: the program doesn't jump straight from `throw` to `catch`—a very important process happens in between, called **stack unwinding**. Between the `throw` point and the nearest matching `catch`, every local object that has already been constructed is destroyed in **reverse order** of construction. This mechanism is the foundation that lets RAII guarantee no resource leaks.

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

`t3`, `t2`, and `t1` are destroyed in reverse order of construction—that is stack unwinding. The whole process requires no manual cleanup code from us; the language mechanism takes care of everything.

During stack unwinding, if some destructor itself throws another exception (a new exception arising while an exception is already being handled), the program calls `std::terminate` on the spot—no recourse whatsoever. That's why destructors must **never** throw. Since C++11, all destructors are marked `noexcept` by default, but if we explicitly write `~MyClass() { throw ...; }` ourselves, the compiler won't stop us—it just blows up at runtime. Keep this firmly in mind.

## The Standard Exception Hierarchy—the exception Family

The C++ standard library defines an exception class hierarchy rooted at `std::exception`. Knowing this hierarchy gives us two benefits: we can choose the standard exception class that best expresses the error's semantics, and we can catch an entire family of exceptions through a reference to a base class.

Take `std::exception`: it is the base class of all standard exceptions, and it defines the virtual function `what()` returning a `const char*` that describes the exception (in plain words, what on earth this exception is).

Its direct derived classes split into two major branches.

`std::logic_error` means "the program's logic is wrong"—in theory detectable before the program even runs, such as when an invalid argument is passed. Its subclasses include `std::invalid_argument` (illegal argument), `std::out_of_range` (index out of bounds), and `std::domain_error` (domain error, which almost nobody uses in practice).

`std::runtime_error` means "a problem that only surfaces at runtime"—it can only appear once the program is up and running, such as a missing file or a network timeout. Its subclasses include `std::overflow_error` and `std::underflow_error` (arithmetic overflow). Additionally, `std::bad_alloc` inherits directly from `std::exception` and is thrown when `new` cannot allocate memory.

Using this inheritance hierarchy, we can do **hierarchical catching**:

```cpp
#include <iostream>
#include <stdexcept>
#include <vector>

int main()
{
    try {
        std::vector<int> v = {1, 2, 3};
        std::cout << v.at(10) << "\n";  // at() throws out_of_range when the index is out of bounds
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

Note that the `catch` blocks match top to bottom: the first `catch` whose type matches gets executed, and the ones after it are skipped.

The order of `catch` clauses matters: always put the most specific exception types first and the most general ones last. If we put `catch (const std::exception&)` first, every standard exception gets intercepted by it, and all the `catch` clauses after it become dead code. Worse still, the compiler won't emit any warning for this kind of mistake—it only shows up at runtime.

## Throw by Value, Catch by const Reference

A best practice widely accepted in the C++ community: **throw by value, catch by const reference**. We throw by value because the value of the `throw` expression is copied (or moved) into a special storage area managed by the compiler, so even if the original object is destroyed during stack unwinding, the exception object itself remains valid. Catching by `const` reference avoids **object slicing**: if we catch `std::exception` by value while what was actually thrown is a `std::runtime_error`, the derived-class part gets sliced off, and `what()` invokes the base-class version instead of the derived one.

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

The `const char*` pointer returned by `what()` points at a string stored inside the exception object; once the exception object is destroyed, that pointer dangles. So using `e.what()` inside the `catch` block is safe, but if we save the return value and use it outside the `catch` block—good luck to you. The right approach is to copy the contents into a `std::string` inside the `catch` block.

## Multiple catch Blocks and Rethrowing

A `try` block can be followed by multiple `catch` blocks, each handling a different type of exception. Also, sometimes after a `catch` block catches an exception we find we can't handle it, or we need to do some cleanup work and then keep propagating it outward—that's when **rethrowing** comes in: a lone `throw;` (with no expression attached):

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
        throw;  // rethrow the original exception, preserving full type information
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
        // catch exceptions of all other types
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

`throw;` and `throw e;` differ in essence: the former rethrows the **original exception object**, preserving its full dynamic type information; the latter copies a fresh exception object whose static type is that of the `catch` parameter, and the derived-class information gets sliced away. So unless you genuinely want to change the exception's type, always use `throw;`. `catch (...)` means "catch exceptions of any type"—it is occasionally used in destructors or at library boundaries, but don't overuse it in everyday code: swallowing an exception without doing anything with it is the root of debugging nightmares.

## noexcept—A Promise Not to Throw

Since C++11 we've had the `noexcept` keyword, used to declare that a function **will not throw exceptions**. This is not just a comment for programmers to read: the compiler performs optimizations based on this promise (for example, omitting the bookkeeping code related to stack unwinding), and some standard library components also choose their implementation path depending on whether an operation is `noexcept`.

```cpp
int safe_computation(int a, int b) noexcept
{
    return a + b;  // pure computation, genuinely cannot throw
}
```

If an exception really is thrown inside a function marked `noexcept`, the program immediately calls `std::terminate`—no stack unwinding, no chance for any `catch`, straight-up death. So `noexcept` is not something to add casually; we must be sure the function truly cannot throw, or that it internally uses `try-catch` to swallow all possible exceptions. `noexcept` can also take a boolean argument: `noexcept(true)` is equivalent to `noexcept`, and `noexcept(false)` is equivalent to not writing it—the standard library's `std::swap` decides its own exception specification based on the `noexcept` properties of the element type.

## Putting It into Practice—exceptions.cpp

Now let's integrate the knowledge points from earlier into one complete program, implementing safe integer division and a file content parser.

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
            throw;  // rethrow and let the caller decide how to handle it
        }
    }
    return result;
}

/// @brief Format and print the parse results (noexcept example)
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

    // File parsing demo
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

    // catch-all demo
    std::cout << "\n=== Catch-all Demo ===\n";
    try { throw 42; }
    catch (const std::exception&) { std::cout << "  Standard\n"; }
    catch (...) { std::cout << "  Unknown exception\n"; }

    return 0;
}
```

The complete code is right below—hit "Try It" to run it directly, no terminal needed:

<OnlineCompilerDemo
  title="Hands-On Practice: exceptions.cpp"
  source-path="code/examples/vol1/20_exceptions.cpp"
  description="Run the safe division and file parser online. Try stuffing one more garbage line into the test file and watch how the reported line number changes."
  run-options="-O2 -std=c++17"
  allow-run
/>

Let's verify section by section. Safe division: `10 / 3` yields `3` as expected. For `7 / 0`, `std::cout` has already printed `7 / 0 =` before `safe_divide` throws the exception, so the error message follows after that prefix; `-20 / 4` gives `-5`. File parsing: the third line of the test file, `"not_a_number"`, cannot be parsed by `std::stoi`; the `catch` block in `parse_int_file` prints the line-number context and then rethrows with `throw;`, and the main function catches it.

Hey! Notice that `print_results` never gets called, because the exception broke out of the parsing loop at line 3. The `catch(...)` part demonstrates the fallback catch for non-standard exception types. The exact content of `stoi`'s `what()` message varies by compiler and standard library version (libstdc++, for example, may print `stoi` or `stoi: no conversion`).

When parsing fails, `std::stoi` throws `std::invalid_argument` (no conversion possible) or `std::out_of_range` (value outside the range of `int`). Both of these exceptions inherit from `std::logic_error`. If we need to distinguish the two cases in a `catch` block, we should handle them with two separate `catch` clauses rather than lumping them into a single `catch (const std::exception&)`—the latter loses the error's specific type information and makes debugging harder.

## Time to Practice

### Exercise 1: Safe Array Access

Write a function `int safe_get(const std::vector<int>& v, std::size_t index)` that throws `std::out_of_range` when `index` is out of bounds, with an error message containing the requested index and the vector's actual size. In `main`, test both a normal access and an out-of-bounds access.

### Exercise 2: String-to-Number Parser

Write a function `std::vector<double> parse_doubles(const std::string& input)` that parses a comma-separated string (such as `"1.5,2.7,3.14"`) into a vector of `double`. Requirements: report invalid number formats with `std::invalid_argument`, and report empty input with `std::runtime_error`. At the call site, handle the two exceptions separately with `try`/`catch` and give friendly messages.

### Exercise 3: The noexcept Operator

Write two functions: `void safe_calc(int x) noexcept` that does a simple computation, and `void risky_calc(int x)` that throws `std::invalid_argument` when `x` is negative. Then in `main`, use the two compile-time operators `noexcept(safe_calc)` and `noexcept(risky_calc)` to check their `noexcept` status and print the results.
