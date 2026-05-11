---
title: C++ Features Reference Card
description: A quick-reference index of all major features from C++98 to C++23, organized
  in a dual-view layout by standard version and functional category
chapter: 99
order: 1
tags:
- host
- cpp-modern
- 入门
difficulty: beginner
---
<!-- markdownlint-disable MD051 -->

# C++ Feature Reference Card

A structured quick-reference index covering all major features from C++98 through C++23. Features with existing reference cards are clickable links leading to core API signatures, minimal compilable examples, embedded systems applicability, and compiler support details. Features without reference cards are listed as plain text and will be gradually added in future batches.

> Need a quick syntax check for a feature? Look here. Want to learn systematically? Check out the corresponding tutorial chapter.

## Quick Navigation

**By standard version:**
[C++98/03](#c9803) | [C++11](#c11) | [C++14](#c14) | [C++17](#c17) | [C++20](#c20) | [C++23](#c23)

**By functional category:**
[Memory Management](#内存管理) | [Containers & Views](#容器与视图) | [Concurrency](#并发) | [Core Language Features](#核心语言特性) | [Templates & Metaprogramming](#模板与元编程)

::: info Legend
- **Applicability**: High = strongly recommended for embedded, Medium = use case dependent, Low = rarely needed
- Blue links = reference card available, plain text = reference card pending
- "Language feature" in the header file column indicates a core language mechanism that requires no `#include`
:::

## By Standard Version

### C++98/03

C++98 (ISO/IEC 14882:1998) is the first ISO-standardized version, establishing the three pillars of STL containers/algorithms/iterators, exception handling, namespaces, and templates—mechanisms that remain the daily foundation of C++ programming today. C++03 is a defect report release that only clarified details like value initialization semantics, with no major new features.

| Feature | Header | Summary | Applicability |
|---------|--------|---------|---------------|
| STL containers (vector, list, deque, map, set...) | `<vector>` etc. | Sequential/associative/unordered container family | **High** |
| STL algorithms (sort, find, transform...) | `<algorithm>` | Sorting, searching, transforming, and other generic algorithms | **High** |
| STL iterators | `<iterator>` | Unified traversal interface | **High** |
| std::string | `<string>` | Variable-length string | **High** |
| iostream | `<iostream>` | Type-safe I/O streams | **Medium** |
| RAII (construct/destruct/copy semantics) | Language feature | RAII (Resource Acquisition Is Initialization) | **High** |
| Exception handling (try/catch/throw) | Language feature | Structured error handling | **Medium** |
| Namespaces (namespace) | Language feature | Preventing name collisions | **High** |
| Class templates / function templates | Language feature | Foundation of generic programming | **High** |
| Operator overloading | Language feature | Custom type operation behavior | **Medium** |
| Function objects (functors) | `<functional>` | Callable objects and adapters | **Medium** |
| RTTI (dynamic_cast, typeid) | `<typeinfo>` | Run-time type identification | **Low** |
| std::complex / std::valarray | `<complex>` | Numerical computation support | **Low** |

### C++11

C++11 is the starting point of modern C++, bringing revolutionary features like lambda expressions, auto, move semantics, smart pointers, and the concurrency support library. Starting from this version, C++ evolved from "C with classes" into a truly efficient abstraction language—this is the first stop for learning Modern C++.

| Feature | Header | Summary | Applicability |
|---------|--------|---------|---------------|
| [std::unique_ptr](memory/01-unique-ptr.md) | `<memory>` | Unique pointer | **High** |
| [std::shared_ptr](memory/02-shared-ptr.md) | `<memory>` | Shared pointer | **Medium** |
| std::weak_ptr | `<memory>` | Breaking shared_ptr cyclic references | **Medium** |
| [lambda expression](core-language/02-lambda.md) | Language feature | Anonymous function objects | **High** |
| [auto](core-language/03-auto-decltype.md) | Language feature | Automatic type deduction | **High** |
| [decltype](core-language/03-auto-decltype.md) | Language feature | Expression type query | **High** |
| [constexpr](core-language/01-constexpr.md) | Language feature | Compile-time constants and functions | **High** |
| Range-for | Language feature | Container traversal syntactic sugar | **High** |
| Move semantics (rvalue reference) | Language feature | Resource transfer instead of copying | **High** |
| std::move / std::forward | `<utility>` | Move and perfect forwarding utilities | **High** |
| nullptr | Language feature | Type-safe null pointer constant | **High** |
| enum class | Language feature | Scoped strongly-typed enumeration | **High** |
| override / final | Language feature | Explicit virtual function annotations | **High** |
| static_assert | Language feature | Compile-time assertion | **High** |
| Variadic templates | Language feature | Arbitrary number of template parameters | **High** |
| std::initializer_list | `<initializer_list>` | Uniform initialization list | **High** |
| std::array | `<array>` | Compile-time fixed-size array | **High** |
| std::tuple | `<tuple>` | Heterogeneous fixed-size container | **Medium** |
| std::unordered_map / set | `<unordered_map>` | Hash table containers | **Medium** |
| std::function | `<functional>` | Polymorphic function wrapper | **Medium** |
| User-defined literal | Language feature | Custom literal suffixes | **Medium** |
| Delegating/inheriting constructors | Language feature | Constructor reuse | **Medium** |
| alignas / alignof | Language feature | Alignment control and query | **Medium** |
| std::thread | `<thread>` | Platform-independent thread | **High** |
| std::mutex / lock_guard | `<mutex>` | Mutex and RAII lock | **High** |
| std::atomic | `<atomic>` | Lock-free atomic operation | **High** |
| std::condition_variable | `<condition_variable>` | Condition variable synchronization | **Medium** |
| std::future / async | `<future>` | Asynchronous tasks and result retrieval | **Medium** |
| std::chrono | `<chrono>` | Time library | **High** |

### C++14

C++14 refines and polishes C++11—relaxing constexpr restrictions, introducing generic lambda expressions and std::make_unique. The changes are modest but highly practical, and almost all features can be used directly in embedded development.

| Feature | Header | Summary | Applicability |
|---------|--------|---------|---------------|
| std::make_unique | `<memory>` | Exception-safe unique_ptr creation | **High** |
| Generic lambda | Language feature | Lambda parameters using auto | **High** |
| Return type deduction (auto return) | Language feature | Function return value auto deduction | **Medium** |
| constexpr extensions | Language feature | Relaxed constexpr restrictions (loops/local variables) | **High** |
| decltype(auto) | Language feature | Perfect forwarding return type deduction | **Medium** |
| std::exchange | `<utility>` | Replace and return old value | **Medium** |
| std::integer_sequence | `<utility>` | Compile-time integer sequence | **Medium** |
| Binary literals (0b) | Language feature | 0b-prefixed binary integers | **Medium** |
| Digit separators (') | Language feature | Apostrophe-separated digits for readability | **Low** |
| std::shared_timed_mutex | `<shared_mutex>` | Timed shared mutex | **Low** |

### C++17

C++17 introduces high-frequency features like structured bindings, if constexpr, string_view, optional, and variant, significantly improving the expressiveness of daily coding. CTAD and guaranteed copy elision eliminate a lot of boilerplate code, and std::filesystem fills the gap in file operations.

| Feature | Header | Summary | Applicability |
|---------|--------|---------|---------------|
| [std::optional](memory/03-optional.md) | `<optional>` | Optional value wrapper | **High** |
| [std::variant](containers/03-variant.md) | `<variant>` | Type-safe union | **Medium** |
| [std::string_view](containers/02-string-view.md) | `<string_view>` | Zero-copy string view | **High** |
| std::any | `<any>` | Type-safe any value container | **Low** |
| std::filesystem | `<filesystem>` | File system operations | **Medium** |
| Structured binding | Language feature | Multiple return value destructuring | **High** |
| if constexpr | Language feature | Compile-time conditional branching | **High** |
| Fold expressions | Language feature | Parameter pack expansion operations | **High** |
| CTAD | Language feature | Class template argument deduction | **High** |
| Guaranteed copy elision | Language feature | Mandatory elimination of temporary object copies | **High** |
| std::invoke | `<functional>` | Unified call interface | **Medium** |
| std::apply | `<tuple>` | Tuple expansion as function arguments | **Medium** |
| Inline variables | Language feature | Defining global variables in headers | **Medium** |
| std::byte | `<cstddef>` | Standalone byte type | **Medium** |
| std::pmr memory resources | `<memory_resource>` | Polymorphic allocator memory resources | **Medium** |
| std::shared_mutex | `<shared_mutex>` | Read-write lock | **Medium** |
| Nested namespaces (A::B::C) | Language feature | Namespace shorthand | **Low** |
| if/switch initializer statements | Language feature | Variable declarations inside conditional statements | **Medium** |

### C++20

C++20 is the largest update since C++11: four major features—Concepts, Ranges, Coroutines, and Modules—fundamentally change template programming, data pipelines, asynchronous flows, and code organization. At the same time, std::format, std::span, and three-way comparison significantly improve the daily development experience. Higher compiler support is required (GCC 10+ / Clang 10+).

| Feature | Header | Summary | Applicability |
|---------|--------|---------|---------------|
| [Concepts](templates/01-concepts.md) | `<concepts>` | Compile-time template parameter constraints | **High** |
| Ranges | `<ranges>` | Composable ranges and views | **High** |
| [std::span](containers/01-span.md) | `<span>` | Non-owning view over a contiguous sequence | **High** |
| std::format | `<format>` | Type-safe formatted output | **High** |
| std::jthread | `<thread>` | Auto-joining thread class | **High** |
| Three-way comparison (<=>) | `<compare>` | Unified comparison operator | **High** |
| Coroutines | `<coroutine>` | Stackless coroutines | **High** |
| Modules | Language feature | Compilation units replacing headers | **High** |
| consteval | Language feature | Forced compile-time evaluation | **Medium** |
| constinit | Language feature | Compile-time static variable initialization | **Medium** |
| std::source_location | `<source_location>` | Compile-time source code location information | **Medium** |
| Designated initializer | Language feature | Aggregate initialization by member name | **Medium** |
| std::atomic_ref | `<atomic>` | Atomic reference operations | **Medium** |
| std::latch / barrier | `<latch>` | Thread synchronization primitives | **Medium** |
| std::stop_token | `<stop_token>` | Cooperative thread cancellation | **Medium** |
| std::erase / erase_if | `<vector>` etc. | Unified interface for container element removal | **Medium** |
| std::is_constant_evaluated | `<type_traits>` | Detecting compile-time evaluation context | **Medium** |
| Range-for initializer statements | Language feature | Range-for with initializer | **Low** |

### C++23

C++23 polishes and fills gaps in C++20: practical library components like std::expected, std::print, std::generator, and std::flat_map fill critical voids, while language improvements like deducing this streamline member function syntax. Compiler support for some features is still in progress (GCC 14+ / Clang 18+).

| Feature | Header | Summary | Applicability |
|---------|--------|---------|---------------|
| std::expected | `<expected>` | Error handling wrapper type | **Medium** |
| std::print / println | `<print>` | Formatted output to stdout | **High** |
| std::generator | `<generator>` | Coroutine synchronous generator | **Medium** |
| std::flat_map / flat_set | `<flat_map>` | Sorted containers based on contiguous storage | **Medium** |
| std::mdspan | `<mdspan>` | Non-owning multidimensional array view | **Medium** |
| std::stacktrace | `<stacktrace>` | Backtrace capture and printing | **Medium** |
| Deducing this | Language feature | Explicit object parameter deduction | **Medium** |
| std::to_underlying | `<utility>` | Enum to underlying type conversion | **Medium** |
| std::out_ptr / inout_ptr | `<memory>` | Smart pointer and C pointer interop | **Medium** |
| Optional monadic operations | `<optional>` | and_then / or_else / transform | **Medium** |
| New Ranges adapters | `<ranges>` | zip / chunk / slide / enumerate etc. | **Medium** |
| if consteval | Language feature | Compile-time evaluation conditional check | **Low** |
| std::unreachable | `<utility>` | Mark unreachable code | **Low** |
| Multidimensional subscript operator | Language feature | operator[] accepting multiple arguments | **Low** |
| std::is_scoped_enum | `<type_traits>` | Detect scoped enum types | **Low** |

## By Functional Category

### Memory Management

Smart pointers, optional values, error handling, and other memory and resource management features. See [Memory Management Reference Card](memory/index.md).

| Feature | Version | Header | Summary | Applicability |
|---------|---------|--------|---------|---------------|
| [std::unique_ptr](memory/01-unique-ptr.md) | C++11 | `<memory>` | Unique pointer | **High** |
| [std::shared_ptr](memory/02-shared-ptr.md) | C++11 | `<memory>` | Shared pointer | **Medium** |
| std::weak_ptr | C++11 | `<memory>` | Breaking shared_ptr cyclic references | **Medium** |
| std::make_unique | C++14 | `<memory>` | Exception-safe unique_ptr creation | **High** |
| [std::optional](memory/03-optional.md) | C++17 | `<optional>` | Optional value wrapper | **High** |
| std::pmr memory resources | C++17 | `<memory_resource>` | Polymorphic allocator memory resources | **Medium** |
| std::expected | C++23 | `<expected>` | Error handling wrapper type | **Medium** |
| std::out_ptr / inout_ptr | C++23 | `<memory>` | Smart pointer and C pointer interop | **Medium** |
| Optional monadic operations | C++23 | `<optional>` | and_then / or_else / transform | **Medium** |

::: details Pending Reference Cards
The following features do not have reference cards yet: std::weak_ptr, std::make_unique, std::pmr memory resources, std::expected, std::out_ptr / inout_ptr, optional monadic operations
:::

### Containers & Views

Standard containers, views, strings, formatting, algorithms, and other data organization and manipulation features. See [Containers & Views Reference Card](containers/index.md).

| Feature | Version | Header | Summary | Applicability |
|---------|---------|--------|---------|---------------|
| STL containers (vector, list, deque, map, set...) | C++98 | `<vector>` etc. | Sequential/associative/unordered container family | **High** |
| STL algorithms | C++98 | `<algorithm>` | Sorting, searching, transforming, and other generic algorithms | **High** |
| std::string | C++98 | `<string>` | Variable-length string | **High** |
| std::array | C++11 | `<array>` | Compile-time fixed-size array | **High** |
| std::tuple | C++11 | `<tuple>` | Heterogeneous fixed-size container | **Medium** |
| std::unordered_map / set | C++11 | `<unordered_map>` | Hash table containers | **Medium** |
| std::function | C++11 | `<functional>` | Polymorphic function wrapper | **Medium** |
| [std::string_view](containers/02-string-view.md) | C++17 | `<string_view>` | Zero-copy string view | **High** |
| [std::variant](containers/03-variant.md) | C++17 | `<variant>` | Type-safe union | **Medium** |
| std::any | C++17 | `<any>` | Type-safe any value container | **Low** |
| std::filesystem | C++17 | `<filesystem>` | File system operations | **Medium** |
| Ranges | C++20 | `<ranges>` | Composable ranges and views | **High** |
| [std::span](containers/01-span.md) | C++20 | `<span>` | Non-owning view over a contiguous sequence | **High** |
| std::format | C++20 | `<format>` | Type-safe formatted output | **High** |
| std::erase / erase_if | C++20 | `<vector>` etc. | Unified interface for container element removal | **Medium** |
| std::flat_map / flat_set | C++23 | `<flat_map>` | Sorted containers based on contiguous storage | **Medium** |
| std::generator | C++23 | `<generator>` | Coroutine synchronous generator | **Medium** |
| std::print / println | C++23 | `<print>` | Formatted output to stdout | **High** |
| std::mdspan | C++23 | `<mdspan>` | Non-owning multidimensional array view | **Medium** |
| New Ranges adapters | C++23 | `<ranges>` | zip / chunk / slide / enumerate etc. | **Medium** |

::: details Pending Reference Cards
The following features do not have reference cards yet: STL containers, STL algorithms, std::string, std::array, std::tuple, std::unordered_map/set, std::function, std::any, std::filesystem, Ranges, std::format, std::erase/erase_if, std::flat_map/flat_set, std::generator, std::print, std::mdspan, new Ranges adapters

### Concurrency

Threads, locks, atomic operations, synchronization primitives, and other concurrency and multithreading features. See [Concurrency Reference Card](concurrency/index.md).

| Feature | Version | Header | Summary | Applicability |
|---------|---------|--------|---------|---------------|
| std::thread | C++11 | `<thread>` | Platform-independent thread | **High** |
| std::mutex / lock_guard | C++11 | `<mutex>` | Mutex and RAII lock | **High** |
| std::atomic | C++11 | `<atomic>` | Lock-free atomic operation | **High** |
| std::condition_variable | C++11 | `<condition_variable>` | Condition variable synchronization | **Medium** |
| std::future / async | C++11 | `<future>` | Asynchronous tasks and result retrieval | **Medium** |
| std::chrono | C++11 | `<chrono>` | Time library | **High** |
| std::shared_timed_mutex | C++14 | `<shared_mutex>` | Timed shared mutex | **Low** |
| std::shared_mutex | C++17 | `<shared_mutex>` | Read-write lock | **Medium** |
| std::jthread | C++20 | `<thread>` | Auto-joining thread class | **High** |
| std::atomic_ref | C++20 | `<atomic>` | Atomic reference operations | **Medium** |
| std::latch / barrier | C++20 | `<latch>` | Thread synchronization primitives | **Medium** |
| std::stop_token | C++20 | `<stop_token>` | Cooperative thread cancellation | **Medium** |

::: details Pending Reference Cards
Reference cards for all features under the Concurrency category are pending. During the second batch, we will prioritize covering std::atomic, std::thread, and std::mutex/lock_guard.

### Core Language Features

Keywords, syntactic sugar, type systems, compile-time mechanisms, and other core language features. See [Core Language Reference Card](core-language/index.md).

| Feature | Version | Header | Summary | Applicability |
|---------|---------|--------|---------|---------------|
| RAII (construct/destruct/copy) | C++98 | Language feature | RAII (Resource Acquisition Is Initialization) | **High** |
| Exception handling | C++98 | Language feature | Structured error handling | **Medium** |
| Namespaces | C++98 | Language feature | Preventing name collisions | **High** |
| Operator overloading | C++98 | Language feature | Custom type operation behavior | **Medium** |
| iostream | C++98 | `<iostream>` | Type-safe I/O streams | **Medium** |
| [lambda expression](core-language/02-lambda.md) | C++11 | Language feature | Anonymous function objects | **High** |
| [auto](core-language/03-auto-decltype.md) | C++11 | Language feature | Automatic type deduction | **High** |
| [decltype](core-language/03-auto-decltype.md) | C++11 | Language feature | Expression type query | **High** |
| [constexpr](core-language/01-constexpr.md) | C++11 | Language feature | Compile-time constants and functions | **High** |
| Range-for | C++11 | Language feature | Container traversal syntactic sugar | **High** |
| Move semantics (rvalue reference) | C++11 | Language feature | Resource transfer instead of copying | **High** |
| std::move / std::forward | C++11 | `<utility>` | Move and perfect forwarding utilities | **High** |
| nullptr | C++11 | Language feature | Type-safe null pointer | **High** |
| enum class | C++11 | Language feature | Scoped strongly-typed enumeration | **High** |
| override / final | C++11 | Language feature | Explicit virtual function annotations | **High** |
| static_assert | C++11 | Language feature | Compile-time assertion | **High** |
| User-defined literal | C++11 | Language feature | Custom literal suffixes | **Medium** |
| Delegating/inheriting constructors | C++11 | Language feature | Constructor reuse | **Medium** |
| alignas / alignof | C++11 | Language feature | Alignment control and query | **Medium** |
| Generic lambda | C++14 | Language feature | Lambda parameters using auto | **High** |
| Return type deduction | C++14 | Language feature | Function return value auto | **Medium** |
| constexpr extensions | C++14 | Language feature | Relaxed constexpr restrictions | **High** |
| decltype(auto) | C++14 | Language feature | Perfect forwarding return type deduction | **Medium** |
| Binary literals | C++14 | Language feature | 0b-prefixed binary integers | **Medium** |
| Structured binding | C++17 | Language feature | Multiple return value destructuring | **High** |
| if constexpr | C++17 | Language feature | Compile-time conditional branching | **High** |
| CTAD | C++17 | Language feature | Class template argument deduction | **High** |
| Guaranteed copy elision | C++17 | Language feature | Mandatory elimination of temporary object copies | **High** |
| Inline variables | C++17 | Language feature | Defining global variables in headers | **Medium** |
| std::byte | C++17 | `<cstddef>` | Standalone byte type | **Medium** |
| Nested namespaces | C++17 | Language feature | A::B::C shorthand | **Low** |
| if/switch initializer statements | C++17 | Language feature | Variable declarations inside conditional statements | **Medium** |
| Three-way comparison (<=>) | C++20 | `<compare>` | Unified comparison operator | **High** |
| Coroutines | C++20 | `<coroutine>` | Stackless coroutines | **High** |
| Modules | C++20 | Language feature | Replacing headers | **High** |
| consteval | C++20 | Language feature | Forced compile-time evaluation | **Medium** |
| constinit | C++20 | Language feature | Compile-time static initialization | **Medium** |
| std::source_location | C++20 | `<source_location>` | Compile-time source code location | **Medium** |
| Designated initializer | C++20 | Language feature | Aggregate initialization by member name | **Medium** |
| Deducing this | C++23 | Language feature | Explicit object parameter deduction | **Medium** |
| std::to_underlying | C++23 | `<utility>` | Enum to underlying type conversion | **Medium** |
| std::unreachable | C++23 | `<utility>` | Mark unreachable code | **Low** |
| if consteval | C++23 | Language feature | Compile-time evaluation conditional check | **Low** |
| Multidimensional subscript operator | C++23 | Language feature | operator[] with multiple arguments | **Low** |
| std::stacktrace | C++23 | `<stacktrace>` | Backtrace capture and printing | **Medium** |

::: details Pending Reference Cards
The following high-frequency features will be prioritized for reference card creation in future batches: move semantics, std::move/std::forward, nullptr, enum class, static_assert, structured binding, if constexpr, CTAD, three-way comparison, Coroutines, Modules

### Templates & Metaprogramming

Templates, constraints, type traits, compile-time computation, and other generic programming and metaprogramming features. See [Templates & Metaprogramming Reference Card](templates/index.md).

| Feature | Version | Header | Summary | Applicability |
|---------|---------|--------|---------|---------------|
| Class templates / function templates | C++98 | Language feature | Foundation of generic programming | **High** |
| Variadic templates | C++11 | Language feature | Arbitrary number of template parameters | **High** |
| std::initializer_list | C++11 | `<initializer_list>` | Uniform initialization list | **High** |
| std::integer_sequence | C++14 | `<utility>` | Compile-time integer sequence | **Medium** |
| Fold expressions | C++17 | Language feature | Parameter pack expansion operations | **High** |
| std::invoke | C++17 | `<functional>` | Unified call interface | **Medium** |
| std::apply | C++17 | `<tuple>` | Tuple expansion as function arguments | **Medium** |
| [Concepts](templates/01-concepts.md) | C++20 | `<concepts>` | Compile-time template parameter constraints | **High** |
| std::is_constant_evaluated | C++20 | `<type_traits>` | Detecting compile-time context | **Medium** |
| std::is_scoped_enum | C++23 | `<type_traits>` | Detecting scoped enumerations | **Low** |

::: details Pending Reference Cards
The following features do not have reference cards yet: variadic templates, std::initializer_list, std::integer_sequence, fold expressions, std::invoke, std::apply, std::is_constant_evaluated, std::is_scoped_enum

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
