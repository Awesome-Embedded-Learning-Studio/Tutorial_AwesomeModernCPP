---
title: "C++98进阶：类型转换、动态内存与异常处理"
description: "四种C++类型转换运算符的精确使用场景、new/delete与placement new管理动态对象、异常处理机制与嵌入式取舍、inline和typedef"
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
  - "C++98面向对象：类与对象深度剖析"
  - "C++98面向对象：继承与多态"
related:
  - "何时用C++、用哪些C++特性"
cpp_standard: [11, 14, 17, 20]
platform: host
---

# C++98进阶：类型转换、动态内存与异常处理

> 完整的仓库地址在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) 中，您也可以光顾一下，喜欢的话给一个 Star 激励一下作者

这一篇我们集中讲解 C++98 中几个相对"进阶"的特性：四种类型转换运算符、动态内存管理（`new`/`delete` 和 `placement new`）、异常处理，以及 `inline` 函数和 `typedef`。它们彼此之间没有强依赖关系，但都需要类的基础知识作为前提。

这些特性有一个共同点：它们要么是对 C 中已有机制的增强（类型转换替代了 C 风格强转，`new`/`delete` 替代了 `malloc`/`free`），要么是 C++ 全新引入的（异常处理）。理解它们的设计意图和适用边界，是正确使用现代 C++ 的前提。

## 1. C++ 类型转换运算符

C++ 提供了四种专用的类型转换运算符，比 C 风格的强制转换 `(type)value` 更安全、更明确。每一种都有明确的适用场景和使用约束。

### 1.1 static_cast

`static_cast` 用于**编译时已知的类型转换**。它是四种转换中最"温和"的一种——它不会做任何危险的底层 reinterpret，只是告诉编译器"我知道这个转换是合理的，请你帮我执行"。

适用场景包括：基本类型之间的转换（如 `int` 到 `float`）、有继承关系的指针或引用之间的转换（向上转型总是安全、向下转型需要程序员确保安全）、以及 `void*` 与其他指针类型之间的转换。

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

`static_cast` 的安全性在于它会进行基本的编译期检查——如果你试图在两个完全不相关的指针类型之间转换（比如 `int*` 到 `float*`），编译器会直接报错。对于这种跨类型的底层转换，你需要使用 `reinterpret_cast`。

### 1.2 reinterpret_cast

`reinterpret_cast` 执行的是**最低级别的重新解释转换**，它几乎可以让你在任意指针类型之间、甚至指针和整数之间进行转换。顾名思义，它只是"重新解释"了一段内存的含义——编译器不做任何安全性检查。

在嵌入式系统中，`reinterpret_cast` 是访问硬件寄存器的标准方法：

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

这种用法在嵌入式开发中是不可避免的——你确实需要把一个固定的内存地址"当作"某种结构体来访问。但要注意，`reinterpret_cast` 的危险性也正在于此：它完全绕过了类型系统，如果你给错了地址或者搞错了结构体布局，后果完全由你自己承担。

另一个常见的用途是函数指针的转换，比如中断向量表：

```cpp
typedef void (*ISR_Handler)(void);

void timer_isr() {
    // 中断处理代码
}

uint32_t isr_address = reinterpret_cast<uint32_t>(timer_isr);
```

### 1.3 dynamic_cast

`dynamic_cast` 用于**运行时类型检查**，主要用于多态类型（包含虚函数的类）的向下转换。它会在运行时检查转换是否安全——如果安全则返回转换后的指针，不安全则返回 `nullptr`（指针版本）或抛出 `std::bad_cast` 异常（引用版本）。

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

需要注意的是，`dynamic_cast` 需要 **RTTI (Runtime Type Information)** 支持。RTTI 会在每个含虚函数的对象中存储类型信息，这会增加代码大小和运行时开销。许多嵌入式编译器默认禁用 RTTI 以节省资源——如果你的项目用了 `-fno-rtti` 编译选项，`dynamic_cast` 就无法使用了。

所以在嵌入式开发中，`dynamic_cast` 的使用频率远低于其他三种转换。如果你确实需要在继承层次中做类型判断，通常有更好的替代方案——比如在基类中定义一个 `type()` 方法，或者使用访问者模式。

### 1.4 const_cast

`const_cast` 用于**添加或移除 `const` 或 `volatile` 属性**。它是唯一能做到这件事的 C++ 转换运算符——其他三种都不能改变 `const` 性质。

最常见的合法用途是调用那些签名不够 `const` 友好的遗留 C API：

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

但有一条铁律：**移除真正 `const` 对象的 `const` 属性并修改它，是未定义行为**。`const_cast` 应该只用于移除"意外添加"的 `const` 属性（比如通过 `const` 引用传入、但底层对象本身不是 `const` 的），而不是用来绕过编译器对真正常量的保护。

```cpp
const int const_value = 100;
int* modifiable = const_cast<int*>(&const_value);
*modifiable = 200;  // 未定义行为！const_value 可能存储在只读内存中
```

### 1.5 类型转换决策指南

四种转换的选择可以用一个简单的逻辑链来决定：

首先问自己：需要移除 `const` 或 `volatile` 吗？如果需要，用 `const_cast`。其次，需要做底层的内存重新解释（如整数地址到指针、不相关指针类型之间）吗？如果需要，用 `reinterpret_cast`——但要格外小心。再次，需要在有虚函数的继承层次中做运行时类型检查吗？如果需要，用 `dynamic_cast`——但要注意 RTTI 开销。如果以上都不是，那就用 `static_cast`——它覆盖了绝大部分日常的类型转换需求。

**一个实用的原则是：优先用 `static_cast`，只有在明确知道为什么需要其他三种的时候才使用它们。**如果你发现自己在大量使用 `reinterpret_cast` 或 `const_cast`，那可能说明你的设计存在问题，值得重新审视。

## 2. 动态内存管理

### 2.1 new 和 delete

C++ 提供了 `new` 和 `delete` 运算符来替代 C 的 `malloc` 和 `free`。可以最简化和不严谨地说——`new` 是 `malloc` 加上对应构造函数调用的简单封装，让你可以在 `sizeof(TargetType)` 大小的内存上就地初始化对象；`delete` 则是先调用析构函数，然后回收内存。

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

对于数组，必须使用 `new[]` 和 `delete[]` 配对：

```cpp
int* arr = new int[10];
delete[] arr;

MyClass* objs = new MyClass[5];  // 调用 5 次构造函数
delete[] objs;                    // 调用 5 次析构函数
```

**`new`/`delete` vs `malloc`/`free` 的关键区别**在于：`new` 会调用构造函数、`delete` 会调用析构函数，而 `malloc`/`free` 只负责分配和释放原始内存，对对象的构造和析构一无所知。这意味着如果你用 `malloc` 分配了一个 C++ 类型的内存，你必须手动调用放置 `new` 来构造对象，释放前还得手动调用析构函数——很容易出错，完全没有必要。

一个经典且非常危险的错误是 `delete` 和 `delete[]` 不匹配：

```cpp
int* arr = new int[10];
delete arr;    // 错误！应该用 delete[]
// 在某些实现上可能不会立即崩溃
// 但行为是未定义的
```

对于基本类型（如 `int`），某些平台可能"碰巧"不出问题，因为基本类型的析构函数是空操作。但对于类类型的数组，`delete`（不带 `[]`）只会调用第一个元素的析构函数，其余元素全部泄漏——如果析构函数负责释放其他资源（比如嵌套的动态内存），后果就严重了。**养成配对使用的习惯：`new` 对 `delete`，`new[]` 对 `delete[]`。**

### 2.2 placement new

`placement new` 允许在**指定的内存位置**构造对象，而不是让 `new` 自己去找一块新内存。在上位机开发中，这个特性用得不是特别多，但在嵌入式系统中非常有价值——它让你可以在预分配的内存池中构造对象，避免使用标准堆。

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

使用 `placement new` 时有几个注意点。首先，内存缓冲区的对齐必须满足对象的要求——`alignas(MyClass)` 确保了这一点。其次，因为内存不是通过 `new` 分配的，所以不能使用 `delete`——只能显式调用析构函数来清理对象状态，然后由你自己决定何时复用或释放这块内存。最后，析构函数的显式调用在 C++ 中是非常罕见的操作，几乎只在 `placement new` 的配套场景下出现——正常情况下，你永远不需要手动调用析构函数。

在嵌入式系统中，`placement new` 最典型的应用是**固定大小的内存池**：

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

内存池的好处在于：分配和释放的时间开销完全可预测（就是指针移动），不会产生内存碎片，也不会出现标准堆在长时间运行后的退化问题。在嵌入式系统中，这些特性非常重要。

## 3. 异常处理 (Exception Handling)

### 3.1 基本异常处理

异常处理提供了一种结构化的错误处理机制，可以将错误处理代码与正常逻辑分离。至少看起来，代码能干净一些。后面会讲为什么在很多情况下，我们会禁止使用异常处理。

C++ 的异常处理范式是 try-catch-throw：尝试执行代码，遇到错误抛出异常，然后捕获并处理异常。

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

`catch (...)` 会捕获所有类型的异常，通常作为最后的兜底。C++ 标准库定义了一系列异常类，从 `std::exception` 派生，如 `std::runtime_error`、`std::logic_error`、`std::out_of_range` 等。你也可以通过继承这些标准异常类来定义自己的异常类型。

### 3.2 异常安全

编写异常安全的代码需要特别注意资源管理。核心问题是：**如果异常在某个操作中间被抛出，在此之前已经获取的资源怎么办？**

```cpp
// 不安全的代码
void unsafe_function() {
    int* data = new int[100];
    risky_operation();  // 如果这里抛出异常，data 永远不会被释放
    delete[] data;
}
```

如果 `risky_operation()` 抛出了异常，程序流程会直接跳转到最近的 `catch` 块，`delete[] data` 这行代码永远不会执行——内存泄漏。

最直接的修复方式是用 try-catch 包裹：

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

但这很丑——每个需要保护的资源都要写一遍 try-catch，而且如果有多个资源，代码会变得非常复杂。更好的做法是使用 RAII——用一个类的构造函数来获取资源，析构函数来释放资源：

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

RAII 是 C++ 中管理资源的核心范式。当异常被抛出时，栈展开 (stack unwinding) 过程会自动调用所有局部对象的析构函数——这保证了资源总是能被正确释放。我们会在后续的章节中专门深入讲解 RAII。

### 3.3 异常安全级别

从异常安全的角度来看，函数可以分为三个级别：

**无保证 (no guarantee)**：如果异常发生，对象可能处于不一致的状态，资源可能泄漏。这是最差的情况，但也最容易出现——只要你在裸用 `new`/`delete` 而没有用 RAII 包装。

**基本保证 (basic guarantee)**：如果异常发生，对象处于一个合法但不确定的状态，不会泄漏资源。所有标准库容器都至少提供基本保证。

**强保证 (strong guarantee)**：如果异常发生，操作被完全回滚，对象状态和调用前完全一样。这通常通过"copy-and-swap" 惯用法来实现。

在嵌入式开发中，**基本保证通常是足够的**。追求强保证虽然理想，但实现成本往往很高——你需要在每次操作前创建完整的备份，这对资源受限的系统并不友好。

### 3.4 异常规格说明

C++98 允许在函数声明中指定它可能抛出哪些异常：

```cpp
void no_throw_function() throw() {
    // 声明不会抛出异常
}

void specific_throw(int value) throw(std::invalid_argument, std::out_of_range) {
    // 声明只可能抛出这两种异常
}
```

不过这个特性在 C++11 中已被废弃。原因是它的运行时检查机制（如果函数抛出了列表之外的异常，会调用 `std::unexpected()`）被认为代价过高，而且实际使用中发现它几乎没有帮助。C++11 用 `noexcept` 关键字替代了这套机制——`noexcept` 只是一个简单的布尔承诺："这个函数不会抛出异常"，编译器据此可以做更激进的优化。

### 3.5 嵌入式系统中的异常处理

在嵌入式系统中使用异常需要非常谨慎。这里有几个关键问题。

**代码大小**：异常处理需要额外的"展开表"和运行时支持代码，这些会显著增加二进制体积。在 Flash 只有几十 KB 的小型 MCU 上，这可能直接导致空间不够。

**时间不确定性**：当异常发生时，处理异常所需的时间完全无法预测——它取决于调用栈的深度、需要析构的对象数量等因素。在实时性看得极重的嵌入式实时系统里，这种不确定性是不可接受的。

**隐式的控制流**：异常引入了一种"看不见的 goto"——任何函数调用都可能因为异常而提前退出，这让代码的执行路径变得更难推理。

因此，许多嵌入式项目选择完全禁用异常（使用 `-fno-exceptions` 编译选项），转而使用返回值或错误码进行错误处理：

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

在现代 C++ 中，`std::optional`（C++17）和 `std::expected`（C++23）提供了比裸错误码更优雅的方案——它们既可以表达"操作失败"，又不会引入异常的运行时开销。笔者在实际项目中就在使用这些方案。

## 4. 内联函数 (inline)

### 4.1 inline 的真正含义

在 C 中，我们用宏来定义短小的"函数"：

```c
#define MAX(a, b) ((a) > (b) ? (a) : (b))
```

宏的问题是众所周知的：没有类型检查、参数可能被多次求值（`MAX(i++, j)` 会递增两次）、调试时看不到宏的内容。C++ 的 `inline` 函数解决了所有这些问题：

```cpp
inline int max(int a, int b) {
    return (a > b) ? a : b;
}
```

`inline` 关键字的原始意图是建议编译器"把函数体直接嵌入调用点，而不是生成函数调用指令"。但在现代编译器中，`inline` 的这个"建议"功能已经基本被忽略了——编译器有自己的一套内联策略，比程序员的标记更准确。编译器会根据函数的复杂度、调用频率、优化等级等因素来决定是否内联，不管你有没有写 `inline`。

那么 `inline` 还有什么用？它的真正价值在于**允许同一个函数在多个翻译单元中被定义，而不违反 ODR (One Definition Rule)**。只要所有定义完全一致，链接器就知道它们是同一个函数，不会报"重复定义"的错误。这就是为什么我们通常把 `inline` 函数的定义放在头文件中——每个 `#include` 这个头文件的 `.cpp` 都会得到一份定义，但链接时只会保留一份。

### 4.2 类内定义的隐式 inline

在类定义内部直接写函数体的成员函数，**隐式地就是 `inline` 的**：

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

### 4.3 嵌入式中的 inline 函数

在嵌入式开发中，`inline` 函数特别适合用来替代那些操作寄存器的宏：

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

相比宏，`inline` 函数有类型检查、不会出现参数多次求值的问题、可以在调试器中看到完整信息。在性能上，两者通常没有区别——编译器会把 `inline` 函数展开为和宏类似的机器码。

## 5. 类型别名 (typedef)

### 5.1 基本用法

除了 C 的 `typedef`，C++ 的 `typedef` 在使用上没有本质变化，但在 C++ 中有了更好的替代方案（C++11 的 `using`）：

```cpp
// 传统 typedef
typedef unsigned int uint32;
typedef void (*ISR_Handler)(void);

// 为模板类型创建别名
typedef std::vector<int> IntVector;
typedef std::map<std::string, int> StringIntMap;
```

### 5.2 预告：using 别名

C++11 引入了 `using` 关键字来创建类型别名，它的功能完全等价于 `typedef`，但语法更直观——特别是在定义函数指针和模板别名的时候：

```cpp
// typedef 方式
typedef void (*ISR_Handler)(void);

// using 方式（C++11）
using ISR_Handler = void (*)(void);
```

`using` 还支持模板别名（`typedef` 做不到）：

```cpp
template<typename T>
using Vector = std::vector<T>;  // C++11 模板别名

Vector<int> v;  // 等价于 std::vector<int>
```

在 C++98 中，你只能使用 `typedef`。如果你的项目已经迁移到了 C++11 或更高版本，建议新代码一律使用 `using`——它的语法更清晰，功能也更强大。

## 小结

这一章我们学习了 C++98 中的几个进阶特性。四种类型转换运算符各有明确的适用场景：`static_cast` 覆盖日常需求，`reinterpret_cast` 用于底层内存操作，`dynamic_cast` 用于运行时类型检查，`const_cast` 用于调整 const 属性。`new`/`delete` 和 `placement new` 提供了比 `malloc`/`free` 更完整的动态内存管理能力。异常处理虽然强大，但在嵌入式系统中的使用需要谨慎权衡。`inline` 函数和 `typedef` 则是对 C 中宏和类型别名的安全替代。

到这里，我们已经完成了 C++98 基础特性的全部学习。在后续章节中，我们将进入 Modern C++ 的世界——看看 C++11 及之后的标准为这些"老特性"带来了哪些改进和替代方案。
