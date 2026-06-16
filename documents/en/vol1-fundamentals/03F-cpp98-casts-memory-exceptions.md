---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Precise use cases for the four C++ type conversion operators, managing
  dynamic objects with `new`/`delete` and placement new, exception handling mechanisms
  and trade-offs in embedded systems, and `inline` and `typedef`.
difficulty: intermediate
order: 3
platform: host
prerequisites:
- C++98面向对象：类与对象深度剖析
- C++98面向对象：继承与多态
reading_time_minutes: 19
related:
- 何时用C++、用哪些C++特性
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: 'C++98 Advanced: Type Conversions, Dynamic Memory, and Exception Handling'
translation:
  source: documents/vol1-fundamentals/03F-cpp98-casts-memory-exceptions.md
  source_hash: 9bf42f9da2591d7014d339be2b318ec6e38277bf32602e9e669a9a106e71c411
  translated_at: '2026-06-16T03:32:50.802543+00:00'
  engine: anthropic
  token_count: 3440
---
# C++98 Advanced: Type Conversions, Dynamic Memory, and Exception Handling

> The complete repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to visit and give it a Star to motivate the author if you like it.

In this chapter, we focus on several relatively "advanced" features in C++98: the four type conversion operators, dynamic memory management (`new`/`delete` and `placement new`), exception handling, and `inline` functions and `typedef`. While they are not strongly dependent on each other, they all require a basic understanding of classes as a prerequisite.

These features share a common characteristic: they are either enhancements to existing C mechanisms (type conversions replace C-style casts, `new`/`delete` replace `malloc`/`free`) or are completely new introductions to C++ (exception handling). Understanding their design intent and boundaries is a prerequisite for using modern C++ correctly.

## 1. C++ Type Conversion Operators

C++ provides four dedicated type conversion operators, which are safer and more explicit than the C-style cast `(Type)`. Each has specific use cases and constraints.

### 1.1 static_cast

`static_cast` is used for **type conversions known at compile time**. It is the most "gentle" of the four conversions—it performs no dangerous low-level reinterpreting, simply telling the compiler, "I know this conversion is reasonable, please execute it for me."

Applicable scenarios include: conversions between fundamental types (e.g., `int` to `double`), conversions between pointers or references with inheritance relationships (upcasting is always safe, downcasting requires the programmer to ensure safety), and conversions between `void*` and other pointer types.

```cpp
double d = 3.14;
int i = static_cast<int>(d); // Truncation, explicit conversion

class Base {};
class Derived : public Base {};
Derived d;
Base* b = static_cast<Base*>(&d); // Upcasting, safe
```

The safety of `static_cast` lies in its basic compile-time checking—if you attempt to convert between two completely unrelated pointer types (like `Base*` to `Unrelated*`), the compiler will report an error. For such cross-type low-level conversions, you need to use `reinterpret_cast`.

### 1.2 reinterpret_cast

`reinterpret_cast` performs the **lowest-level reinterpreting conversion**. It allows you to convert between almost any pointer types, or even between pointers and integers. As the name suggests, it merely "reinterprets" the meaning of a memory block—the compiler performs no safety checks.

In embedded systems, `reinterpret_cast` is the standard method for accessing hardware registers:

```cpp
// GPIO Register layout
struct GPIORegisters {
    volatile uint32_t MODER;   // Mode register
    volatile uint32_t OTYPER;  // Output type register
    // ...
};

// 0x40020000 is the base address of GPIOA on STM32F4
GPIORegisters* gpioa = reinterpret_cast<GPIORegisters*>(0x40020000);

// Configure PA5 as output
gpioa->MODER |= (1 << 10);
```

This usage is unavoidable in embedded development—you indeed need to treat a fixed memory address "as" a specific structure. However, the danger of `reinterpret_cast` lies right here: it completely bypasses the type system. If you provide the wrong address or mess up the structure layout, you bear the consequences entirely.

Another common use is converting function pointers, such as for interrupt vector tables:

```cpp
// Function pointer type for interrupt handlers
using IRQHandler = void(*)();

// Cast a raw address to a function pointer and call it
IRQHandler handler = reinterpret_cast<IRQHandler>(0x08000004);
handler();
```

### 1.3 dynamic_cast

`dynamic_cast` is used for **runtime type checking**, primarily for downcasting polymorphic types (classes with virtual functions). It checks at runtime if the conversion is safe—if safe, it returns the converted pointer; if not, it returns `nullptr` (pointer version) or throws a `std::bad_cast` exception (reference version).

```cpp
class Base {
public:
    virtual ~Base() = default;
};

class Derived : public Base {
    // ...
};

void process(Base* b) {
    // Runtime check: is b actually a Derived?
    if (Derived* d = dynamic_cast<Derived*>(b)) {
        // Safe to use Derived-specific features
    }
}
```

Note that `dynamic_cast` requires **RTTI (Runtime Type Information)** support. RTTI stores type information in every object with virtual functions, increasing code size and runtime overhead. Many embedded compilers disable RTTI by default to save resources—if your project uses the `-fno-rtti` compiler flag, `dynamic_cast` cannot be used.

Therefore, in embedded development, `dynamic_cast` is used far less frequently than the other three. If you really need to determine types in an inheritance hierarchy, there are usually better alternatives—such as defining a `type()` method in the base class or using the Visitor pattern.

### 1.4 const_cast

`const_cast` is used to **add or remove `const` or `volatile` attributes**. It is the only C++ cast operator that can do this—the other three cannot modify the `const` nature of an object.

The most common legitimate use is calling legacy C APIs with signatures that aren't `const`-correct:

```cpp
void legacy_c_function(char* buffer); // Does not modify buffer, but lacks const

void safe_wrapper(const std::string& s) {
    // legacy_c_function(s.c_str()); // Error: cannot convert const char* to char*

    // Tell the compiler: "I know this function doesn't actually modify it"
    legacy_c_function(const_cast<char*>(s.c_str()));
}
```

But there is an iron rule: **Removing the `const` attribute from a truly `const` object and modifying it is undefined behavior.** `const_cast` should only be used to remove "accidentally added" `const` attributes (e.g., passed via a `const` reference where the underlying object isn't `const`), not to bypass the compiler's protection of actual constants.

```cpp
const int ci = 10;
const_cast<int&>(ci) = 20; // Undefined behavior! ci is truly constant
```

### 1.5 Type Conversion Decision Guide

The choice of four conversions can be decided by a simple logic chain:

First, ask yourself: Do I need to remove `const` or `volatile`? If yes, use `const_cast`. Second, do I need low-level memory reinterpreting (e.g., integer address to pointer, between unrelated pointer types)? If yes, use `reinterpret_cast`—but be extremely careful. Third, do I need runtime type checking in an inheritance hierarchy with virtual functions? If yes, use `dynamic_cast`—but be aware of RTTI overhead. If none of the above apply, use `static_cast`—it covers the vast majority of daily type conversion needs.

**A practical principle is: prioritize `static_cast`, and only use the other three when you clearly know why you need them.** If you find yourself using `reinterpret_cast` or `const_cast` frequently, it may indicate a design flaw that warrants re-examination.

## 2. Dynamic Memory Management

### 2.1 new and delete

C++ provides the `new` and `delete` operators to replace C's `malloc` and `free`. To put it simply and imprecisely—`new` is a simple wrapper around `malloc` plus a call to the corresponding constructor, allowing you to initialize an object in-place on a block of memory of `sizeof` size; `delete` calls the destructor first, then reclaims the memory.

```cpp
// Allocate and construct an int
int* p = new int(42);
// ... use p ...
// Destroy and free
delete p;

// Allocate and construct an object
MyClass* obj = new MyClass(arg1, arg2);
// ... use obj ...
// Destroy and free
delete obj;
```

For arrays, you must use `new[]` and `delete[]` in pairs:

```cpp
int* arr = new int[10];
// ... use arr ...
delete[] arr;
```

**The key difference between `new`/`delete` and `malloc`/`free`** is that `new` calls the constructor and `delete` calls the destructor, whereas `malloc`/`free` only handle allocating and freeing raw memory, knowing nothing about object construction or destruction. This means if you use `malloc` to allocate memory for a C++ type, you must manually call placement `new` to construct the object, and manually call the destructor before freeing—this is error-prone and completely unnecessary.

A classic and dangerous error is mismatching `new` and `delete`:

```cpp
MyClass* arr = new MyClass[10];
delete arr; // WRONG! Should be delete[] arr
```

For fundamental types (like `int`), some platforms might "coincidentally" work without issue because the destructor of fundamental types is a no-op. However, for arrays of class types, `delete` (without `[]`) will only call the destructor for the first element, leaking the rest—if the destructor is responsible for releasing other resources (like nested dynamic memory), the consequences are severe. **Develop the habit of pairing: `new` with `delete`, `new[]` with `delete[]`.**

### 2.2 placement new

`placement new` allows you to **construct an object at a specified memory location**, rather than letting `new` find a new block of memory itself. In desktop development, this feature isn't used very often, but it is very valuable in embedded systems—it allows you to construct objects in pre-allocated memory pools, avoiding the standard heap.

```cpp
// Pre-allocated memory buffer (aligned)
alignas(std::string) unsigned char buffer[sizeof(std::string)];

// Construct string in buffer
std::string* str = new(buffer) std::string("Hello, World");

// Use it
std::cout << *str << std::endl;

// Manually call destructor
str->~std::string();
// Buffer can be reused or freed later
```

There are several points to note when using `placement new`. First, the alignment of the memory buffer must meet the object's requirements—`alignas` ensures this. Second, because the memory wasn't allocated via `new`, you cannot use `delete`—you must explicitly call the destructor to clean up the object state, then decide yourself when to reuse or free that memory block. Finally, explicit destructor calls are very rare in C++ and almost exclusively appear in `placement new` scenarios—normally, you never need to manually call a destructor.

In embedded systems, the most typical application of `placement new` is **fixed-size memory pools**:

```cpp
class MemoryPool {
public:
    MemoryPool() : head_(buffer) {
        // Link all blocks
        for (size_t i = 0; i < POOL_SIZE - 1; ++i) {
            blocks[i].next = &blocks[i + 1];
        }
        blocks[POOL_SIZE - 1].next = nullptr;
    }

    void* allocate() {
        if (!head_) return nullptr;
        Block* tmp = head_;
        head_ = head_->next;
        return tmp;
    }

    void deallocate(void* ptr) {
        if (!ptr) return;
        Block* block = static_cast<Block*>(ptr);
        block->next = head_;
        head_ = block;
    }

    template<typename T, typename... Args>
    T* create(Args&&... args) {
        void* mem = allocate();
        if (!mem) return nullptr;
        return new(mem) T(std::forward<Args>(args)...);
    }

    template<typename T>
    void destroy(T* ptr) {
        if (ptr) {
            ptr->~T();
            deallocate(ptr);
        }
    }

private:
    struct Block {
        Block* next;
        // Ensure block is large enough for any object we store
        alignas(std::max_align_t) unsigned char data[128];
    };

    Block* head_;
    static constexpr size_t POOL_SIZE = 10;
    Block blocks[POOL_SIZE];
    unsigned char buffer[0]; // Placeholder
};
```

The benefit of a memory pool is that the time overhead for allocation and deallocation is entirely predictable (just pointer movement), it produces no memory fragmentation, and avoids the degradation issues of the standard heap after long runtime. These characteristics are crucial in embedded systems.

## 3. Exception Handling

### 3.1 Basic Exception Handling

Exception handling provides a structured error handling mechanism that separates error handling code from normal logic. At the very least, the code looks cleaner. Later, we will discuss why exception handling is often prohibited in many cases.

The C++ exception handling paradigm is try-catch-throw: try to execute code, throw an exception when encountering an error, then catch and handle it.

```cpp
double divide(int a, int b) {
    if (b == 0) {
        throw std::runtime_error("Division by zero");
    }
    return static_cast<double>(a) / b;
}

void calculate() {
    try {
        std::cout << divide(10, 2) << std::endl;
        std::cout << divide(10, 0) << std::endl; // Throws
    } catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown error" << std::endl;
    }
}
```

`catch (...)` catches all types of exceptions and usually serves as a final fallback. The C++ standard library defines a series of exception classes derived from `std::exception`, such as `std::runtime_error`, `std::logic_error`, `std::bad_alloc`, etc. You can also define your own exception types by inheriting from these standard exception classes.

### 3.2 Exception Safety

Writing exception-safe code requires special attention to resource management. The core issue is: **If an exception is thrown in the middle of an operation, what happens to resources acquired before that point?**

```cpp
void risky_function() {
    int* p = new int(42);

    // If do_something() throws, p is never deleted
    do_something();

    delete p;
}
```

If `do_something()` throws an exception, the program flow jumps directly to the nearest `catch` block, and `delete p` is never executed—memory leak.

The most direct fix is to wrap it in try-catch:

```cpp
void risky_function() {
    int* p = new int(42);
    try {
        do_something();
    } catch (...) {
        delete p;
        throw; // Re-throw
    }
    delete p;
}
```

But this is ugly—every resource needing protection requires a try-catch block, and if there are multiple resources, the code becomes very complex. A better approach is to use RAII—use a class constructor to acquire resources and the destructor to release them:

```cpp
void safe_function() {
    std::unique_ptr<int> p(new int(42));
    do_something();
    // Destructor of p runs automatically when leaving scope
}
```

RAII is the core paradigm for resource management in C++. When an exception is thrown, the stack unwinding process automatically calls the destructors of all local objects—this guarantees resources are always correctly released. We will cover RAII in depth in a later chapter.

### 3.3 Exception Safety Levels

From an exception safety perspective, functions can be classified into three levels:

**No guarantee**: If an exception occurs, the object may be in an inconsistent state, and resources may leak. This is the worst case but also the most common—as long as you are using raw `new`/`delete` without RAII wrappers.

**Basic guarantee**: If an exception occurs, the object is in a valid but unspecified state, and no resources are leaked. All standard library containers provide at least the basic guarantee.

**Strong guarantee**: If an exception occurs, the operation is completely rolled back, and the object state is exactly the same as before the call. This is usually implemented via the "copy-and-swap" idiom.

In embedded development, **the basic guarantee is usually sufficient**. Pursuing the strong guarantee is ideal but often has a high implementation cost—you need to create a complete backup before every operation, which is not friendly for resource-constrained systems.

### 3.4 Exception Specifications

C++98 allowed specifying which exceptions a function might throw in its declaration:

```cpp
// This function can only throw int or double
void risky_function() throw(int, double);
```

However, this feature was deprecated in C++11. The reason is that its runtime checking mechanism (if the function throws an exception not in the list, `std::unexpected` is called) was considered too costly, and it was found to be of little help in practice. C++11 replaced this mechanism with the `noexcept` keyword—`noexcept` is simply a boolean promise: "this function will not throw exceptions," allowing the compiler to perform more aggressive optimizations.

### 3.5 Exception Handling in Embedded Systems

Using exceptions in embedded systems requires great caution. Here are several key issues.

**Code size**: Exception handling requires additional "unwind tables" and runtime support code, which significantly increases binary size. On small MCUs with only tens of KB of Flash, this can directly lead to insufficient space.

**Time uncertainty**: When an exception occurs, the time required to handle it is completely unpredictable—it depends on the depth of the call stack, the number of objects needing destruction, and other factors. In embedded real-time systems where real-time performance is critical, this uncertainty is unacceptable.

**Implicit control flow**: Exceptions introduce an "invisible goto"—any function call might exit early due to an exception, making the code's execution path harder to reason about.

Therefore, many embedded projects choose to completely disable exceptions (using the `-fno-exceptions` compiler flag), opting instead for return values or error codes for error handling:

```cpp
ErrorStatus peripheral_init() {
    if (clock_enable_failed()) {
        return ErrorStatus::CLOCK_ERROR;
    }
    if (gpio_config_failed()) {
        return ErrorStatus::GPIO_ERROR;
    }
    return ErrorStatus::OK;
}
```

In modern C++, `std::optional` (C++17) and `std::expected` (C++23) provide more elegant solutions than raw error codes—they can express "operation failed" without introducing the runtime overhead of exceptions. The author uses these solutions in actual projects.

## 4. Inline Functions

### 4.1 The True Meaning of inline

In C, we use macros to define short "functions":

```c
#define SQUARE(x) ((x) * (x))
int a = 5;
int b = SQUARE(a++); // a is incremented twice! Result is undefined
```

The problems with macros are well-known: no type checking, parameters may be evaluated multiple times (`a++` increments twice), and macro content is invisible during debugging. C++'s `inline` functions solve all these problems:

```cpp
inline int square(int x) {
    return x * x;
}
```

The original intent of the `inline` keyword was to suggest to the compiler "embed the function body directly at the call site, rather than generating a function call instruction." However, in modern compilers, this "advisory" function of `inline` is largely ignored—compilers have their own inlining strategies that are more accurate than the programmer's hint. The compiler decides whether to inline based on function complexity, call frequency, optimization level, and other factors, regardless of whether you wrote `inline`.

So what is `inline` still useful for? Its true value lies in **allowing the same function to be defined in multiple translation units without violating the ODR (One Definition Rule)**. As long as all definitions are identical, the linker knows they are the same function and won't report a "multiple definition" error. This is why we usually put the definition of `inline` functions in header files—every `.cpp` file that includes this header gets a copy of the definition, but only one is retained at link time.

### 4.2 Implicit inline for In-Class Definitions

Member functions defined directly inside a class definition are **implicitly `inline`**:

```cpp
class MyClass {
public:
    void inline_function() {
        // This function is implicitly inline
    }
};
```

### 4.3 inline Functions in Embedded Systems

In embedded development, `inline` functions are particularly suitable for replacing macros that manipulate registers:

```cpp
// Register access macros
#define SET_BIT(reg, bit) ((reg) |= (1U << (bit)))

// Better inline function version
inline void set_bit(volatile uint32_t& reg, int bit) {
    reg |= (1U << bit);
}
```

Compared to macros, `inline` functions have type checking, avoid multiple parameter evaluation issues, and provide full information in the debugger. In terms of performance, there is usually no difference—the compiler will expand the `inline` function into machine code similar to a macro.

## 5. Type Aliases (typedef)

### 5.1 Basic Usage

Besides C's `typedef`, C++'s `typedef` usage hasn't changed essentially, but in C++ there is a better alternative (C++11's `using`):

```cpp
typedef unsigned int uint32_t; // C style
typedef void (*FunctionPtr)(int); // Function pointer

// C++98 style
typedef std::vector<int> IntVector;
```

### 5.2 Preview: using Aliases

C++11 introduced the `using` keyword to create type aliases. Its functionality is completely equivalent to `typedef`, but the syntax is more intuitive—especially when defining function pointers and template aliases:

```cpp
// C++11 using syntax
using uint32_t = unsigned int;
using FunctionPtr = void(*)(int);

// Template alias (typedef cannot do this)
template<typename T>
using IntVector = std::vector<T>;
```

`using` also supports template aliases (which `typedef` cannot do):

```cpp
template<typename T>
using Matrix = std::vector<std::vector<T>>;
```

In C++98, you can only use `typedef`. If your project has migrated to C++11 or higher, it is recommended to use `using` exclusively for new code—its syntax is clearer and its functionality is more powerful.

## Summary

In this chapter, we learned several advanced features of C++98. The four type conversion operators each have specific use cases: `static_cast` covers daily needs, `reinterpret_cast` is for low-level memory operations, `dynamic_cast` is for runtime type checking, and `const_cast` is for adjusting const attributes. `new`/`delete` and `placement new` provide more complete dynamic memory management capabilities than `malloc`/`free`. While exception handling is powerful, its use in embedded systems requires careful trade-offs. `inline` functions and `typedef` serve as safe replacements for C macros and type aliases.

At this point, we have completed learning all the basic features of C++98. In subsequent chapters, we will enter the world of Modern C++—exploring what improvements and alternatives C++11 and later standards have brought to these "old features."
