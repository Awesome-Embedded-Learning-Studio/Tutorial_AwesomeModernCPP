---
title: Exception Basics
description: Master the try/catch/throw syntax and the standard exception hierarchy
chapter: 10
order: 1
difficulty: intermediate
reading_time_minutes: 14
platform: host
prerequisites:
- 模板特化初步
tags:
- cpp-modern
- host
- intermediate
- 进阶
cpp_standard:
- 11
- 14
- 17
- 20
translation:
  source: documents/vol1-fundamentals/ch10/01-try-catch.md
  source_hash: 49b53d2c5cd3ad7f7671f4b8c41df0e56a968c5cf578d1e5bd50e71bf605301c
  translated_at: '2026-05-26T10:56:56.745372+00:00'
  engine: anthropic
  token_count: 2822
---
# Exception Basics

So far, we have handled errors in basically two ways: either using return values to indicate failure (such as a function returning `-1` or `nullptr`), or directly calling `assert` to crash the program. These two approaches barely suffice in small programs, but once the project scales up, the problems become apparent—return value error codes are easily ignored by callers, and `assert` gets stripped out entirely by the compiler in Release builds. What's more troublesome is that if an error occurs deep in a nested call chain, you have to propagate the error code outward layer by layer. Every intermediate layer must check and handle it, and the code quickly turns into a giant `if (error)` Christmas tree. (Having seen this thing so many times, it honestly makes me want to throw up...)

C++'s exception mechanism was born to solve this problem. It provides a **structured error propagation channel**—a function can directly throw an exception to report "something went wrong," and any capable caller along the call chain can catch and handle it. The intermediate functions don't need to know about it or pass it through. In this chapter, we start with the basic `try`/`catch`/`throw` syntax, clarify the hierarchy of standard exception classes, and finally write a complete practical example to tie all the concepts together.

## Ignition — the throw, try, catch trio

The core operations of the exception mechanism involve only three keywords. `throw` is responsible for throwing an exception—the expression following it is an exception object, which can be any copyable type. `try` marks a code region where "something might go wrong." `catch` is responsible for catching and handling exceptions thrown within the `try` region. Let's look at a minimal example first:

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

The output is `Caught: Something went wrong`. `throw` creates a `std::runtime_error` object and throws it. The program immediately interrupts execution after `throw` in the `try` block and jumps to the matching `catch` block. `e.what()` returns the string passed in during construction. You might ask: why use `std::runtime_error` instead of directly throwing `throw 42` or `throw "oops"`? Technically, you could—C++ allows throwing any type—but in practical engineering, using standard exception classes or custom exception classes is a better approach. Exception objects can carry rich error information, and you can leverage the inheritance hierarchy for hierarchical catching.

### Stack unwinding — what happens when an exception flies by

After an exception is thrown, the program doesn't just jump directly from `throw` to `catch`—a very important process called **stack unwinding** happens in between. Between the `throw` point and the nearest matching `catch`, all local objects that have already been constructed are destructed in **reverse** order of their construction. This mechanism is the foundation that allows RAII to guarantee no resource leaks.

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

`t3`, `t2`, and `t1` are destructed in reverse order of their construction—this is stack unwinding. The entire process requires us to write no manual cleanup code; the language mechanism guarantees everything.

> **Pitfall warning**: During stack unwinding, if a destructor itself throws an exception (a new exception is generated while handling an existing one), the program will directly call `std::terminate` and terminate, with no chance of recovery. Therefore, destructors must **absolutely not** throw exceptions. Starting with C++11, all destructors are implicitly marked as `noexcept`, but if you explicitly write `~MyClass() { throw ...; }` yourself, the compiler won't stop you, and it will blow up at runtime. Make sure to keep this in mind.

## Standard exception hierarchy — the exception family

The C++ standard library defines an exception class hierarchy rooted at `std::exception`. Understanding this hierarchy has two benefits: first, you can choose the most appropriate standard exception class to express error semantics, and second, you can use a base class reference to catch an entire family of exceptions.

`std::exception` is the base class of all standard exceptions, defining the virtual function `what()` that returns a `const char*` description. Its direct derived classes split into two major branches. `std::logic_error` represents "logical errors in the program"—theoretically detectable before the program runs, such as passing an invalid argument; its subclasses include `std::invalid_argument` (invalid argument), `std::out_of_range` (out-of-range subscript), and `std::domain_error` (domain error, which practically no one uses). `std::runtime_error` represents "problems exposed only at runtime"—they only appear after the program starts running, such as a file not existing or a network timeout; its subclasses include `std::overflow_error` and `std::underflow_error` (arithmetic overflow). Additionally, `std::bad_alloc` inherits directly from `std::exception` and is thrown when `new` fails to allocate memory.

Leveraging this inheritance hierarchy, we can perform **hierarchical catching**:

```cpp
#include <iostream>
#include <stdexcept>
#include <vector>

int main()
{
    try {
        std::vector<int> v = {1, 2, 3};
        std::cout << v.at(10) << "\n";  // at() 越界抛出 out_of_range
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

The matching rule for `catch` blocks is top-to-bottom: the first `catch` with a matching type gets executed, and the rest are skipped.

> **Pitfall warning**: The order of `catch` is very important. Always put the most specific exception types first and the most generic ones last. If you put `catch (const std::exception&)` first, all standard exceptions will be intercepted by it, and the subsequent `catch` blocks will all become dead code. What's worse, the compiler won't issue any warning for this mistake—it only exposes itself at runtime.

## Throw by value, catch by const reference

A widely recognized best practice in the C++ community: **throw by value, catch by const reference**. Throwing by value is because the value of the `throw` expression gets copied (or moved) into a special storage area managed by the compiler. Even if the original object is destructed during stack unwinding, the exception object itself remains valid. Catching by `const` reference avoids **object slicing**—if you catch `std::exception` by value and you actually threw a `std::runtime_error`, the derived class portion gets sliced off, and `what()` calls the base class version instead of the derived class version.

```cpp
// 错误：按值捕获会切片
catch (std::exception e) {           // runtime_error 部分丢失！
    std::cout << e.what() << "\n";   // 错误信息可能完全不对
}

// 正确：按 const 引用捕获
catch (const std::exception& e) {    // 多态完整保留
    std::cout << e.what() << "\n";   // 正确输出原始信息
}
```

> **Pitfall warning**: The `const char*` pointer returned by `what()` points to a string stored inside the exception object. Once the exception object is destroyed, this pointer dangles. So using `e.what()` inside the `catch` block is safe, but if you save the return value and use it outside the `catch` block—good luck. The correct approach is to copy the contents into a `std::string` inside the `catch` block.

## Multiple catch blocks and rethrowing

A single `try` block can be followed by multiple `catch` blocks to handle different types of exceptions separately. Additionally, sometimes after a `catch` block catches an exception, it finds it can't handle it, or it needs to do some cleanup work and then continue throwing it outward. This is where **rethrowing** comes in—just write a bare `throw;` (without any expression):

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
        throw;  // 重新抛出原始异常，保持完整类型信息
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
        // 捕获所有其他类型的异常
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

There is a fundamental difference between `throw;` and `throw e;`—the former rethrows the **original exception object**, preserving the complete dynamic type information; the latter copies a new exception object whose static type is that of the `catch` parameter, and the derived class information gets sliced off. So unless you genuinely want to change the type of the exception, always use `throw;`. `catch (...)` means "catch any type of exception." It is occasionally used at destructor boundaries or library boundaries, but don't abuse it in everyday code—swallowing exceptions without doing any handling is the root cause of debugging nightmares.

## noexcept — promising not to throw

Starting with C++11, the `noexcept` keyword is used to declare that a function **will not throw exceptions**. This is not just a comment for programmers to read—the compiler uses this promise to perform optimizations (such as omitting stack unwinding registration code), and some standard library components choose their implementation paths based on whether an operation is `noexcept`.

```cpp
int safe_computation(int a, int b) noexcept
{
    return a + b;  // 纯计算，确实不会抛异常
}
```

If a function marked `noexcept` actually throws an exception internally, the program immediately calls `std::terminate`—with no stack unwinding, no chance for any `catch`, just instant death. So don't just add `noexcept` casually; you need to be certain that this function truly won't throw, or that it internally uses `try-catch` to swallow all possible exceptions. `noexcept` can also accept a boolean parameter—`noexcept(true)` is equivalent to `noexcept`, and `noexcept(false)` is equivalent to not adding it at all. The standard library's `std::swap` uses the `noexcept` trait of the element type to determine its own exception specification.

## Practical example — exceptions.cpp

Now let's integrate the concepts we've covered into a complete program, implementing safe integer division and a file content parser.

```cpp
// exceptions.cpp
// 演示 try/catch/throw、标准异常层次、noexcept 的综合应用

#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

/// @brief 安全的整数除法，除数为零时抛出异常
int safe_divide(int dividend, int divisor)
{
    if (divisor == 0) {
        throw std::invalid_argument("Division by zero is not allowed");
    }
    return dividend / divisor;
}

/// @brief 解析文件中的整数行
/// @throws std::runtime_error 文件无法打开
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
            throw;  // 重新抛出，让调用者决定怎么处理
        }
    }
    return result;
}

/// @brief 格式化并打印解析结果（noexcept 示例）
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
    // 安全除法演示
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

    // 文件解析演示
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

    // catch-all 演示
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

Verify the output:

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

Let's verify it section by section. For the safe division part: `10 / 3` normally yields `3`; when `7 / 0` throws an exception before `safe_divide`, `std::cout` has already output `7 / 0 =`, so the error message follows this prefix; `-20 / 4` yields `-5`. For the file parsing part: the third line of the test file, `"not_a_number"`, cannot be parsed by `std::stoi`. The `catch` block in `parse_int_file` prints the line number context and then rethrows with `throw;`, which the main function catches—note that `print_results` is not called because the exception interrupts the parsing loop at line three. The `catch(...)` section demonstrates a catch-all for non-standard exception types. The content of the `what()` message for `stoi` varies depending on the compiler and standard library version (for example, libstdc++ might output `stoi` or `stoi: no conversion`).

> **Pitfall warning**: `std::stoi` throws `std::invalid_argument` (unable to convert) or `std::out_of_range` (value out of `int` range) when parsing fails. Both exceptions inherit from `std::logic_error`. If you need to distinguish between these two cases in a `catch` block, you should use two separate `catch` blocks to handle them individually, rather than uniformly swallowing them with `catch (const std::exception&)`—the latter loses the specific type information of the error and increases debugging difficulty.

## Practice time

### Exercise 1: Safe array access

Write a function `int safe_get(const std::vector<int>& v, std::size_t index)` that throws `std::out_of_range` when `index` is out of bounds. The error message should include the requested index and the actual size of the vector. Test both normal access and out-of-bounds access scenarios in `main`.

### Exercise 2: String-to-number parser

Write a function `std::vector<double> parse_doubles(const std::string& input)` that parses a comma-separated string (such as `"1.5,2.7,3.14"`) into a `double` vector. Requirements: report invalid number formats with `std::invalid_argument`, and report empty input with `std::runtime_error`. At the call site, use `try`/`catch` to handle the two exceptions separately and provide user-friendly messages.

### Exercise 3: The noexcept operator

Write two functions: `void safe_calc(int x) noexcept` performs a simple calculation, and `void risky_calc(int x)` throws `std::invalid_argument` when `x` is negative. Then, in `main`, use the `noexcept(safe_calc)` and `noexcept(risky_calc)` compile-time operators to check their `noexcept` status and print the results.

## Summary

In this chapter, we built the basic framework of C++ exception handling from scratch. `throw` is responsible for throwing exception objects, `try` marks the monitored region, and `catch` catches and handles exceptions—this trio forms the syntactic core of the exception mechanism. Stack unwinding ensures that all local objects are correctly destructed when an exception flies by, and the inheritance hierarchy of standard exception classes allows us to perform polymorphic catching using base class references. "Throw by value, catch by const reference" is the key convention for avoiding object slicing, `throw;` is used to rethrow the original exception, and `noexcept` is used to mark functions that don't throw exceptions—it serves as both an optimization hint for the compiler and a contractual promise to the caller.

However, knowing how to throw and how to catch is only the first step. A more important question is: when an exception flies by, what about the resources that were already allocated, the files that were opened, and the mutexes that were locked beforehand? In the next article, we will discuss this topic—exception safety. We will learn about the four levels of exception safety, see how RAII guarantees no resource leaks when exceptions occur, and learn how to use the copy-and-swap idiom to give operations transaction-level strong safety guarantees.
