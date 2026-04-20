---
title: 'C++98 Advanced: Type Conversions, Dynamic Memory, and Exception Handling'
description: Precise use cases for the four C++ type casting operators, managing dynamic
  objects with `new`/`delete` and placement new, exception handling mechanisms and
  embedded trade-offs, and `inline` and `typedef`
chapter: 0
order: 3
tags:
- cpp-modern
- host
- intermediate
- 进阶
difficulty: intermediate
reading_time_minutes: 25
prerequisites:
- C++98面向对象：类与对象深度剖析
- C++98面向对象：继承与多态
related:
- 何时用C++、用哪些C++特性
cpp_standard:
- 11
- 14
- 17
- 20
platform: host
translation:
  source: documents/vol1-fundamentals/03F-cpp98-casts-memory-exceptions.md
  source_hash: dcfc538a941ebfe4ed5f6119516b31472737563ff87dcd9a152dc548e2b5e36e
  translated_at: '2026-04-20T03:08:23.243560+00:00'
  engine: anthropic
  token_count: 3446
---
# C++98 Advanced: Type Conversions, Dynamic Memory, and Exception Handling

> The complete repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to check it out, and if you like it, give it a Star to encourage the author.

In this chapter, we focus on several relatively "advanced" features in C++98: the four type conversion operators, dynamic memory management (`new`/`delete` and `placement new`), exception handling, and `inline` functions and `typedef`. They don't have strong dependencies on each other, but all require a basic understanding of classes as a prerequisite.

These features share a common trait: they either enhance existing C mechanisms (type conversions replace C-style casts, `new`/`delete` replace `malloc`/`free`), or they are entirely new to C++ (exception handling). Understanding their design intent and applicable boundaries is a prerequisite for using modern C++ correctly.

## 1. C++ Type Conversion Operators

C++ provides four dedicated type conversion operators, which are safer and more explicit than C-style casts `(type)value`. Each has clear use cases and constraints.

### 1.1 static_cast

`static_cast` is used for **type conversions known at compile time**. It is the "gentlest" of the four conversions—it doesn't perform any dangerous low-level reinterpretation; it simply tells the compiler, "I know this conversion is reasonable, please execute it for me."

Applicable scenarios include: conversions between fundamental types (such as `int` to `float`), conversions between pointers or references with an inheritance relationship (upcasting is always safe, downcasting requires the programmer to ensure safety), and conversions between `void*` and other pointer types.

```cpp
// 基本类型转换
int i = 10;
float f = static_cast<float>(i);

// 指针类型转换
void* void_ptr = &i;
int* int_ptr = static_cast<int*>(void_ptr);

// 向上转换（派生类到基类，总是安全的）
class Base {};
class Derived : public Base {};
Derived d;
Base* base_ptr = static_cast<Base*>(&d);

// 向下转换（基类到派生类，程序员需确保安全）
Base b;
// Derived* derived_ptr = static_cast<Derived*>(&b);  // 危险！
```

The safety of `static_cast` lies in its basic compile-time checking—if you try to convert between two completely unrelated pointer types (such as `int*` to `float*`), the compiler will directly report an error. For this kind of cross-type low-level conversion, you need to use `reinterpret_cast`.

### 1.2 reinterpret_cast

`reinterpret_cast` performs the **lowest-level reinterpreting conversion**, allowing you to convert between almost any pointer types, and even between pointers and integers. As the name suggests, it simply "reinterprets" the meaning of a memory block—the compiler performs no safety checks.

In embedded systems, `reinterpret_cast` is the standard method for accessing hardware registers:

```cpp
// 定义外设基地址
#define PERIPH_BASE     0x40000000UL
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)
#define GPIOA_BASE      (AHB1PERIPH_BASE + 0x0000UL)

// 定义寄存器结构
typedef struct {
    volatile uint32_t MODER;    // 模式寄存器
    volatile uint32_t OTYPER;   // 输出类型寄存器
    volatile uint32_t OSPEEDR;  // 输出速度寄存器
    volatile uint32_t PUPDR;    // 上拉/下拉寄存器
    volatile uint32_t IDR;      // 输入数据寄存器
    volatile uint32_t ODR;      // 输出数据寄存器
    volatile uint32_t BSRR;     // 位设置/复位寄存器
} GPIO_TypeDef;

// 创建指向硬件的指针
#define GPIOA (reinterpret_cast<GPIO_TypeDef*>(GPIOA_BASE))

// 使用
GPIOA->MODER |= 0x01;  // 配置引脚模式
```

This usage is unavoidable in embedded development—you genuinely need to "treat" a fixed memory address as a certain structure. But note that the danger of `reinterpret_cast` lies exactly here: it completely bypasses the type system, and if you provide the wrong address or mess up the struct layout, you bear the full consequences.

Another common use case is converting function pointers, such as in an interrupt vector table:

```cpp
typedef void (*ISR_Handler)(void);

void timer_isr() {
    // 中断处理代码
}

uint32_t isr_address = reinterpret_cast<uint32_t>(timer_isr);
```

### 1.3 dynamic_cast

`dynamic_cast` is used for **runtime type checking**, primarily for downcasting polymorphic types (classes containing virtual functions). It checks at runtime whether the conversion is safe—if safe, it returns the converted pointer; if unsafe, it returns `nullptr` (pointer version) or throws a `std::bad_cast` exception (reference version).

```cpp
class Base {
public:
    virtual ~Base() {}  // 必须有虚函数才能使用 dynamic_cast
};

class Derived : public Base {
public:
    void derived_specific_method() {}
};

Base* base_ptr = new Derived();
Derived* derived_ptr = dynamic_cast<Derived*>(base_ptr);
if (derived_ptr != nullptr) {
    derived_ptr->derived_specific_method();
}
```

Note that `dynamic_cast` requires **RTTI (Runtime Type Information)** support. RTTI stores type information in every object with virtual functions, which increases code size and runtime overhead. Many embedded compilers disable RTTI by default to save resources—if your project uses the `-fno-rtti` compiler flag, `dynamic_cast` cannot be used.

Therefore, in embedded development, `dynamic_cast` is used far less frequently than the other three conversions. If you truly need to perform type checking within an inheritance hierarchy, there are usually better alternatives—such as defining a `type()` method in the base class, or using the Visitor pattern.

### 1.4 const_cast

`const_cast` is used to **add or remove the `const` or `volatile` qualifier**. It is the only C++ conversion operator that can do this—the other three cannot alter the `const` nature of an object.

The most common legitimate use case is calling legacy C APIs whose signatures aren't very `const`-friendly:

```cpp
// 遗留 C 函数：参数应该是 const 的，但当时没写
void legacy_uart_send(uint8_t* data, size_t length);

class UARTWrapper {
public:
    void send(const uint8_t* data, size_t length) {
        // 我们知道 legacy_uart_send 不会修改数据
        // 但它的签名不正确
        legacy_uart_send(const_cast<uint8_t*>(data), length);
    }
};
```

But there is an ironclad rule: **removing the `const` qualifier from a truly `const` object and modifying it is undefined behavior (UB)**. `const_cast` should only be used to remove "accidentally added" `const` qualifiers (such as when passed via a `const` reference, but the underlying object itself is not `const`), not to bypass the compiler's protection of true constants.

```cpp
const int const_value = 100;
int* modifiable = const_cast<int*>(&const_value);
*modifiable = 200;  // 未定义行为！const_value 可能存储在只读内存中
```

### 1.5 Type Conversion Decision Guide

The choice among the four conversions can be decided with a simple logic chain:

First, ask yourself: do you need to remove `const` or `volatile`? If yes, use `const_cast`. Second, do you need to do low-level memory reinterpretation (such as integer address to pointer, or between unrelated pointer types)? If yes, use `reinterpret_cast`—but be extra careful. Third, do you need runtime type checking in an inheritance hierarchy with virtual functions? If yes, use `dynamic_cast`—but be mindful of the RTTI overhead. If none of the above apply, use `static_cast`—it covers the vast majority of everyday type conversion needs.

**A practical principle is: prefer `static_cast`, and only use the other three when you clearly know why you need them.** If you find yourself heavily using `reinterpret_cast` or `const_cast`, it likely indicates a problem with your design that is worth re-examining.

## 2. Dynamic Memory Management

### 2.1 new and delete

C++ provides the `new` and `delete` operators to replace C's `malloc` and `free`. Put simply and loosely—`new` is a thin wrapper around `malloc` plus the corresponding constructor call, allowing you to construct an object in-place on a block of `sizeof(TargetType)` bytes; `delete` first calls the destructor, and then frees the memory.

```cpp
// 分配单个对象
int* p = new int;
*p = 42;
delete p;

// 分配并初始化
int* p2 = new int(100);
delete p2;

// 分配对象
class MyClass {
public:
    MyClass() { printf("Constructor\n"); }
    ~MyClass() { printf("Destructor\n"); }
};

MyClass* obj = new MyClass();  // 调用构造函数
delete obj;                    // 调用析构函数，然后释放内存
```

For arrays, you must use the paired `new[]` and `delete[]`:

```cpp
int* arr = new int[10];
delete[] arr;

MyClass* objs = new MyClass[5];  // 调用 5 次构造函数
delete[] objs;                    // 调用 5 次析构函数
```

**The key difference between `new`/`delete` and `malloc`/`free`** is that `new` calls the constructor and `delete` calls the destructor, whereas `malloc`/`free` only handle allocating and freeing raw memory, knowing nothing about object construction and destruction. This means if you use `malloc` to allocate memory for a C++ type, you must manually call placement `new` to construct the object, and manually call the destructor before freeing—this is error-prone and completely unnecessary.

A classic and highly dangerous mistake is mismatching `delete` and `delete[]`:

```cpp
int* arr = new int[10];
delete arr;    // 错误！应该用 delete[]
// 在某些实现上可能不会立即崩溃
// 但行为是未定义的
```

For fundamental types (like `int`), some platforms might "happen" to work without issues, because the destructor of a fundamental type is a no-op. But for arrays of class types, `delete` (without `[]`) will only call the destructor of the first element, leaving all other elements leaked—if the destructor is responsible for releasing other resources (like nested dynamic memory), the consequences are severe. **Make it a habit to use them in pairs: `new` with `delete`, and `new[]` with `delete[]`.**

### 2.2 placement new

`placement new` allows you to construct an object at a **specified memory location**, rather than letting `new` find a new block of memory on its own. In application development, this feature isn't used very often, but it is highly valuable in embedded systems—it lets you construct objects in pre-allocated memory pools, avoiding the use of the standard heap.

```cpp
#include <new>  // 需要包含这个头文件

// 预分配的内存缓冲区
alignas(MyClass) uint8_t buffer[sizeof(MyClass)];

// 在缓冲区中构造对象
MyClass* obj = new (buffer) MyClass();

// 使用对象
obj->some_method();

// 必须显式调用析构函数
obj->~MyClass();

// 不要使用 delete！内存不是用 new 分配的
```

There are a few points to note when using `placement new`. First, the alignment of the memory buffer must satisfy the object's requirements—`alignas(MyClass)` ensures this. Second, because the memory wasn't allocated via `new`, you cannot use `delete`—you can only explicitly call the destructor to clean up the object's state, and then decide for yourself when to reuse or release that memory block. Finally, explicitly calling a destructor is a very rare operation in C++, almost exclusively seen in conjunction with `placement new`—under normal circumstances, you never need to manually call a destructor.

In embedded systems, the most typical application of `placement new` is a **fixed-size memory pool**:

```cpp
class FixedMemoryPool {
private:
    static constexpr size_t POOL_SIZE = 1024;
    alignas(max_align_t) uint8_t memory_pool[POOL_SIZE];
    size_t used;

public:
    FixedMemoryPool() : used(0) {}

    void* allocate(size_t size, size_t alignment = alignof(max_align_t)) {
        size_t padding = (alignment - (used % alignment)) % alignment;
        size_t new_used = used + padding + size;

        if (new_used > POOL_SIZE) {
            return nullptr;
        }

        void* ptr = &memory_pool[used + padding];
        used = new_used;
        return ptr;
    }

    void reset() {
        used = 0;
    }
};

// 使用
FixedMemoryPool pool;
void* mem = pool.allocate(sizeof(MyClass), alignof(MyClass));
if (mem) {
    MyClass* obj = new (mem) MyClass();
    // 使用 obj
    obj->~MyClass();
}
```

The advantage of a memory pool is that the time overhead of allocation and deallocation is completely predictable (just pointer movement), it doesn't produce memory fragmentation, and it won't suffer from the degradation issues that the standard heap experiences after running for a long time. In embedded systems, these characteristics are very important.

## 3. Exception Handling

### 3.1 Basic Exception Handling

Exception handling provides a structured error-handling mechanism that separates error-handling code from normal logic. At least on the surface, it makes the code cleaner. Later, we will discuss why in many cases we prohibit the use of exception handling.

The C++ exception handling paradigm is try-catch-throw: attempt to execute code, throw an exception when an error is encountered, and then catch and handle the exception.

```cpp
#include <exception>
#include <stdexcept>

void risky_function(int value) {
    if (value < 0) {
        throw std::invalid_argument("Value must be non-negative");
    }
    if (value > 100) {
        throw std::out_of_range("Value exceeds maximum");
    }
}

void caller() {
    try {
        risky_function(-5);
    } catch (const std::invalid_argument& e) {
        printf("Invalid argument: %s\n", e.what());
    } catch (const std::out_of_range& e) {
        printf("Out of range: %s\n", e.what());
    } catch (const std::exception& e) {
        printf("Exception: %s\n", e.what());
    } catch (...) {
        printf("Unknown exception\n");
    }
}
```

`catch (...)` catches all types of exceptions and usually serves as a last-resort fallback. The C++ standard library defines a series of exception classes derived from `std::exception`, such as `std::runtime_error`, `std::logic_error`, and `std::out_of_range`. You can also define your own exception types by inheriting from these standard exception classes.

### 3.2 Exception Safety

Writing exception-safe code requires special attention to resource management. The core issue is: **if an exception is thrown in the middle of an operation, what happens to the resources already acquired before that point?**

```cpp
// 不安全的代码
void unsafe_function() {
    int* data = new int[100];
    risky_operation();  // 如果这里抛出异常，data 永远不会被释放
    delete[] data;
}
```

If `risky_operation()` throws an exception, the program flow jumps directly to the nearest `catch` block, and the line `delete[] data` never executes—resulting in a memory leak.

The most direct fix is to wrap it with try-catch:

```cpp
void safe_function_v1() {
    int* data = new int[100];
    try {
        risky_operation();
        delete[] data;
    } catch (...) {
        delete[] data;
        throw;  // 重新抛出异常
    }
}
```

But this is ugly—you have to write try-catch for every resource that needs protection, and if there are multiple resources, the code becomes very complex. A better approach is to use RAII (Resource Acquisition Is Initialization)—use a class's constructor to acquire a resource and its destructor to release it:

```cpp
class AutoArray {
private:
    int* data;

public:
    explicit AutoArray(size_t size) : data(new int[size]) {}
    ~AutoArray() { delete[] data; }

    int& operator[](size_t index) { return data[index]; }
};

void safe_function_v2() {
    AutoArray data(100);
    risky_operation();
    // 即使抛出异常，data 的析构函数也会被自动调用
}
```

RAII is the core paradigm for resource management in C++. When an exception is thrown, the stack unwinding process automatically calls the destructors of all local objects—this guarantees that resources are always correctly released. We will dive deep into RAII in a later chapter.

### 3.3 Exception Safety Levels

From an exception safety perspective, functions can be divided into three levels:

**No guarantee**: If an exception occurs, the object may be left in an inconsistent state, and resources may leak. This is the worst case, but also the most common—whenever you use bare `new`/`delete` without wrapping them in RAII.

**Basic guarantee**: If an exception occurs, the object is left in a valid but unspecified state, and no resources are leaked. All standard library containers provide at least the basic guarantee.

**Strong guarantee**: If an exception occurs, the operation is completely rolled back, and the object state is exactly the same as before the call. This is typically implemented using the "copy-and-swap" idiom.

In embedded development, **the basic guarantee is usually sufficient**. Pursuing the strong guarantee is ideal, but the implementation cost is often very high—you need to create a complete backup before each operation, which is not friendly for resource-constrained systems.

### 3.4 Exception Specifications

C++98 allows specifying which exceptions a function might throw in its declaration:

```cpp
void no_throw_function() throw() {
    // 声明不会抛出异常
}

void specific_throw(int value) throw(std::invalid_argument, std::out_of_range) {
    // 声明只可能抛出这两种异常
}
```

However, this feature was deprecated in C++11. The reason is that its runtime checking mechanism (if a function throws an exception not in the list, it calls `std::unexpected()`) was considered too costly, and in practice it was found to be of almost no help. C++11 replaced this mechanism with the `noexcept` keyword—`noexcept` is simply a boolean promise: "this function will not throw exceptions," and the compiler can use this to perform more aggressive optimizations.

### 3.5 Exception Handling in Embedded Systems

Using exceptions in embedded systems requires great caution. There are several key issues here.

**Code size**: Exception handling requires additional "unwind tables" and runtime support code, which significantly increase binary size. On small MCUs with only a few dozen KB of Flash, this can directly lead to insufficient space.

**Time unpredictability**: When an exception occurs, the time required to handle it is completely unpredictable—it depends on factors like the depth of the call stack and the number of objects that need to be destructed. In embedded real-time systems where timeliness is paramount, this unpredictability is unacceptable.

**Implicit control flow**: Exceptions introduce an "invisible goto"—any function call might exit early due to an exception, making the code's execution paths harder to reason about.

Therefore, many embedded projects choose to disable exceptions entirely (using the `-fno-exceptions` compiler flag), opting instead for return values or error codes for error handling:

```cpp
// 推荐的嵌入式错误处理方式
enum ErrorCode {
    ERROR_OK = 0,
    ERROR_INVALID_PARAM,
    ERROR_TIMEOUT,
    ERROR_HARDWARE_FAULT
};

ErrorCode initialize_hardware() {
    if (!check_hardware()) {
        return ERROR_HARDWARE_FAULT;
    }
    if (!configure_registers()) {
        return ERROR_TIMEOUT;
    }
    return ERROR_OK;
}

ErrorCode result = initialize_hardware();
if (result != ERROR_OK) {
    // 处理错误
}
```

In modern C++, `std::optional` (C++17) and `std::expected` (C++23) provide more elegant solutions than bare error codes—they can express "operation failed" without introducing the runtime overhead of exceptions. In my actual projects, I use these approaches.

## 4. Inline Functions

### 4.1 The True Meaning of inline

In C, we use macros to define short "functions":

```c
#define MAX(a, b) ((a) > (b) ? (a) : (b))
```

The problems with macros are well-known: no type checking, parameters may be evaluated multiple times (`MAX(i++, j)` increments twice), and macro content is invisible during debugging. C++'s `inline` functions solve all these problems:

```cpp
inline int max(int a, int b) {
    return (a > b) ? a : b;
}
```

The original intent of the `inline` keyword was to suggest to the compiler "embed the function body directly at the call site, rather than generating a function call instruction." But in modern compilers, this "suggestion" feature of `inline` is largely ignored—compilers have their own inlining strategies that are more accurate than a programmer's annotation. The compiler decides whether to inline based on factors like function complexity, call frequency, and optimization level, regardless of whether you wrote `inline`.

So what is `inline` still good for? Its true value lies in **allowing the same function to be defined in multiple translation units without violating the one definition rule (ODR)**. As long as all definitions are identical, the linker knows they are the same function and won't report a "multiple definition" error. This is why we typically put the definition of an `inline` function in a header file—every translation unit (`.cpp`) that `#include` this header gets a copy of the definition, but only one is kept at link time.

### 4.2 Implicit inline for In-Class Definitions

Member functions with their bodies written directly inside a class definition are **implicitly `inline`**:

```cpp
class Math {
public:
    // 这个函数隐式是 inline 的
    int add(int a, int b) {
        return a + b;
    }

    // 这个函数需要在类外写 inline
    int multiply(int a, int b);
};

inline int Math::multiply(int a, int b) {
    return a * b;
}
```

### 4.3 Inline Functions in Embedded Systems

In embedded development, `inline` functions are particularly well-suited for replacing macros that manipulate registers:

```cpp
inline void set_bit(volatile uint32_t& reg, int bit) {
    reg |= (1UL << bit);
}

inline void clear_bit(volatile uint32_t& reg, int bit) {
    reg &= ~(1UL << bit);
}

inline bool read_bit(volatile uint32_t& reg, int bit) {
    return (reg >> bit) & 1UL;
}
```

Compared to macros, `inline` functions provide type checking, don't suffer from multiple parameter evaluation issues, and show full information in a debugger. In terms of performance, there is usually no difference—the compiler will expand an `inline` function into machine code similar to that of a macro.

## 5. Type Aliases (typedef)

### 5.1 Basic Usage

Aside from C's `typedef`, the usage of C++'s `typedef` hasn't changed fundamentally, but C++ has a better alternative (C++11's `using`):

```cpp
// 传统 typedef
typedef unsigned int uint32;
typedef void (*ISR_Handler)(void);

// 为模板类型创建别名
typedef std::vector<int> IntVector;
typedef std::map<std::string, int> StringIntMap;
```

### 5.2 Preview: using Aliases

C++11 introduced the `using` keyword to create type aliases. Its functionality is completely equivalent to `typedef`, but the syntax is more intuitive—especially when defining function pointers and template aliases:

```cpp
// typedef 方式
typedef void (*ISR_Handler)(void);

// using 方式（C++11）
using ISR_Handler = void (*)(void);
```

`using` also supports template aliases (which `typedef` cannot do):

```cpp
template<typename T>
using Vector = std::vector<T>;  // C++11 模板别名

Vector<int> v;  // 等价于 std::vector<int>
```

In C++98, you can only use `typedef`. If your project has already migrated to C++11 or later, it is recommended to use `using` exclusively for new code—its syntax is clearer and its capabilities are more powerful.

## Summary

In this chapter, we learned several advanced features in C++98. The four type conversion operators each have clear use cases: `static_cast` covers everyday needs, `reinterpret_cast` is for low-level memory operations, `dynamic_cast` is for runtime type checking, and `const_cast` is for adjusting const qualifiers. `new`/`delete` and `placement new` provide more complete dynamic memory management capabilities than `malloc`/`free`. Although exception handling is powerful, its use in embedded systems requires careful trade-offs. `inline` functions and `typedef` serve as safe replacements for C macros and type aliases.

At this point, we have completed our study of all the fundamental features of C++98. In subsequent chapters, we will enter the world of Modern C++—exploring what improvements and replacements C++11 and later standards have brought to these "old features."
