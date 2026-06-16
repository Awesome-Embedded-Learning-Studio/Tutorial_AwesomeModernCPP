---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: A quick review of basic C syntax, covering core concepts such as data
  types, operators, control flow, pointers, arrays, and structs.
difficulty: beginner
order: 2
platform: host
prerequisites: []
reading_time_minutes: 27
related: []
tags:
- cpp-modern
- host
- intermediate
title: C Language Crash Course Review
translation:
  source: documents/vol1-fundamentals/02-c-language-crash-course.md
  source_hash: 549fc401730ae08cfe9b764a7c03e09a8431bc25f157b1bd17576f608e14cf29
  translated_at: '2026-06-16T03:31:51.283528+00:00'
  engine: anthropic
  token_count: 5758
---
# Quick C Language Refresher

> The full repository is located at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to visit, and if you like it, give the project a Star to motivate the author.

Although I must clarify that C++ can no longer be described today as a **simple C superset**, C++ was designed to strive for compatibility with C from the very beginning. Therefore, we assume that everyone's C skills are sufficient to write functional business logic code for one or more embedded systems. Thus, this section serves as a quick, comprehensive refresher on the common-sense parts of the C language.

## 1. Basic Data Types and Type Modifiers

It is worth mentioning that C itself is a **strongly typed** programming language. Declaring what a variable is has been a standard requirement since the birth of C.

> I know some might mention `auto`. While `auto` is indeed great for saving time when writing complex types, my stance is: don't abuse it.

C's type system is the foundation of the language. In embedded development, accurately understanding the size and range of data types is particularly important because hardware resources are often constrained. We must also keep this in mind when writing C++.

### 1.1 The Integer Family

C provides a rich set of integer types, each with its specific purpose and range. Note that with the exception of the `char` type, which is fixed at 8 bits on some platforms, the actual size of other integers is implementation-defined.

```c
char c = 'a';          // Usually 8 bits
short s = 10;          // Usually 16 bits
int i = 100;           // Usually 16 or 32 bits
long l = 1000;         // Usually 32 bits
long long ll = 10000;  // Usually 64 bits
```

In embedded systems, we often need precise control over the size of data types. The `stdint.h` header introduced in the C99 standard provides fixed-width integers, which is extremely important when writing portable embedded code, especially for foundational libraries that might be used on both 32-bit and 64-bit platforms. (I've noticed that 64-bit chips for embedded platforms are slowly emerging, so we really do need to care about this.)

```c
#include <stdint.h>

uint8_t  u8 = 255;   // Unsigned 8-bit
int16_t  i16 = -100; // Signed 16-bit
uint32_t u32 = 1000; // Unsigned 32-bit
```

So the question is: when do you use which size? Well, this doesn't have to be too rigid, but there is one thing you must note—**your data range must be sufficient**. So, the question arises: **how big can an N-bit number store?** For an **unsigned integer**, N bits can represent **2ⁿ values**, with a range of **0 ~ 2ⁿ − 1**. What about a **signed integer**? The highest bit is used for the sign bit. Using two's complement representation, the range is **−2ⁿ⁻¹ ~ 2ⁿ⁻¹ − 1**. Since we are all embedded programmers, I assume we can all handle this binary math.

### 1.2 Floating-Point Types

Floating-point types are used to represent real numbers, but using floating-point arithmetic in embedded systems requires extreme caution, as many microcontrollers do not support hardware floating-point operations, and software simulation brings significant performance overhead.

```c
float  f = 3.14f;   // Single precision (32-bit)
double d = 3.14159; // Double precision (64-bit)
```

 in extremely resource-constrained embedded systems, if you must use floating-point arithmetic, prioritize `float` over `double`, as it consumes less memory and computational resources. `double` can sometimes be too heavy on the operation count.

### 1.3 Type Modifiers

Type modifiers can change the properties of basic types and have special importance in embedded programming.

#### signed and unsigned

The `unsigned` modifier extends the range of an integer variable to non-negative numbers only. This is very useful when handling hardware register values and bit masks:

```c
unsigned int count = 0; // Can hold larger positive values
uint8_t flags = 0xFF;    // Standard usage for 8-bit data
```

#### const Modifier

The `const` keyword declares a variable as read-only. This has multiple roles in embedded development. First, it helps the compiler optimize by placing **constant data in ROM or Flash instead of RAM**, saving valuable RAM resources. Second, it provides compile-time safety checks to prevent accidental modification of data that shouldn't change. This is sometimes important, essentially emphasizing that within the current logic, this is an invariant. (Of course, C++ provides the even more powerful `constexpr`, which we will discuss when we get to C++.)

```c
const int MAX_BUFFER_SIZE = 128;
// MAX_BUFFER_SIZE is stored in Flash, not RAM
```

Using `const` in function parameters clearly indicates that the function will not modify the passed data, which is good practice when designing APIs:

```c
void process_data(const uint8_t *data, size_t len);
```

#### volatile Modifier

The literal meaning of `volatile` is "changeable." It is an extremely important, yet most easily misunderstood, keyword in embedded C programming. Its core role is not to "prohibit compiler optimization," but to **explicitly tell the compiler: the value of this variable might change outside the current program control flow**. In embedded systems, such "outside control flow" changes usually come from hardware peripherals, interrupt service routines (ISRs), DMA, or other concurrent execution contexts.

Because of this, when the compiler faces an object modified by `volatile`, **it cannot assume the variable remains unchanged between two accesses**. Every read and write of a `volatile` variable belongs to an **observable behavior** in the abstract machine model and must actually occur in memory, rather than being cached in a register, merged, or directly eliminated. This doesn't mean the compiler "can't optimize at all," but rather that it cannot make a "value stability" assumption about the `volatile` object; other unrelated code can still be optimized normally.

In embedded programming, the most common scenario for `volatile` is passing status information between an interrupt and the main loop. For example, an event flag that is set in an interrupt callback and polled in the main loop must be declared as `volatile`. Otherwise, at higher optimization levels, the compiler might think the variable is never modified in the main loop, leading it to hoist, cache, or even optimize away the read operation, causing program behavior to deviate seriously from expectations.

Looking at it from another angle, if a normal variable is written to different values consecutively in the same execution path, but no observable behavior in between depends on it, then without `volatile`, the compiler has every reason to consider these writes "redundant" and eliminate them. Once the variable is declared `volatile`, these writes become non-eliminable memory accesses that must strictly occur in order.

It is particularly important to emphasize that `volatile` only solves the **compiler-level visibility problem**. It does not guarantee atomicity, nor does it provide any thread synchronization or memory ordering semantics. Compound operations on `volatile` variables (e.g., incrementing) can still produce race conditions in interrupt or multi-threaded environments. If the program requires atomicity or synchronization guarantees, one must use interrupt disabling, locks, atomic instructions, or specialized concurrency primitives. This is why any operating system must encapsulate and provide lock primitives.

```c
volatile bool flag = false;

// Interrupt Service Routine
void EXTI_Handler(void) {
    flag = true; // Set flag in interrupt
}

// Main loop
while (1) {
    if (flag) { // Must read from memory, cannot be optimized away
        flag = false;
        // Handle event
    }
}
```

Additionally, when accessing hardware registers, it is usually necessary to use both `volatile` and `const`. I believe friends who have read SDKs know this.

```c
// Pointer to volatile const register (Read-Only)
#define REG_STATUS (*(volatile const uint32_t*)(0x40000000))

// Pointer to volatile register (Read-Write)
#define REG_DATA   (*(volatile uint32_t*)(0x40000004))
```

## 2. Operators and Expressions

### 2.1 Arithmetic Operators

C provides standard arithmetic operators, but when using them in embedded systems, watch out for overflow and type promotion issues:

```c
int a = 10, b = 3;
int sum = a + b;    // Addition
int diff = a - b;   // Subtraction
int prod = a * b;   // Multiplication
int quot = a / b;   // Division (integer)
int rem = a % b;    // Modulo
```

In embedded development, division and modulo operations usually have high overhead, especially on MCUs without a hardware divider. In performance-critical code, we should avoid division operations or replace division by powers of two with bit shifts:

```c
// Instead of: int result = value / 8;
int result = value >> 3; // Faster if value is positive

// Instead of: int result = value % 16;
int result = value & 0xF; // Faster
```

### 2.2 Bitwise Operators

Bitwise operators are core tools in embedded programming. They operate directly on the binary bits of data and are commonly used for hardware register configuration, flag management, and efficient mathematical operations.

```c
unsigned int a = 0x0F; // 0000 1111

unsigned int and = a & 0x0F; // Bitwise AND
unsigned int or  = a | 0xF0; // Bitwise OR
unsigned int xor = a ^ 0xFF; // Bitwise XOR
unsigned int not = ~a;        // Bitwise NOT (Complement)

unsigned int lshift = a << 4; // Left shift (multiply by 16)
unsigned int rshift = a >> 2; // Right shift (divide by 4)
```

Typical applications of bitwise operations in embedded development include:

**Register Bit Manipulation**:

```c
// Set bit 5
REG_CONTROL |= (1 << 5);

// Clear bit 3
REG_CONTROL &= ~(1 << 3);

// Toggle bit 2
REG_CONTROL ^= (1 << 2);
```

**Bitfield Masks**:

```c
#define MASK_STATUS  0x07 // Bits 0-2
#define MASK_ENABLE  0x80 // Bit 7

uint8_t status = REG_READ & MASK_STATUS;
bool enabled = (REG_READ & MASK_ENABLE) ? true : false;
```

### 2.3 Relational and Logical Operators

Relational operators are used for comparison and return an integer result (0 for false, non-zero for true):

```c
int a = 5, b = 10;

if (a < b) { ... }   // Less than
if (a == b) { ... }  // Equal to
if (a != b) { ... }  // Not equal to
```

Logical operators have short-circuit characteristics, which can be used for conditional optimization in embedded programming:

```c
// If the first condition is false, the second function is not called
if (ptr != NULL && ptr->data > 0) { ... }

// If the first condition is true, the second is not evaluated
if (error_occurred() || retry_count > 3) { ... }
```

### 2.4 Other Important Operators

The **Ternary Conditional Operator** is the only ternary operator in C and can simplify simple if-else statements:

```c
int max = (a > b) ? a : b;
```

The **sizeof Operator** returns the byte size of a type or object and is evaluated at compile time, often used for array size calculations:

```c
int arr[10];
size_t n = sizeof(arr) / sizeof(arr[0]); // Number of elements
```

The **Comma Operator** evaluates expressions from left to right and returns the value of the rightmost expression:

```c
int i = (a = 1, b = 2, a + b); // i is 3
```

## 3. Control Flow Statements

### 3.1 Conditional Statements

The **if-else statement** is the most basic conditional branch:

```c
if (condition) {
    // Code block
} else if (another_condition) {
    // Code block
} else {
    // Code block
}
```

In embedded systems, for multiple mutually exclusive conditions, using an else-if chain can avoid unnecessary condition checks and improve execution efficiency.

The **switch statement** is suitable for multi-way branching. Compilers usually optimize this into a jump table, which in some cases is more efficient than multiple if-else statements:

```c
switch (value) {
    case 1:
        // Handle case 1
        break;
    case 2:
        // Handle case 2
        break;
    default:
        // Handle default case
        break;
}
```

In embedded development, switch statements are often used to implement state machines:

```c
typedef enum {
    STATE_IDLE,
    STATE_RUNNING,
    STATE_ERROR
} SystemState;

void state_machine(SystemState state) {
    switch (state) {
        case STATE_IDLE:
            // Idle logic
            break;
        case STATE_RUNNING:
            // Running logic
            break;
        case STATE_ERROR:
            // Error logic
            break;
    }
}
```

### 3.2 Loop Statements

The **for loop** is typically used when the number of iterations is known:

```c
for (int i = 0; i < 10; i++) {
    // Loop body
}
```

The **while loop** is used when the condition is unknown or depends on calculations within the loop body:

```c
while (condition) {
    // Loop body
}
```

The **do-while loop** executes the loop body at least once and is suitable for certain initialization scenarios:

```c
do {
    // Initialization code
} while (!ready());
```

In embedded systems, an infinite loop is the standard structure for the main program:

```c
while (1) {
    // Main loop
    // or
    // for (;;) { ... }
}
```

### 3.3 Jump Statements

The **break statement** is used to exit a loop or switch statement early:

```c
while (1) {
    if (error) break;
    // ...
}
```

The **continue statement** skips the remainder of the current iteration and continues to the next:

```c
for (int i = 0; i < 100; i++) {
    if (i % 2 == 0) continue; // Skip even numbers
    // Process odd numbers
}
```

Although the **goto statement** is often criticized, in embedded C, it has reasonable use cases in error handling and resource cleanup scenarios:

```c
int init_peripherals() {
    if (init_uart() != OK) goto cleanup;
    if (init_spi() != OK) goto cleanup_uart;
    if (init_i2c() != OK) goto cleanup_spi;

    return OK;

cleanup_spi:
    deinit_spi();
cleanup_uart:
    deinit_uart();
cleanup:
    return ERROR;
}
```

## 4. Functions

I recall another term for functions: subroutines. A function completes a piece of logic and is code for people to read. From this perspective, the foundation of modular programming in C is functions.

> I've actually met some friends who think function calls waste time and thus shouldn't write functions—well, the first part is right, but the rest is wrong. They clearly don't know that modern compilers optimize unnecessary function calls by inlining them (i.e., directly inserting fragments at the call site, saving the time consumed by pushing/popping stacks and refreshing the pipeline). Besides, do you really need to care about function call time to this extent?

### 4.1 Function Definition and Declaration

```c
// Function declaration (prototype)
int add(int a, int b);

// Function definition
int add(int a, int b) {
    return a + b;
}
```

### 4.2 Function Parameter Passing

C uses pass-by-value, but the effect of pass-by-reference can be achieved through pointers:

```c
void swap(int *a, int *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}
```

In embedded development, use pointers when passing large structures to avoid expensive copies:

```c
// Inefficient: copies the entire structure
void process_data(Data_t data);

// Efficient: only passes the address
void process_data(const Data_t *data);
```

### 4.3 Inline Functions

Modern `inline` no longer means **inline function**—this is something you must be aware of when writing C++. It refers to allowing duplicate definitions. To a certain extent, it eliminates a separate symbol encoding to avoid conflicts—C compilers also optimize actively nowadays. So, if you find that your compiler actually respects this keyword, then write it; otherwise, there is no need to write it.

```c
inline static int square(int x) {
    return x * x;
}
```

### 4.4 Function Pointers and Callbacks

Function pointers are a basic building block for implementing callbacks. A callback is just that—calling back. We store the address of a function, and when needed, we **call back**, effectively storing the processing flow!

```c
typedef void (*event_handler_t)(int event_id);

void register_callback(event_handler_t handler) {
    // Store handler for later use
}

void my_handler(int event_id) {
    // Handle event
}

int main() {
    register_callback(my_handler);
    // ...
}
```

Function pointers can also be used to implement simple polymorphism. I recall a nice embedded C tutorial that wrote an example of C-based polymorphism, but unfortunately, I've forgotten the book title (sweat).

```c
struct Device {
    void (*init)(void);
    void (*read)(uint8_t *data);
};

void uart_init(void) { /* ... */ }
void uart_read(uint8_t *data) { /* ... */ }

struct Device uart_dev = { uart_init, uart_read };
```

## 5. Pointers

Pointers are the most powerful yet error-prone feature in C, and they are particularly important in embedded programming. Since this is a quick refresher, I will just give you a flash review of C pointers.

### 5.1 Pointer Basics

```c
int value = 10;
int *ptr = &value; // ptr holds the address of value

*ptr = 20; // Dereference to modify value
```

### 5.2 Pointers and Arrays

In most cases, an array name decays into a pointer to its first element. However, note this—**arrays are not pointers!!!**

```c
int arr[5] = {1, 2, 3, 4, 5};
int *ptr = arr; // Equivalent to &arr[0]

// Pointer arithmetic
ptr++; // Points to arr[1]
```

### 5.3 Multi-level Pointers

This thing reminds me of a meme—a person pointing at a person pointing at a person.jpg. Yes, that's exactly what it means. A pointer to a pointer variable that points to a pointer variable that points to a variable. Hmm, it's dizzying. I suggest unless absolutely necessary, don't play this game; you are burying a huge trap for your colleagues.

```c
int value = 10;
int *ptr1 = &value;
int **ptr2 = &ptr1;
int ***ptr3 = &ptr2;

// Accessing value
***ptr3 = 20;
```

Multi-level pointers are useful when allocating two-dimensional arrays dynamically, but dynamic memory allocation should be used cautiously in embedded systems.

### 5.4 Pointers and const

The combination of `const` and pointers has multiple meanings:

```c
// Pointer to constant data (data cannot be modified via ptr)
const int *ptr1 = &value;
int const *ptr2 = &value; // Same as above

// Constant pointer (address cannot be changed)
int * const ptr3 = &value;

// Constant pointer to constant data
const int * const ptr4 = &value;
```

## 6. Arrays and Strings

### 6.1 Arrays

Arrays are contiguous collections of elements of the same type:

```c
int arr[10]; // Declaration
arr[0] = 1;  // Access
```

In embedded systems, arrays are often used for buffers and lookup tables:

```c
// Sine table
const float sin_table[360] = { /* ... */ };

// Communication buffer
uint8_t tx_buffer[256];
```

### 6.2 Strings

Strings in C are character arrays ending with a null character `\0`:

```c
char str[] = "Hello"; // Actually 6 bytes: 'H', 'e', 'l', 'l', 'o', '\0'
```

In embedded systems, prefer safe function versions with length limits:

```c
char buffer[10];
strncpy(buffer, "Hello", sizeof(buffer) - 1);
buffer[sizeof(buffer) - 1] = '\0'; // Ensure null termination
```

String handling considerations:

- Ensure the destination buffer is large enough
- Always ensure the string ends with `\0`
- In resource-constrained systems, consider using fixed-size buffers to avoid dynamic allocation

## 7. Structures, Unions, and Enumerations

### 7.1 Structures

Structures allow combining different types of data into a single unit:

```c
struct Person {
    char name[20];
    int age;
};

struct Person p1 = {"Alice", 30};
```

In embedded development, structures are widely used to represent configurations, states, and packets:

```c
struct Config {
    uint32_t baudrate;
    uint8_t  parity;
    bool     enable_crc;
};
```

### 7.2 Bit Fields

Bit fields allow allocating storage in a structure by bits, which is extremely useful when dealing with hardware registers:

```c
struct Flags {
    unsigned int flag1 : 1;
    unsigned int flag2 : 1;
    unsigned int reserved : 6;
};

struct Flags status;
status.flag1 = 1;
```

Note: The implementation of bit fields depends on the compiler and platform. Use cautiously when precise control is needed.

### 7.3 Unions

All members of a union share the same block of memory, used to save space or for type punning:

```c
union Data {
    int i;
    float f;
    char bytes[4];
};
```

In embedded programming, unions are often used for data type conversion and protocol processing:

```c
union FloatBytes {
    float value;
    uint8_t bytes[4];
};

// Send float over UART
union FloatBytes data;
data.value = 3.14f;
uart_send(data.bytes, 4);
```

### 7.4 Enumerations

Enumerations define named sets of integer constants, improving code readability:

```c
enum Color {
    RED,
    GREEN,
    BLUE
};

enum Color c = RED;
```

Enumerations in embedded development are often used to define states, command codes, and configuration options:

```c
enum State {
    STATE_IDLE,
    STATE_RUN,
    STATE_ERROR
};
```

## 8. Preprocessor

The preprocessor processes source code before compilation. It is an important source of C's flexibility and is particularly important in embedded development.

### 8.1 Macro Definitions

```c
#define PI 3.14159f
#define MAX(a, b) ((a) > (b) ? (a) : (b))
```

Macro considerations:

- Parameters should be enclosed in parentheses to avoid precedence issues
- Multi-line macros should be wrapped in `do-while(0)`
- Macros do not perform type checking; be careful when using them

Typical applications in embedded development:

```c
// Register access
#define REG_BASE 0x40000000
#define REG_CTRL (*(volatile uint32_t*)(REG_BASE + 0x00))

// Bit manipulation
#define SET_BIT(reg, bit) ((reg) |= (1U << (bit)))
#define CLR_BIT(reg, bit) ((reg) &= ~(1U << (bit)))
```

### 8.2 Conditional Compilation

Conditional compilation allows selective inclusion or exclusion of code based on conditions. This is a fundamental tool for cross-platform implementation.

```c
#ifdef STM32F407xx
    #include "stm32f4xx_hal.h"
#elif defined(ESP32)
    #include "esp32_hal.h"
#else
    #error "Platform not supported"
#endif
```

### 8.3 File Inclusion

```c
#include <stdio.h>  // Standard library
#include "my_header.h" // User header
```

### 8.4 Predefined Macros

Compilers provide some useful predefined macros:

```c
void debug_info() {
    printf("File: %s, Line: %d, Date: %s, Time: %s\n",
           __FILE__, __LINE__, __DATE__, __TIME__);
}
```

## 9. Storage Classes and Scope

### 9.1 Storage Classes

C provides several storage class specifiers:

**auto**: The default storage class for local variables, rarely used explicitly:

```c
auto int count = 0; // Equivalent to: int count = 0;
```

**static**: Has two main uses.

Static local variables retain their values between function calls:

```c
void counter() {
    static int count = 0; // Initialized only once
    count++;
}
```

Static global variables and functions limit scope to the current file:

```c
static int private_var = 0; // Only visible in this file

static void helper_function() { ... }
```

**extern**: Declares that a variable or function is defined in another file:

```c
// In file1.c
int global_value = 10;

// In file2.c
extern int global_value; // Reference to definition in file1.c
```

**register**: Suggests to the compiler that the variable be stored in a register (modern compilers usually ignore this):

```c
register int fast_counter = 0;
```

### 9.2 Scope Rules

C has four scopes: file scope, function scope, block scope, and function prototype scope.

In embedded development, using scope reasonably can avoid naming conflicts and unexpected side effects:

```c
int global_var; // File scope

void function() {
    int local_var; // Block scope

    {
        int nested_var; // Nested block scope
    }
}
```

## 10. Memory Management

### 10.1 Dynamic Memory Allocation

Although dynamic memory allocation should be avoided in embedded systems (due to memory fragmentation and uncertainty), understanding these functions is still important:

```c
int *ptr = (int*)malloc(sizeof(int) * 10); // Allocate
if (ptr != NULL) {
    // Use memory
    free(ptr); // Release
}
```

### 10.2 Memory Layout

Understanding the memory layout of a program is crucial for embedded development. We will cover this in a more specialized section later, so we will just pass over it here.

```text
+------------------+
|      .text       |  Code (Flash)
+------------------+
|      .data       |  Initialized Data (RAM)
+------------------+
|      .bss        |  Uninitialized Data (RAM)
+------------------+
|      Heap       |  Dynamic Memory
+------------------+
|      Stack      |  Local Variables
+------------------+
```

In embedded systems, we usually need precise control over where variables are stored:

```c
// Store in Flash (const)
const uint32_t config[10] = { /* ... */ };

// Store in specific RAM section
__attribute__((section(".bss.sram"))) uint8_t buffer[1024];
```
