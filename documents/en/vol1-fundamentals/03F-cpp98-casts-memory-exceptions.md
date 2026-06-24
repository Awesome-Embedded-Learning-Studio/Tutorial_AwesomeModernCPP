---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Precise usage scenarios for the four C++ type conversion operators, managing
  dynamic objects with new/delete and placement new, exception handling mechanisms
  and trade-offs in embedded systems, and inline and typedef.
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
title: 'C++98 Advanced: Type Conversion, Dynamic Memory, and Exception Handling'
translation:
  source: documents/vol1-fundamentals/03F-cpp98-casts-memory-exceptions.md
  source_hash: a6421f9c9c4686525e11505b91ec29f69c07082a4781be45e7a6da115884e7ed
  translated_at: '2026-06-24T00:29:41.313294+00:00'
  engine: anthropic
  token_count: 3440
---
# C++98 Advanced: Type Conversions, Dynamic Memory, and Exception Handling

> The full repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to visit, and if you like it, give the author a Star to show your support.

In this article, we focus on several relatively "advanced" features in C++98: the four type conversion operators, dynamic memory management (`new`/`delete` and `placement new`), exception handling, as well as `inline` functions and `typedef`. While they are not strongly dependent on one another, they all require a basic understanding of classes as a prerequisite.

These features share a common trait: they either enhance existing C mechanisms (type conversions replace C-style casts, `new`/`delete` replace `malloc`/`free`) or are entirely new to C++ (exception handling). Understanding their design intent and applicable boundaries is a prerequisite for using modern C++ correctly.

## 1. C++ Type Conversion Operators

C++ provides four dedicated type conversion operators, which are safer and more explicit than the C-style cast `(type)value`. Each has a specific use case and usage constraints.

### 1.1 static_cast

`static_cast` is used for **type conversions known at compile time**. It is the most "gentle" of the four conversions—it does not perform any dangerous low-level reinterpreting, but simply tells the compiler, "I know this conversion is reasonable, please execute it for me."

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

The safety of `static_cast` lies in its basic compile-time checking—if you attempt to convert between two completely unrelated pointer types (e.g., `int*` to `float*`), the compiler will issue an error directly. For this kind of low-level cross-type conversion, you need to use `reinterpret_cast`.

### 1.2 reinterpret_cast

`reinterpret_cast` performs the **lowest-level reinterpreting conversion**. It allows you to convert between almost any pointer type, and even between pointers and integers. As the name suggests, it merely "reinterprets" the meaning of a block of memory—the compiler performs no safety checks.

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

This usage is inevitable in embedded development—we do need to treat a specific memory address "as" a certain structure. However, be aware that the danger of `reinterpret_cast` lies right here: it completely bypasses the type system. If you provide the wrong address or get the structure layout wrong, you are fully responsible for the consequences.

Another common use case is casting function pointers, such as in the interrupt vector table:

```cpp
typedef void (*ISR_Handler)(void);

void timer_isr() {
    // 中断处理代码
}

uint32_t isr_address = reinterpret_cast<uint32_t>(timer_isr);
```

### 1.3 dynamic_cast

`dynamic_cast` is used for **runtime type checking**, primarily for downcasting polymorphic types (classes containing virtual functions). It checks whether the conversion is safe at runtime—if safe, it returns the converted pointer; otherwise, it returns `nullptr` (pointer version) or throws a `std::bad_cast` exception (reference version).

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

Note that `dynamic_cast` requires **RTTI (Runtime Type Information)** support. RTTI stores type information within every object containing virtual functions, which increases code size and runtime overhead. Many embedded compilers disable RTTI by default to save resources—if your project uses the `-fno-rtti` compiler flag, `dynamic_cast` will not be available.

Therefore, in embedded development, `dynamic_cast` is used far less frequently than the other three types of casting. If you really need to determine types within an inheritance hierarchy, there are usually better alternatives—such as defining a `type()` method in the base class or using the visitor pattern.

### 1.4 const_cast

`const_cast` is used to **add or remove `const` or `volatile` attributes**. It is the only C++ cast operator that can do this—the other three cannot modify the `const`-ness of an object.

The most common legitimate use case is calling legacy C APIs with signatures that are not `const`-correct:

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

However, there is one ironclad rule: **removing the `const` qualification from a truly `const` object and modifying it results in undefined behavior (UB)**. We should use `const_cast` only to remove "accidental" `const` qualification (for example, when an object is passed via a `const` reference but the underlying object itself is not `const`), not to bypass the compiler's protection of actual constants.

```cpp
const int const_value = 100;
int* modifiable = const_cast<int*>(&const_value);
*modifiable = 200;  // 未定义行为！const_value 可能存储在只读内存中
```

### 1.5 Type Conversion Decision Guide

We can decide which of the four casts to use using a simple logic chain:

First, ask yourself: Do we need to remove `const` or `volatile`? If so, use `const_cast`. Second, do we need to perform low-level memory reinterpreting (such as integer address to pointer, or between unrelated pointer types)? If so, use `reinterpret_cast`—but be extremely careful. Third, do we need runtime type checking within an inheritance hierarchy that has virtual functions? If so, use `dynamic_cast`—but be aware of the RTTI overhead. If none of the above apply, use `static_cast`—it covers the vast majority of daily type conversion needs.

**A practical rule is: prefer `static_cast`, and only use the other three when you explicitly know why you need them**. If you find yourself using `reinterpret_cast` or `const_cast` frequently, it may indicate a flaw in your design that warrants re-examination.

## 2. Dynamic Memory Management

### 2.1 new and delete

C++ provides the `new` and `delete` operators to replace C's `malloc` and `free`. To put it simply and loosely—`new` is essentially a wrapper around `malloc` that invokes the corresponding constructor, allowing us to initialize an object in-place on a block of memory sized `sizeof(TargetType)`. Conversely, `delete` calls the destructor first, and then reclaims the memory.

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

For arrays, we must use `new[]` and `delete[]` in pairs:

```cpp
int* arr = new int[10];
delete[] arr;

MyClass* objs = new MyClass[5];  // 调用 5 次构造函数
delete[] objs;                    // 调用 5 次析构函数
```

The key difference between `new`/`delete` and `malloc`/`free` is that `new` invokes the constructor and `delete` invokes the destructor, whereas `malloc`/`free` only handles allocating and freeing raw memory, knowing nothing about object construction or destruction. This means that if you use `malloc` to allocate memory for a C++ type, you must manually use placement `new` to construct the object, and manually call the destructor before freeing—this is error-prone and completely unnecessary.

A classic and highly dangerous error is mismatching `delete` and `delete[]`:

```cpp
int* arr = new int[10];
delete arr;    // 错误！应该用 delete[]
// 在某些实现上可能不会立即崩溃
// 但行为是未定义的
```

For fundamental types (like `int`), some platforms might "happen" to work without issue because the destructor for fundamental types is essentially a no-op. However, for arrays of class types, using `delete` (without `[]`) will only invoke the destructor for the first element, leaving the rest to leak—if the destructor is responsible for releasing other resources (such as nested dynamic memory), the consequences can be severe. **Make it a habit to use them in matching pairs: `new` with `delete`, and `new[]` with `delete[]`.**

### 2.2 placement new

`placement new` allows us to construct an object at a **specific memory location**, rather than letting `new` find a new block of memory on its own. While this feature isn't used extensively in desktop development, it is extremely valuable in embedded systems—it allows us to construct objects within pre-allocated memory pools, avoiding the use of the standard heap.

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

There are a few points to keep in mind when using `placement new`. First, the alignment of the memory buffer must satisfy the object's requirements—`alignas(MyClass)` ensures this. Second, because the memory was not allocated via `new`, we cannot use `delete`—we must explicitly call the destructor to clean up the object's state, and then decide when to reuse or release this memory ourselves. Finally, explicitly calling the destructor is a very rare operation in C++, almost exclusively appearing in conjunction with `placement new`—under normal circumstances, we never need to manually invoke the destructor.

In embedded systems, the most typical application of `placement new` is **fixed-size memory pools**:

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

The advantage of a memory pool is that the time overhead for allocation and deallocation is entirely predictable (it is merely pointer movement). It does not generate memory fragmentation, nor does it suffer from the degradation issues often found in standard heaps after long-running operations. In embedded systems, these characteristics are crucial.

## 3. Exception Handling

### 3.1 Basic Exception Handling

Exception handling provides a structured error handling mechanism that allows us to separate error handling code from the normal logic. At the very least, the code appears cleaner. Later, we will discuss why we often prohibit the use of exception handling in many scenarios.

The C++ exception handling paradigm is `try-catch-throw`: we attempt to execute code, throw an exception when an error is encountered, and then catch and handle the exception.

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

`catch (...)` catches all types of exceptions and is typically used as a final fallback. The C++ Standard Library defines a series of exception classes derived from `std::exception`, such as `std::runtime_error`, `std::logic_error`, and `std::out_of_range`. We can also define our own exception types by inheriting from these standard exception classes.

### 3.2 Exception Safety

Writing exception-safe code requires special attention to resource management. The core question is: **If an exception is thrown in the middle of an operation, what happens to the resources that were already acquired?**

```cpp
// 不安全的代码
void unsafe_function() {
    int* data = new int[100];
    risky_operation();  // 如果这里抛出异常，data 永远不会被释放
    delete[] data;
}
```

If `risky_operation()` throws an exception, the program flow jumps directly to the nearest `catch` block, and the line `delete[] data` is never executed—resulting in a memory leak.

The most direct fix is to wrap it in a try-catch block:

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

But this is ugly—we have to write a try-catch block for every resource that needs protection, and if there are multiple resources, the code becomes extremely complex. A better approach is to use RAII—acquiring resources in a class constructor and releasing them in the destructor:

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

RAII is the core paradigm for resource management in C++. When an exception is thrown, the stack unwinding process automatically invokes the destructors of all local objects—this guarantees that resources are always released correctly. We will dive deep into RAII in a later chapter.

### 3.3 Exception Safety Levels

From the perspective of exception safety, functions can be categorized into three levels:

**No guarantee**: If an exception occurs, the object may be left in an inconsistent state, and resources might leak. This is the worst scenario, but it is also the most common—especially if you are using raw `new`/`delete` without wrapping them in RAII.

**Basic guarantee**: If an exception occurs, the object remains in a valid but unspecified state, and no resources are leaked. All standard library containers provide at least the basic guarantee.

**Strong guarantee**: If an exception occurs, the operation is completely rolled back, and the object state is exactly the same as before the call. This is typically implemented using the "copy-and-swap" idiom.

In embedded development, the **basic guarantee is usually sufficient**. Pursuing the strong guarantee is ideal, but the implementation cost is often high—you would need to create a full backup before every operation, which is not friendly for resource-constrained systems.

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

However, this feature was deprecated in C++11. The reason is that its runtime checking mechanism (if a function throws an exception not listed in the specification, it calls `std::unexpected()`) was considered too costly. Furthermore, practical experience revealed that it provided almost no benefit. C++11 replaced this mechanism with the `noexcept` keyword. `noexcept` is simply a boolean promise: "this function will not throw exceptions." Based on this, the compiler can perform more aggressive optimizations.

### 3.5 Exception Handling in Embedded Systems

Using exceptions in embedded systems requires extreme caution. Here are several key issues.

**Code Size**: Exception handling requires additional "unwind tables" and runtime support code, which significantly increases the binary size. On small MCUs with only a few tens of KB of Flash, this can directly lead to insufficient space.

**Timing Uncertainty**: When an exception occurs, the time required to handle it is completely unpredictable—it depends on the depth of the call stack, the number of objects that need to be destroyed, and other factors. In embedded real-time systems where timing is critical, this uncertainty is unacceptable.

**Implicit Control Flow**: Exceptions introduce an "invisible goto"—any function call might exit prematurely due to an exception, making the code's execution path much harder to reason about.

Therefore, many embedded projects choose to disable exceptions entirely (using the `-fno-exceptions` compiler flag) and instead use return values or error codes for error handling:

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

In modern C++, `std::optional` (C++17) and `std::expected` (C++23) provide a more elegant solution than raw error codes—they can express "operation failure" without introducing the runtime overhead of exceptions. We use these approaches in our actual projects.

## 4. Inline Functions (`inline`)

### 4.1 The True Meaning of `inline`

In C, we use macros to define short "functions":

```c
#define MAX(a, b) ((a) > (b) ? (a) : (b))
```

The problems with macros are well known: there is no type checking, parameters might be evaluated multiple times (`MAX(i++, j)` increments twice), and macro contents are invisible during debugging. C++ `inline` functions solve all these problems:

```cpp
inline int max(int a, int b) {
    return (a > b) ? a : b;
}
```

The original intent of the `inline` keyword was to suggest to the compiler: "embed the function body directly at the call site, rather than generating a function call instruction." However, in modern compilers, this "suggestive" function is largely ignored—compilers have their own set of inlining strategies that are more accurate than a programmer's annotation. The compiler decides whether to inline based on function complexity, call frequency, optimization level, and other factors, regardless of whether you write `inline` or not.

So, what is `inline` still used for? Its true value lies in **allowing the same function to be defined in multiple translation units without violating the ODR (One Definition Rule)**. As long as all definitions are identical, the linker knows they represent the same function and will not report a "multiple definition" error. This is why we typically place the definition of `inline` functions in header files—every `.cpp` file that `#include`s this header gets a copy of the definition, but only one copy is retained during linking.

### 4.2 Implicit inline for in-class definitions

Member functions defined directly inside the class body are **implicitly `inline`**:

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

In embedded development, `inline` functions are particularly suitable for replacing register-manipulation macros:

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

Compared to macros, `inline` functions offer type checking, avoid issues with multiple parameter evaluations, and provide complete visibility within a debugger. In terms of performance, there is usually no difference—the compiler expands `inline` functions into machine code similar to macros.

## 5. Type Aliases (typedef)

### 5.1 Basic Usage

Beyond C's `typedef`, the usage of `typedef` in C++ hasn't changed fundamentally. However, C++ provides a better alternative (C++11's `using`):

```cpp
// 传统 typedef
typedef unsigned int uint32;
typedef void (*ISR_Handler)(void);

// 为模板类型创建别名
typedef std::vector<int> IntVector;
typedef std::map<std::string, int> StringIntMap;
```

### 5.2 Preview: using Aliases

C++11 introduced the `using` keyword for creating type aliases. It is functionally equivalent to `typedef`, but offers a more intuitive syntax—especially when defining function pointers and template aliases:

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

In C++98, you could only use `typedef`. If your project has migrated to C++11 or later, we recommend using `using` exclusively for new code—its syntax is clearer, and it is more powerful.

## Summary

In this chapter, we covered several advanced features from C++98. The four type conversion operators each have distinct use cases: `static_cast` covers everyday needs, `reinterpret_cast` handles low-level memory operations, `dynamic_cast` performs runtime type checking, and `const_cast` adjusts `const` attributes. `new`/`delete` and placement new provide more comprehensive dynamic memory management capabilities than `malloc`/`free`. Exception handling is powerful, but its use in embedded systems requires careful trade-offs. `inline` functions and `typedef` serve as safer alternatives to C macros and type aliases.

At this point, we have completed our study of the fundamental features of C++98. In subsequent chapters, we will enter the world of Modern C++—exploring how the C++11 and later standards have improved and provided alternatives for these "legacy features."
