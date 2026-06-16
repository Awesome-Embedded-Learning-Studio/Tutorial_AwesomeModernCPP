---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Here is the translation:


  "Explores when to choose C++ over C, and how to wisely use C++ features in embedded
  environments, covering recommended, cautious, and prohibited features.'
difficulty: beginner
order: 4
platform: host
prerequisites: []
reading_time_minutes: 16
related: []
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: When to Use C++ and Which Features to Use
translation:
  source: documents/vol1-fundamentals/04-when-to-use-cpp.md
  source_hash: c593167374386fb7fe98148866a8c5ec733e3a59dc2b75119a227e26c7b229ca
  translated_at: '2026-06-16T03:32:31.085030+00:00'
  engine: anthropic
  token_count: 2627
---
# When to Use C++ and Which Features to Use

Honestly, whenever I see a "C vs C++" holy war break out in the embedded community, I feel pretty helpless. The debate often quickly slides into the realm of faith—C users think C++ is a cult, and C++ users think C is primitive. But the real question is: For this project, on this hardware, is using this language cost-effective? No one can answer that for you, but I can share experience gained from actual projects to help you avoid some detours.

In this chapter, we need to clarify two things: First, what kind of project is worth upgrading to C++; second, once we use C++, which features should we use boldly, which should we use with caution, and which should be avoided if possible.

## When is C++ Worth the Effort

Let's start with the major premise: If your project code scale exceeds tens of thousands of lines and involves multiple subsystems that need clear interface boundaries, then the advantages of C++ will start to become apparent. In C, maintaining projects of this scale is certainly doable, but you need the team to invest a lot of energy in maintaining the code structure—manually managing module division, hand-writing interface abstractions, and manually ensuring type safety. C++ classes, namespaces, and templates do exactly these things at the language level. Especially when multiple subsystems require strict interface definitions, C++'s type system can block a large number of interface misuse errors at compile time, whereas in C, these often only expose themselves at runtime.

Type safety is directly a matter of life and death in safety-critical systems. Automotive electronics, medical equipment, aerospace—in these fields, the implicit type conversions and loose typing common in C are simply ticking time bombs. C++'s strong type system, `enum class`, reference semantics, and `constexpr` correctness can prevent a large number of low-level errors from the compiler level. This is not some fancy theoretical advantage, but a tangible reduction in the probability of bugs entering the product.

Code reuse demand is also an important consideration. If your project needs to reuse components across multiple product lines, or has a large number of similar but not identical functional modules, C++'s template mechanism can be powerful—it generates type-safe code at compile time with zero runtime overhead. Compared to the "generics" cobbled together by macros and `void*` in C, C++'s solution is both safe and elegant.

But to be fair, the prerequisite for using C++ is that the team has the corresponding technical reserve. If the entire team has only written C, hasn't heard of RAII, and has no mechanism for training and code review, then rashly using C++ will most likely lead to a crash. Conversely, if the team has members familiar with modern C++ practices who can formulate and execute reasonable coding standards, then the advantages of C++ can truly be realized.

### When C is Still the Better Choice

Conversely, in some scenarios, sticking with C is the more pragmatic choice. When the target platform is extremely resource-constrained—for example, low-cost MCUs with Flash less than 32KB and RAM less than 4KB—the simplicity and predictability of C are its greatest advantages. For simple applications with small code size (e.g., less than five thousand lines), introducing C++ adds unnecessary complexity. Additionally, if the project requires deep integration with a large amount of legacy C code, or if the target platform's toolchain has incomplete support for C++ (which is not uncommon on some niche chips), continuing with C is often the most worry-free decision.

## Our Good Friends: Recommended Core Features

Alright, assuming you've decided to go with C++. Next, we need to figure out which features should become the basic toolbox for daily development. These features all conform to the principle of zero-overhead abstraction—enjoying better code organization without paying a runtime price.

### Classes and Encapsulation

Classes and encapsulation are one of the most basic and valuable features of C++. Let's look directly at a practical example—a sensor driver. In C, you might be used to writing this:

```c
// sensor.c
static volatile uint32_t* const SENSOR_ADDR = (uint32_t*)0x40000000;

void sensor_init() {
    *SENSOR_ADDR = 0x01;
}

uint32_t sensor_read() {
    return *SENSOR_ADDR;
}
```

The problem with this approach is obvious: `SENSOR_ADDR` is a global variable, and any place can directly manipulate it; no one can stop it. The C++ approach is to encapsulate the register address and access logic inside the class, exposing only `init` and `read` interfaces:

```cpp
// sensor.hpp
class Sensor {
    static constexpr volatile uint32_t* const ADDR =
        reinterpret_cast<volatile uint32_t*>(0x40000000);

public:
    void init() const {
        *ADDR = 0x01;
    }

    [[nodiscard]] uint32_t read() const {
        return *ADDR;
    }
};
```

The key is that the code generated by the compiler is almost indistinguishable from the machine code of the C version above—member functions are inline by default, so there is no loss in performance. However, external code can no longer touch the registers directly, drastically compressing the possibility of errors.

### Namespaces

Naming conflicts in large projects are a headache, especially when you integrate several third-party libraries. The traditional C approach is to add prefixes to function names, like `sensor_init()`, `timer_init()`, `uart_init()`, which works but isn't elegant. C++ namespaces provide a more systematic solution—organizing related functions and classes into logical groups, fundamentally eliminating naming conflicts:

```cpp
namespace Drivers {
    void init();
    void transfer();
}

namespace Utils {
    void init();  // No conflict with Drivers::init
    void log();
}
```

Best of all, namespaces are a pure compile-time feature and produce no runtime overhead.

### Reference Semantics

Compared to pointers, references have two key advantages: first, references cannot be null, so there is no need for null pointer checks; second, reference syntax more clearly expresses the function's intent. When we need to pass a large struct but don't want to copy, a `const` reference is both efficient and safe; when a function needs to modify the passed argument, a non-`const` reference clearly indicates this intent:

```cpp
void update_config(Config& cfg) {
    cfg.baud_rate = 115200;
}

void print_status(const Config& cfg) {
    printf("Baud: %d\n", cfg.baud_rate);
}
```

Compared to the C pointer approach, we save the `nullptr` check, and the code is more concise. Underlying implementation, references are usually just pointers, so there is no additional performance overhead.

### Compile-Time Calculation (constexpr)

`constexpr` is a killer feature of modern C++ in embedded development. It allows the compiler to complete calculations during the compilation phase, and the generated code directly contains the result value, with zero runtime overhead. For example, calculating the serial port baud rate divisor:

```cpp
constexpr uint32_t calculate_baud_div(uint32_t clock, uint32_t baud) {
    return clock / baud;
}

constexpr uint32_t UART_DIV = calculate_baud_div(72000000, 115200);
```

The traditional approach is to do division at runtime, while with a `constexpr` function, this division is completed at compile time. When the program runs, the value of `UART_DIV` is already `625`, requiring no calculation. This not only improves performance but also makes the intent of the code clearer. It can even be used directly as an array size:

```cpp
std::array<uint32_t, UART_DIV> buffer;
```

### Strongly Typed Enums (enum class)

Traditional C enums have several annoying problems: they implicitly convert to integers, values between different enums can be mixed, and enum names pollute the outer scope. `enum class` introduced in C++11 solves all these problems at once:

```cpp
enum class Periph : uint32_t {
    UART1 = 0x40011000,
    UART2 = 0x40004400
};

void reset(Periph p) {
    *reinterpret_cast<volatile uint32_t*>(p) = 0;
}
```

Now if you try to pass the wrong type, the compiler will directly report an error, instead of silently accepting it like C enums and giving you a runtime surprise:

```cpp
reset(Periph::UART1);  // OK
reset(0x40011000);     // Compile error
```

Moreover, compilers usually optimize `enum class` into ordinary integers, so there is absolutely no loss in performance.

## Templates: Don't Swing the Sword Blindly

Templates are C++'s most powerful but also most easily abused feature. In embedded environments, we need to find a balance between code reuse and code bloat.

### Simple Templates: Use with Confidence

Simple function templates are usually safe because they are often inlined by the compiler, and the final generated code is exactly the same as a hand-written specific type version:

```cpp
template<typename T>
T clamp(T val, T min, T max) {
    return (val < min) ? min : (val > max) ? max : val;
}
```

### Class Templates: It Depends

Class templates are also useful in appropriate scenarios, with a typical example being a fixed-size ring buffer. By taking the element type and size as template parameters, we achieve a generic but zero-overhead buffer:

```cpp
template<typename T, size_t N>
class RingBuffer {
    T data[N];
    size_t head = 0, tail = 0;
public:
    bool push(T item) { /* ... */ }
    bool pop(T& item) { /* ... */ }
};
```

Since the size is determined at compile time, the compiler can fully optimize.

⚠️ But there is a pitfall to note: every different combination of template parameters generates a separate copy of the code. If you instantiate `RingBuffer<uint8_t, 128>` and `RingBuffer<uint8_t, 256>`, there will be two almost identical copies of the code in Flash. So use templates, but don't abuse them.

### SFINAE and if constexpr: Use but Don't Overcomplicate

More advanced template techniques, like SFINAE and type traits, should be used with caution in embedded environments. C++17's `if constexpr` is much clearer than traditional SFINAE; if you really need to select different implementations based on type, prioritize using it:

```cpp
template<typename T>
auto process(T val) {
    if constexpr (std::is_integral_v<T>) {
        return val * 2;
    } else {
        return val;
    }
}
```

Only consider these techniques when you truly need compile-time type constraints, and keep it as simple as possible. If template metaprogramming gets too complex in embedded systems, even you won't understand it two weeks later.

## Features Requiring a Disclaimer

Some C++ features are not unusable, but require extra care. The following features are like double-edged swords in embedded projects—used well, they are sharp tools; used poorly, they are time bombs.

### Constructors and Destructors

Simple, fast construction and destruction are perfectly fine. RAII-style resource management is the best example—acquire resources during construction and automatically release them during destruction, which is both safe and elegant:

```cpp
class LockGuard {
    Mutex& mtx;
public:
    explicit LockGuard(Mutex& m) : mtx(m) { mtx.lock(); }
    ~LockGuard() { mtx.unlock(); }
};
```

Usage is very simple; leaving the scope automatically releases it, and even if you `return` early, you won't forget to unlock:

```cpp
void critical_task() {
    LockGuard lock(mutex);
    // ... do critical stuff ...
    // Automatic unlock here
}
```

⚠️ But if you do dynamic memory allocation (`new`), call hardware initialization that might fail, or create objects requiring destruction in an interrupt context within the constructor, you are asking for trouble. The correct approach is to keep the constructor simple and use an explicit `init()` function to handle initialization that might fail:

```cpp
class SpiDriver {
public:
    SpiDriver() = default;  // Do nothing
    Error init() {          // Explicit initialization
        // Setup hardware...
        return Error::OK;
    }
};
```

Also, destructors must be marked `noexcept`—if an exception is thrown during destruction, the program will directly call `std::terminate`, which in embedded systems means a crash.

### Exception Handling: Disabled by Default

In embedded projects, my advice is to directly turn off exceptions via the `-fno-exceptions` compiler flag. This isn't bias—turning off exceptions can reduce code size by 10% to 30%, eliminate unpredictable execution time, and avoid the extra RAM overhead from stack unwinding. Moreover, many embedded toolchains have incomplete support for exceptions themselves, making debugging impossible if problems arise.

So what about error handling? Use error codes. While not as elegant as exceptions, they are predictable, efficient, and easy to analyze for worst-case scenarios:

```cpp
enum class Error { OK, Timeout, InvalidParam };

Error sensor_read(int& value) {
    if (!sensor_ready()) return Error::Timeout;
    value = *SENSOR_ADDR;
    return Error::OK;
}
```

For scenarios that need to return a value and an error status simultaneously, you can use a simple struct to separate the two:

```cpp
struct Result {
    Error err;
    int value;
};
```

⚠️ Unless your system has ample Flash and RAM, relaxed real-time requirements, full toolchain support for exceptions, and the team has extensive experience using exceptions—don't touch exceptions.

### RTTI: Turn It Off

Run-Time Type Information (RTTI) should also be disabled by default, using the `-fno-rtti` compiler flag. RTTI increases code size, requires extra metadata storage, and brings performance overhead. In the vast majority of embedded scenarios, if you need to determine the type, adding a type ID enum to the base class is sufficient; you don't need `dynamic_cast`.

### Virtual Functions: Restricted Use

Virtual functions provide runtime polymorphism and are indeed useful when designing driver abstraction layers. But the cost is real: every object containing virtual functions needs a virtual table pointer (vptr) (4 to 8 bytes), virtual function calls are 5% to 10% slower than direct calls, and they can hinder compiler inline optimization.

If you only need compile-time polymorphism, passing specific types via template parameters is sufficient, with zero runtime overhead. Virtual functions should only be used in scenarios that truly require runtime polymorphism, and avoid frequent calls on performance-critical paths.

## Features to Avoid If Possible

The following features are recommended to be avoided in embedded environments if possible.

### Dynamic Memory Allocation

`new`, `delete`, `malloc`, `free`—operations that are commonplace in desktop development are sources of risk in embedded systems. Heap fragmentation can cause memory allocation failures after running for a while, and such failures are extremely difficult to reproduce and debug. The unpredictable execution time of dynamic allocation also makes worst-case analysis impossible.

Alternatives are to use fixed-size data structures. The standard library's `std::array` is a safe choice; it involves no dynamic memory. If you need dynamically sized containers, you can implement static capacity versions, or use memory pools—pre-allocate a fixed number of equally sized memory blocks, where allocation and deallocation are both O(1) and fragmentation-free.

### Most STL Containers

`std::vector`, `std::list`, `std::map`, `std::string`—these containers all rely on dynamic memory allocation and are not suitable for embedded environments. `std::shared_ptr`'s reference counting involves atomic operations, which has significant overhead on some platforms. `std::iostream` should be completely avoided; a simple `printf` can introduce over 50KB of code.

But not all of the standard library is unusable. Algorithms in `<algorithm>` (note that some allocate temporary memory), compile-time tools like `<type_traits>`, and `std::array` and `std::span` in `<array>`—these are all zero-overhead or low-overhead good stuff.

If you really need containers, look at the Embedded Template Library (ETL), which provides fixed-size containers that don't use dynamic memory and are STL-compatible.

### Standard Multithreading Library

`std::thread`, `std::mutex`—these components have large code volume and rely on specific operating system support. In embedded systems, it is usually better to use primitives provided directly by the RTOS—FreeRTOS tasks, semaphores, and queues, or CMSIS-RTOS standard interfaces—these have been optimized for the embedded environment and occupy fewer resources.

## Final Words

Choosing the right features is just the first step. To truly implement it in a project, you need to establish clear coding standards, specifying what is allowed, what is forbidden, and what needs review. Code review should be standard, specifically watching for sneaky use of disabled features, templates that are too complex causing code bloat, and virtual functions appearing where they shouldn't. Static analysis tools can help us automatically detect many such problems; don't skip this step.

In terms of performance, periodically check the compiled binary size to ensure there is no unexpected bloat. Make actual measurements of performance-critical code paths; don't just rely on feeling. Compilers have many optimization options, but the effects need to be verified by actual measurement—don't try them directly in the production environment; make sure they work on the development board first.

Language choice is not a matter of faith, but an engineering problem. Let data speak, choose tools by module layer, establish and enforce constraints with automated means. Remember this one sentence: use the right tool for the right job, don't turn tools into beliefs.
