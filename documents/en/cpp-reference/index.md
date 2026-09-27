---
title: "C++ Feature Cheat Sheet"
description: "A quick-reference index of all major features from C++98 through C++23, organized as dual views by standard version and by functional category"
chapter: 99
order: 1
tags:
  - host
  - cpp-modern
  - 入门
difficulty: beginner
translation:
  source: documents/cpp-reference/index.md
  source_hash: ef275b342e0914de36a1ab7e4ec0557d1aca4a7e85c835452f43e320132ba2c2
  translated_at: '2026-09-27T02:03:39+00:00'
  engine: anthropic
  token_count: 3600
---

<!-- markdownlint-disable MD051 -->

# C++ Feature Cheat Sheet

A structured quick-reference index covering all major features from C++98 through C++23. Features that already have a cheat sheet are directly clickable, leading to core API signatures, minimal compilable examples, embedded applicability, and compiler support information. Features without a cheat sheet yet are listed as plain text and will be filled in by later batches.

> Need to quickly look up the syntax of some feature? Come here. Want to learn it systematically? Go read the tutorial articles in the corresponding volume.

## Quick Navigation

**By standard version:**
[C++98/03](#c9803) | [C++11](#c11) | [C++14](#c14) | [C++17](#c17) | [C++20](#c20) | [C++23](#c23) | [C++26](#c26)

**By functional category:**
[Memory Management](#memory-management) | [Containers and Views](#containers-and-views) | [Concurrency](#concurrency) | [Core Language Features](#core-language-features) | [Templates and Metaprogramming](#templates-and-metaprogramming)

::: info Legend

- **Applicability**: High = strongly recommended for embedded use, Medium = choose by scenario, Low = usually not needed
- Blue links = a cheat sheet already exists, plain text = cheat sheet pending
- "Language feature" in the Header column means a core language mechanism that requires no `#include`

:::

## By Standard Version

### C++98/03

C++98 (ISO/IEC 14882:1998) was the first ISO-standardized version. It laid down the core machinery — the three STL pillars of containers, algorithms, and iterators, plus exception handling, namespaces, and templates — and these mechanisms still form the daily infrastructure of C++ programming today. C++03 was a bug-fix release: it only clarified details such as value-initialization semantics, with no major new features.

| Feature | Header | Summary | Applicability |
|---------|--------|---------|---------------|
| STL containers (vector, list, deque, map, set...) | `<vector>` etc. | Sequential/associative/unordered container families | **High** |
| STL algorithms (sort, find, transform...) | `<algorithm>` | Sorting/searching/transformation and other generic algorithms | **High** |
| STL iterators | `<iterator>` | Unified traversal interface | **High** |
| std::string | `<string>` | Variable-length string | **High** |
| iostream | `<iostream>` | Type-safe I/O streams | **Medium** |
| RAII (construction/destruction/copy semantics) | Language feature | Resource acquisition is initialization | **High** |
| Exception handling (try/catch/throw) | Language feature | Structured error handling | **Medium** |
| Namespaces (namespace) | Language feature | Prevents name collisions | **High** |
| Class templates / function templates | Language feature | Foundation of generic programming | **High** |
| Operator overloading | Language feature | Custom operator behavior for user types | **Medium** |
| Function objects (functors) | `<functional>` | Callable objects and adaptors | **Medium** |
| RTTI (dynamic_cast, typeid) | `<typeinfo>` | Run-time type identification | **Low** |
| std::complex / std::valarray | `<complex>` | Numeric computation support | **Low** |

### C++11

C++11 is where modern C++ begins: it brought revolutionary features such as lambdas, auto, move semantics, smart pointers, and the concurrency support library. From this version on, C++ evolved from "C with classes" into a truly efficient abstraction language — this is the first stop for learning Modern C++.

| Feature | Header | Summary | Applicability |
|---------|--------|---------|---------------|
| [std::unique_ptr](memory/01-unique-ptr.md) | `<memory>` | Smart pointer with exclusive ownership | **High** |
| [std::shared_ptr](memory/02-shared-ptr.md) | `<memory>` | Smart pointer with shared ownership | **Medium** |
| std::weak_ptr | `<memory>` | Breaks shared_ptr reference cycles | **Medium** |
| [Lambda expressions](core-language/02-lambda.md) | Language feature | Anonymous function objects | **High** |
| [auto](core-language/03-auto-decltype.md) | Language feature | Automatic type deduction | **High** |
| [decltype](core-language/03-auto-decltype.md) | Language feature | Queries the type of an expression | **High** |
| [constexpr](core-language/01-constexpr.md) | Language feature | Compile-time constants and functions | **High** |
| [Range-based for (range-for)](core-language/07-range-for.md) | Language feature | Syntactic sugar for container traversal | **High** |
| Move semantics (rvalue references) | Language feature | Transfers resources instead of copying | **High** |
| [std::move / std::forward](core-language/08-move-forward.md) | `<utility>` | Utilities for moving and perfect forwarding | **High** |
| [nullptr](core-language/04-nullptr.md) | Language feature | Type-safe null pointer constant | **High** |
| [enum class](core-language/05-enum-class.md) | Language feature | Scoped, strongly typed enumerations | **High** |
| [override / final](core-language/06-override-final.md) | Language feature | Explicit marking of virtual overrides | **High** |
| static_assert | Language feature | Compile-time assertions | **High** |
| [Variadic templates](templates/02-variadic-templates.md) | Language feature | Any number of template parameters | **High** |
| [std::initializer_list](containers/05-initializer-list.md) | `<initializer_list>` | Uniform initializer lists | **High** |
| [std::array](containers/04-array.md) | `<array>` | Compile-time fixed-size array | **High** |
| std::tuple | `<tuple>` | Heterogeneous fixed-size container | **Medium** |
| std::unordered_map / set | `<unordered_map>` | Hash-table containers | **Medium** |
| std::function | `<functional>` | Polymorphic function wrapper | **Medium** |
| User-defined literals | Language feature | Custom literal suffixes | **Medium** |
| Delegating / inheriting constructors | Language feature | Constructor reuse | **Medium** |
| alignas / alignof | Language feature | Alignment control and query | **Medium** |
| [std::thread](concurrency/02-thread.md) | `<thread>` | Platform-independent threads | **High** |
| [std::mutex / lock_guard](concurrency/03-mutex.md) | `<mutex>` | Mutexes and RAII locking | **High** |
| [std::atomic](concurrency/01-atomic.md) | `<atomic>` | Lock-free atomic operations | **High** |
| std::condition_variable | `<condition_variable>` | Condition-variable synchronization | **Medium** |
| std::future / async | `<future>` | Asynchronous tasks and result retrieval | **Medium** |
| std::chrono | `<chrono>` | Time library | **High** |

### C++14

C++14 polished and rounded off C++11 — it relaxed the constexpr restrictions and introduced generic lambdas and std::make_unique. The changes are modest but practical, and almost every one of them can be used directly in embedded development.

| Feature | Header | Summary | Applicability |
|---------|--------|---------|---------------|
| [std::make_unique](memory/04-make-unique.md) | `<memory>` | Exception-safe creation of unique_ptr | **High** |
| [Generic lambdas](core-language/09-generic-lambda.md) | Language feature | auto parameters in lambdas | **High** |
| Return type deduction (auto return) | Language feature | auto deduction of function return values | **Medium** |
| Relaxed constexpr | Language feature | Relaxed constexpr restrictions (loops/local variables) | **High** |
| decltype(auto) | Language feature | Return type deduction for perfect forwarding | **Medium** |
| [std::exchange](core-language/10-exchange.md) | `<utility>` | Replaces a value and returns the old one | **Medium** |
| std::integer_sequence | `<utility>` | Compile-time integer sequences | **Medium** |
| Binary literals (0b) | Language feature | Binary integers with the 0b prefix | **Medium** |
| Digit separators (') | Language feature | Apostrophe separators improve numeric readability | **Low** |
| std::shared_timed_mutex | `<shared_mutex>` | Timed shared mutex | **Low** |

### C++17

C++17 brought high-frequency features such as structured bindings, if constexpr, string_view, optional, and variant, markedly boosting the expressiveness of everyday coding. CTAD and guaranteed copy elision removed piles of boilerplate, and std::filesystem filled the long-standing gap in file operations.

| Feature | Header | Summary | Applicability |
|---------|--------|---------|---------------|
| [std::optional](memory/03-optional.md) | `<optional>` | Optional-value wrapper | **High** |
| [std::variant](containers/03-variant.md) | `<variant>` | Type-safe union | **Medium** |
| [std::string_view](containers/02-string-view.md) | `<string_view>` | Zero-copy string view | **High** |
| std::any | `<any>` | Type-safe container for any value | **Low** |
| [std::filesystem](containers/06-filesystem.md) | `<filesystem>` | Filesystem operations | **Medium** |
| [Structured bindings](core-language/11-structured-binding.md) | Language feature | Destructuring multiple return values | **High** |
| [if constexpr](core-language/13-if-constexpr.md) | Language feature | Compile-time conditional branches | **High** |
| [Fold expressions](templates/03-fold-expressions.md) | Language feature | Operations over expanded parameter packs | **High** |
| CTAD | Language feature | Class template argument deduction | **High** |
| Guaranteed copy elision | Language feature | Mandated elision of temporary copies | **High** |
| std::invoke | `<functional>` | Unified invocation interface | **Medium** |
| std::apply | `<tuple>` | Expands a tuple into function arguments | **Medium** |
| [Inline variables](core-language/14-inline-variables.md) | Language feature | Define global variables in headers | **Medium** |
| std::byte | `<cstddef>` | Standalone byte type | **Medium** |
| std::pmr memory resources | `<memory_resource>` | Memory resources for polymorphic allocators | **Medium** |
| std::shared_mutex | `<shared_mutex>` | Reader-writer lock | **Medium** |
| [Nested namespaces (A::B::C)](core-language/15-nested-namespace.md) | Language feature | Namespace shorthand | **Low** |
| if/switch init statements | Language feature | Declare variables inside conditionals | **Medium** |

### C++20

C++20 is the biggest update since C++11: the four headline features — Concepts, Ranges, Coroutines, and Modules — thoroughly changed how we do template programming, data pipelines, asynchronous flows, and code organization. On top of that, features like std::format, std::span, and three-way comparison noticeably improved day-to-day development. Compiler support requirements are relatively high (GCC 10+ / Clang 10+).

| Feature | Header | Summary | Applicability |
|---------|--------|---------|---------------|
| [Concepts](templates/01-concepts.md) | `<concepts>` | Compile-time constraints on template parameters | **High** |
| Ranges | `<ranges>` | Composable ranges and views | **High** |
| [std::span](containers/01-span.md) | `<span>` | Non-owning view of contiguous sequences | **High** |
| [std::format](containers/07-format.md) | `<format>` | Type-safe formatted output | **High** |
| [std::jthread](concurrency/04-jthread.md) | `<thread>` | Thread class that joins automatically | **High** |
| [Three-way comparison (<=>)](core-language/12-spaceship-operator.md) | `<compare>` | Unified comparison operators | **High** |
| [Coroutines](core-language/16-coroutines.md) | `<coroutine>` | Stackless coroutines | **High** |
| [Modules](core-language/17-modules.md) | Language feature | Compilation units replacing headers | **High** |
| consteval | Language feature | Forces compile-time evaluation | **Medium** |
| constinit | Language feature | Compile-time initialization of static variables | **Medium** |
| std::source_location | `<source_location>` | Compile-time source location information | **Medium** |
| Designated initializers | Language feature | Initialize aggregates by member name | **Medium** |
| std::atomic_ref | `<atomic>` | Atomic operations through references | **Medium** |
| std::latch / barrier | `<latch>` | Thread synchronization primitives | **Medium** |
| std::stop_token | `<stop_token>` | Cooperative thread cancellation | **Medium** |
| std::erase / erase_if | `<vector>` etc. | Unified interface for erasing container elements | **Medium** |
| std::is_constant_evaluated | `<type_traits>` | Detects constant-evaluation contexts | **Medium** |
| range-for init statements | Language feature | range-for with an initializer | **Low** |

### C++23

C++23 polished C++20 and filled in its gaps: practical library components such as std::expected, std::print, std::generator, and std::flat_map closed key blanks, and language improvements like deducing this slimmed down how member functions are written. Compiler support for some features is still in progress (GCC 14+ / Clang 18+).

| Feature | Header | Summary | Applicability |
|---------|--------|---------|---------------|
| [std::expected](memory/05-expected.md) | `<expected>` | Wrapper type for error handling | **Medium** |
| [std::print / println](containers/10-print.md) | `<print>` | Formatted output to stdout | **High** |
| [std::generator](containers/09-generator.md) | `<generator>` | Synchronous coroutine generator | **Medium** |
| [std::flat_map / flat_set](containers/08-flat-map.md) | `<flat_map>` | Ordered containers backed by contiguous storage | **Medium** |
| [std::mdspan](containers/11-mdspan.md) | `<mdspan>` | Non-owning view of multidimensional arrays | **Medium** |
| [std::stacktrace](core-language/19-stacktrace.md) | `<stacktrace>` | Capture and print the call stack | **Medium** |
| [deducing this](core-language/18-deducing-this.md) | Language feature | Explicit object parameter deduction | **Medium** |
| std::to_underlying | `<utility>` | Converts an enum to its underlying type | **Medium** |
| std::out_ptr / inout_ptr | `<memory>` | Smart pointer interop with C pointers | **Medium** |
| optional monadic operations | `<optional>` | and_then / or_else / transform | **Medium** |
| New Ranges adaptors | `<ranges>` | zip / chunk / slide / enumerate, etc. | **Medium** |
| if consteval | Language feature | Conditional check for constant evaluation | **Low** |
| std::unreachable | `<utility>` | Marks unreachable code | **Low** |
| Multidimensional subscript operator | Language feature | operator[] accepts multiple arguments | **Low** |
| std::is_scoped_enum | `<type_traits>` | Detects scoped enumeration types | **Low** |

### C++26

C++26 is the next standard, currently under development (ISO/IEC 14882:2026). As of mid-2026, most features are still at the working-draft stage, with preliminary or only partial compiler support, and their final inclusion and wording may still change. Only high-certainty directions are listed here; **for compiler support, defer to the [up-to-date cppreference table](https://en.cppreference.com/w/cpp/compiler_support/26)**.

| Feature | Header | Summary | Applicability |
|---------|--------|---------|---------------|
| Reflection (static reflection) | `<meta>` (draft) | Compile-time type introspection and generation, the C++26 flagship feature | **Medium** |
| Contracts | `<contracts>` (draft) | precondition/postcondition/assert runtime contract checks | **Medium** |
| std::execution / Senders | `<execution>` (draft) | scheduler/sender/receiver asynchronous task graphs (P2300) | **Medium** |
| std::linalg | `<linalg>` | BLAS-based linear algebra free functions | **Medium** |
| std::text_encoding | `<text_encoding>` | Detects the runtime text encoding | **Low** |
| Hazard pointers / RCU | `<hazard_pointer>` `<rcu>` | Lock-free concurrency primitives | **Low** |

::: warning C++26 is still a draft
Compiler support for the features above is uneven: GCC 16 has partial implementations of reflection, contracts, and linalg; none of the three major standard libraries ships a usable std::execution yet (you have to run NVIDIA/stdexec for that); most of the rest are still experimental. Do not rely on it in production, but studying it for a preview is fine. See the [cppreference C++26 compiler support table](https://en.cppreference.com/w/cpp/compiler_support/26).
:::

## By Functional Category

### Memory Management

Memory- and resource-management features such as smart pointers, optional values, and error handling. See the [Memory Management cheat sheet](memory/index.md) for details.

| Feature | Version | Header | Summary | Applicability |
|------|------|--------|------|--------|
| [std::unique_ptr](memory/01-unique-ptr.md) | C++11 | `<memory>` | Smart pointer with exclusive ownership | **High** |
| [std::shared_ptr](memory/02-shared-ptr.md) | C++11 | `<memory>` | Smart pointer with shared ownership | **Medium** |
| std::weak_ptr | C++11 | `<memory>` | Breaks shared_ptr reference cycles | **Medium** |
| [std::make_unique](memory/04-make-unique.md) | C++14 | `<memory>` | Exception-safe creation of unique_ptr | **High** |
| [std::optional](memory/03-optional.md) | C++17 | `<optional>` | Optional-value wrapper | **High** |
| std::pmr memory resources | C++17 | `<memory_resource>` | Memory resources for polymorphic allocators | **Medium** |
| [std::expected](memory/05-expected.md) | C++23 | `<expected>` | Wrapper type for error handling | **Medium** |
| std::out_ptr / inout_ptr | C++23 | `<memory>` | Smart pointer interop with C pointers | **Medium** |
| optional monadic operations | C++23 | `<optional>` | and_then / or_else / transform | **Medium** |

::: details Cheat sheets still pending
Cheat sheets have not been created yet for: std::weak_ptr, std::pmr memory resources, std::out_ptr / inout_ptr, optional monadic operations
:::

### Containers and Views

Features for organizing and manipulating data: standard containers, views, strings, formatting, algorithms, and more. See the [Containers and Views cheat sheet](containers/index.md) for details.

| Feature | Version | Header | Summary | Applicability |
|------|------|--------|------|--------|
| STL containers (vector, list, deque, map, set...) | C++98 | `<vector>` etc. | Sequential/associative/unordered container families | **High** |
| STL algorithms | C++98 | `<algorithm>` | Sorting/searching/transformation and other generic algorithms | **High** |
| std::string | C++98 | `<string>` | Variable-length string | **High** |
| [std::array](containers/04-array.md) | C++11 | `<array>` | Compile-time fixed-size array | **High** |
| std::tuple | C++11 | `<tuple>` | Heterogeneous fixed-size container | **Medium** |
| std::unordered_map / set | C++11 | `<unordered_map>` | Hash-table containers | **Medium** |
| std::function | C++11 | `<functional>` | Polymorphic function wrapper | **Medium** |
| [std::string_view](containers/02-string-view.md) | C++17 | `<string_view>` | Zero-copy string view | **High** |
| [std::variant](containers/03-variant.md) | C++17 | `<variant>` | Type-safe union | **Medium** |
| std::any | C++17 | `<any>` | Type-safe container for any value | **Low** |
| [std::filesystem](containers/06-filesystem.md) | C++17 | `<filesystem>` | Filesystem operations | **Medium** |
| Ranges | C++20 | `<ranges>` | Composable ranges and views | **High** |
| [std::span](containers/01-span.md) | C++20 | `<span>` | Non-owning view of contiguous sequences | **High** |
| [std::format](containers/07-format.md) | C++20 | `<format>` | Type-safe formatted output | **High** |
| std::erase / erase_if | C++20 | `<vector>` etc. | Unified interface for erasing container elements | **Medium** |
| [std::flat_map / flat_set](containers/08-flat-map.md) | C++23 | `<flat_map>` | Ordered containers backed by contiguous storage | **Medium** |
| [std::generator](containers/09-generator.md) | C++23 | `<generator>` | Synchronous coroutine generator | **Medium** |
| [std::print / println](containers/10-print.md) | C++23 | `<print>` | Formatted output to stdout | **High** |
| [std::mdspan](containers/11-mdspan.md) | C++23 | `<mdspan>` | Non-owning view of multidimensional arrays | **Medium** |
| New Ranges adaptors | C++23 | `<ranges>` | zip / chunk / slide / enumerate, etc. | **Medium** |

::: details Cheat sheets still pending
Cheat sheets have not been created yet for: STL containers, STL algorithms, std::string, std::tuple, std::unordered_map/set, std::function, std::any, Ranges, std::erase/erase_if, new Ranges adaptors

### Concurrency

Concurrency and multithreading features: threads, locks, atomic operations, synchronization primitives, and more. See the [Concurrency cheat sheet](concurrency/index.md) for details.

| Feature | Version | Header | Summary | Applicability |
|------|------|--------|------|--------|
| [std::thread](concurrency/02-thread.md) | C++11 | `<thread>` | Platform-independent threads | **High** |
| [std::mutex / lock_guard](concurrency/03-mutex.md) | C++11 | `<mutex>` | Mutexes and RAII locking | **High** |
| [std::atomic](concurrency/01-atomic.md) | C++11 | `<atomic>` | Lock-free atomic operations | **High** |
| std::condition_variable | C++11 | `<condition_variable>` | Condition-variable synchronization | **Medium** |
| std::future / async | C++11 | `<future>` | Asynchronous tasks and result retrieval | **Medium** |
| std::chrono | C++11 | `<chrono>` | Time library | **High** |
| std::shared_timed_mutex | C++14 | `<shared_mutex>` | Timed shared mutex | **Low** |
| std::shared_mutex | C++17 | `<shared_mutex>` | Reader-writer lock | **Medium** |
| [std::jthread](concurrency/04-jthread.md) | C++20 | `<thread>` | Thread class that joins automatically | **High** |
| std::atomic_ref | C++20 | `<atomic>` | Atomic operations through references | **Medium** |
| std::latch / barrier | C++20 | `<latch>` | Thread synchronization primitives | **Medium** |
| std::stop_token | C++20 | `<stop_token>` | Cooperative thread cancellation | **Medium** |

::: details Cheat sheets still pending
Cheat sheets have not been created yet for: std::condition_variable, std::future / async, std::chrono, std::shared_timed_mutex, std::shared_mutex, std::atomic_ref, std::latch / barrier, std::stop_token

### Core Language Features

Core language features: keywords, syntactic sugar, the type system, compile-time mechanisms, and more. See the [Core Language Features cheat sheet](core-language/index.md) for details.

| Feature | Version | Header | Summary | Applicability |
|------|------|--------|------|--------|
| RAII (construction/destruction/copy) | C++98 | Language feature | Resource acquisition is initialization | **High** |
| Exception handling | C++98 | Language feature | Structured error handling | **Medium** |
| Namespaces | C++98 | Language feature | Prevents name collisions | **High** |
| Operator overloading | C++98 | Language feature | Custom operator behavior for user types | **Medium** |
| iostream | C++98 | `<iostream>` | Type-safe I/O streams | **Medium** |
| [Lambda expressions](core-language/02-lambda.md) | C++11 | Language feature | Anonymous function objects | **High** |
| [auto](core-language/03-auto-decltype.md) | C++11 | Language feature | Automatic type deduction | **High** |
| [decltype](core-language/03-auto-decltype.md) | C++11 | Language feature | Queries the type of an expression | **High** |
| [constexpr](core-language/01-constexpr.md) | C++11 | Language feature | Compile-time constants and functions | **High** |
| [Range-based for](core-language/07-range-for.md) | C++11 | Language feature | Syntactic sugar for container traversal | **High** |
| Move semantics (rvalue references) | C++11 | Language feature | Transfers resources instead of copying | **High** |
| [std::move / std::forward](core-language/08-move-forward.md) | C++11 | `<utility>` | Utilities for moving and perfect forwarding | **High** |
| [nullptr](core-language/04-nullptr.md) | C++11 | Language feature | Type-safe null pointer constant | **High** |
| [enum class](core-language/05-enum-class.md) | C++11 | Language feature | Scoped, strongly typed enumerations | **High** |
| [override / final](core-language/06-override-final.md) | C++11 | Language feature | Explicit marking of virtual overrides | **High** |
| static_assert | C++11 | Language feature | Compile-time assertions | **High** |
| User-defined literals | C++11 | Language feature | Custom literal suffixes | **Medium** |
| Delegating / inheriting constructors | C++11 | Language feature | Constructor reuse | **Medium** |
| alignas / alignof | C++11 | Language feature | Alignment control and query | **Medium** |
| [Generic lambdas](core-language/09-generic-lambda.md) | C++14 | Language feature | auto parameters in lambdas | **High** |
| Return type deduction | C++14 | Language feature | auto deduction of function return values | **Medium** |
| Relaxed constexpr | C++14 | Language feature | Relaxed constexpr restrictions | **High** |
| decltype(auto) | C++14 | Language feature | Return type deduction for perfect forwarding | **Medium** |
| Binary literals | C++14 | Language feature | Binary integers with the 0b prefix | **Medium** |
| [Structured bindings](core-language/11-structured-binding.md) | C++17 | Language feature | Destructuring multiple return values | **High** |
| [if constexpr](core-language/13-if-constexpr.md) | C++17 | Language feature | Compile-time conditional branches | **High** |
| CTAD | C++17 | Language feature | Class template argument deduction | **High** |
| Guaranteed copy elision | C++17 | Language feature | Mandated elision of temporary copies | **High** |
| [Inline variables](core-language/14-inline-variables.md) | C++17 | Language feature | Define global variables in headers | **Medium** |
| std::byte | C++17 | `<cstddef>` | Standalone byte type | **Medium** |
| [Nested namespaces](core-language/15-nested-namespace.md) | C++17 | Language feature | A::B::C shorthand | **Low** |
| if/switch init statements | C++17 | Language feature | Declare variables inside conditionals | **Medium** |
| [Three-way comparison (<=>)](core-language/12-spaceship-operator.md) | C++20 | `<compare>` | Unified comparison operators | **High** |
| [Coroutines](core-language/16-coroutines.md) | C++20 | `<coroutine>` | Stackless coroutines | **High** |
| [Modules](core-language/17-modules.md) | C++20 | Language feature | Compilation units replacing headers | **High** |
| consteval | C++20 | Language feature | Forces compile-time evaluation | **Medium** |
| constinit | C++20 | Language feature | Compile-time initialization of static variables | **Medium** |
| std::source_location | C++20 | `<source_location>` | Compile-time source location | **Medium** |
| Designated initializers | C++20 | Language feature | Initialize aggregates by member name | **Medium** |
| [deducing this](core-language/18-deducing-this.md) | C++23 | Language feature | Explicit object parameter deduction | **Medium** |
| std::to_underlying | C++23 | `<utility>` | Converts an enum to its underlying type | **Medium** |
| std::unreachable | C++23 | `<utility>` | Marks unreachable code | **Low** |
| if consteval | C++23 | Language feature | Conditional check for constant evaluation | **Low** |
| Multidimensional subscript operator | C++23 | Language feature | operator[] with multiple arguments | **Low** |
| [std::stacktrace](core-language/19-stacktrace.md) | C++23 | `<stacktrace>` | Capture and print the call stack | **Medium** |

::: details Cheat sheets still pending
Cheat sheets have not been created yet for: move semantics, static_assert, user-defined literals, delegating/inheriting constructors, alignas / alignof, return type deduction, relaxed constexpr, decltype(auto), binary literals, CTAD, guaranteed copy elision, std::byte, if/switch init statements, consteval, constinit, std::source_location, designated initializers, std::to_underlying, std::unreachable, if consteval, multidimensional subscript operator

### Templates and Metaprogramming

Generic programming and metaprogramming features: templates, constraints, type traits, compile-time computation, and more. See the [Templates and Metaprogramming cheat sheet](templates/index.md) for details.

| Feature | Version | Header | Summary | Applicability |
|------|------|--------|------|--------|
| Class templates / function templates | C++98 | Language feature | Foundation of generic programming | **High** |
| [Variadic templates](templates/02-variadic-templates.md) | C++11 | Language feature | Any number of template parameters | **High** |
| [std::initializer_list](containers/05-initializer-list.md) | C++11 | `<initializer_list>` | Uniform initializer lists | **High** |
| std::integer_sequence | C++14 | `<utility>` | Compile-time integer sequences | **Medium** |
| [Fold expressions](templates/03-fold-expressions.md) | C++17 | Language feature | Operations over expanded parameter packs | **High** |
| std::invoke | C++17 | `<functional>` | Unified invocation interface | **Medium** |
| std::apply | C++17 | `<tuple>` | Expands a tuple into function arguments | **Medium** |
| [Concepts](templates/01-concepts.md) | C++20 | `<concepts>` | Compile-time constraints on template parameters | **High** |
| std::is_constant_evaluated | C++20 | `<type_traits>` | Detects constant-evaluation contexts | **Medium** |
| std::is_scoped_enum | C++23 | `<type_traits>` | Detects scoped enumeration types | **Low** |

::: details Cheat sheets still pending
Cheat sheets have not been created yet for: std::integer_sequence, std::invoke, std::apply, std::is_constant_evaluated, std::is_scoped_enum

---

*Some content is referenced from [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
