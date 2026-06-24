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
  source_hash: 9311fc4ebecbbd5f1f81c728777eafa30c56d156518c15bc13349b5670c47c4c
  translated_at: '2026-06-24T00:27:45.279734+00:00'
  engine: anthropic
  token_count: 5758
---
# A Quick C Refresher

> The complete repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to visit, and if you like it, give the project a Star to motivate the author.

Although it should be noted that C++ can no longer be described today as a **simple C superset**, C++ was originally designed to be largely compatible with C. Therefore, we assume that everyone here is capable of writing functional business logic code for embedded systems using C. Thus, this section serves as a quick, comprehensive refresher on the common sense parts of the C language.

## 1. Basic Data Types and Type Modifiers

It is worth mentioning that C itself is a **strongly typed** programming language. Clarifying what a variable is has been a standard requirement since the inception of C.

> I know some might bring up `auto`. While `auto` is indeed excellent for saving time when writing complex types, my stance is: do not abuse it.

The C type system is the foundation of the entire language. In embedded development, accurately understanding the size and range of data types is particularly important because hardware resources are often constrained. We must also keep this in mind when writing C++.

### 1.1 The Integer Family

C provides a rich set of integer types, each with its specific purpose and range. It is important to note that, while the `char` type is fixed at 8 bits on some platforms, the actual size of other integer types is implementation-defined.

```c
char c = 'A';              // 至少8位，通常用于字符
short s = 100;             // 至少16位
int i = 1000;              // 至少16位，通常为32位
long l = 100000L;          // 至少32位
long long ll = 100000LL;   // 至少64位（C99标准引入）

```

In embedded systems, we often need precise control over the size of data types. The `stdint.h` header file introduced by the C99 standard provides fixed-width integer types. This is extremely important when writing portable embedded code, especially for foundational libraries that might be used on architectures ranging from 32-bit to 64-bit (the author notes that 64-bit chips for embedded platforms are slowly emerging, so this is definitely something we need to care about).

```c
#include <stdint.h>

int8_t   i8 = -128;        // 精确8位有符号整数
uint8_t  u8 = 255;         // 精确8位无符号整数
int16_t  i16 = -32768;     // 精确16位有符号整数
uint16_t u16 = 65535;      // 精确16位无符号整数
int32_t  i32 = -2147483648;// 精确32位有符号整数
uint32_t u32 = 4294967295U;// 精确32位无符号整数

```

So, the question is: when should we use which size? Well, this doesn't have to be overly rigid, but there is one thing we must keep in mind—**your data range must be sufficient**. So, the next question is: **how large a value can an N-bit data type actually hold**? For an **unsigned integer**, N bits can represent **2ⁿ distinct values**, with a range of **0 ~ 2ⁿ − 1**. What about a **signed integer**? The most significant bit is used as the sign bit, and using two's complement representation, the range is **−2ⁿ⁻¹ ~ 2ⁿ⁻¹ − 1**. Since we are all embedded programmers here, we should be able to handle this binary math.

### 1.2 Floating-Point Types

Floating-point types are used to represent real numbers. However, we must use them with caution in embedded systems, as many microcontrollers do not support hardware floating-point operations, and software emulation incurs significant performance overhead.

```c
float f = 3.14f;           // 单精度，通常32位，精度约7位十进制数
double d = 3.14159265359;  // 双精度，通常64位，精度约15位十进制数
long double ld = 3.14L;    // 扩展精度，至少与double相同

```

In some resource-constrained embedded systems, if we must use floating-point arithmetic, we should prioritize `float` over `double`. This is because it consumes less memory and computational resources, as `double` can sometimes be too demanding.

### 1.3 Type Modifiers

Type modifiers can alter the properties of fundamental types and hold special importance in embedded programming.

#### signed and unsigned

The `unsigned` modifier extends the representation range of an integer variable to non-negative numbers only. This is particularly useful when dealing with hardware register values and bit masks:

```c
unsigned int counter = 0;       // 范围：0 到 4294967295（32位系统）
signed int temperature = -40;   // 范围：-2147483648 到 2147483647

```

#### The `const` Qualifier

The `const` keyword declares a variable as read-only, which serves multiple purposes in embedded development. First, it helps the compiler optimize code by placing **constant data in ROM or Flash rather than in RAM**, thus conserving valuable RAM resources. Second, it provides compile-time safety checks to prevent accidental modification of data that should remain unchanged. This is often important, as it emphasizes that the value is an invariant within the current logic (of course, C++ also provides the more powerful `constexpr`, which we will discuss when we dive deeper into C++).

```c
const int MAX_BUFFER_SIZE = 256;           // 常量整数
const uint8_t lookup_table[] = {0, 1, 4, 9, 16, 25};  // 常量数组，可存放在Flash中

```

Using `const` in function parameters clearly indicates that the function will not modify the passed data, which is a best practice when designing APIs:

```c
void process_data(const uint8_t* data, size_t length) {
    // 函数承诺不修改data指向的内容
}

```

#### The volatile Qualifier

The literal meaning of `volatile` is "changeable." It is an extremely important, yet easily misunderstood, keyword in embedded C programming. Its core purpose is not to "disable compiler optimization," but rather to **explicitly tell the compiler: the value of this variable might change outside the current program's control flow**. In embedded systems, such "external" changes typically originate from hardware peripherals, interrupt service routines (ISRs), DMA, or other concurrent execution contexts.

Consequently, when the compiler encounters an object qualified with `volatile`, **it cannot assume the variable remains unchanged between two accesses**. Every read and write of a `volatile` variable counts as an **observable behavior** in the abstract machine model. These operations must physically occur in memory and cannot be cached in registers, merged, or eliminated entirely. This does not mean the compiler "cannot optimize at all"; rather, it cannot make a "value stability" assumption about the `volatile` object. Code unrelated to it can still be optimized normally.

In embedded programming, the most common use case for `volatile` is passing status information between interrupts and the main loop. For example, an event flag set in an interrupt callback and polled in the main loop must be declared as `volatile`. Otherwise, at higher optimization levels, the compiler might assume the variable is never modified in the main loop, leading it to hoist, cache, or even eliminate the read operation, causing program behavior to deviate severely from expectations.

From another perspective, if a normal variable is written to consecutively with different values within the same execution path, but no intermediate observable behavior depends on it, the compiler has every reason—without `volatile`—to consider these writes "redundant" and eliminate them. Once declared `volatile`, however, these writes become non-eliminable memory accesses that must strictly occur in order.

It is particularly important to emphasize that `volatile` only addresses **visibility at the compiler level**. It does not guarantee atomicity, nor does it provide any thread synchronization or memory ordering semantics. Compound operations on `volatile` variables (such as incrementing) can still produce race conditions in interrupt or multi-threaded environments. If a program requires atomicity or synchronization guarantees, we must use mechanisms like disabling interrupts, locks, atomic instructions, or dedicated concurrency primitives. This is why every operating system encapsulates and provides locking primitives.

```c
volatile uint32_t* const GPIO_IDR = (volatile uint32_t*)0x40020010;  // GPIO输入数据寄存器
volatile uint8_t uart_rx_flag = 0;  // 在中断中被修改的标志

void UART_IRQHandler(void) {
    uart_rx_flag = 1;  // 中断中修改
}

int main(void) {
    while (uart_rx_flag == 0) {
        // 如果没有volatile，编译器可能优化掉这个循环
    }
}

```

Additionally, when accessing hardware registers, we typically need to use both `volatile` and `const`. I believe those of you who have read the SDK are already aware of this.

```c
#define RCC_BASE    0x40023800
#define RCC_AHB1ENR (*(volatile uint32_t*)(RCC_BASE + 0x30))  // 可读可写的寄存器
```

## 2. Operators and Expressions

### 2.1 Arithmetic Operators

C provides standard arithmetic operators, but we need to be aware of overflow and type promotion issues when using them in embedded systems:

```c
int a = 10, b = 3;
int sum = a + b;        // 加法：13
int diff = a - b;       // 减法：7
int product = a * b;    // 乘法：30
int quotient = a / b;   // 整数除法：3（截断）
int remainder = a % b;  // 取模：1

```

In embedded development, division and modulo operations are often expensive, especially on MCUs without a hardware divider. In performance-critical code, we should avoid division operations, or replace divisions by powers of two with bit shifts:

```c
uint32_t value = 1024;
uint32_t div_by_2 = value >> 1;   // 相当于 value / 2，但更快
uint32_t div_by_8 = value >> 3;   // 相当于 value / 8

```

### 2.2 Bitwise Operators

Bitwise operators are core tools in embedded programming. They operate directly on the binary bits of data and are commonly used for hardware register configuration, flag management, and efficient mathematical operations.

```c
uint8_t a = 0b10110011;  // 二进制字面量（C23标准，部分编译器支持）
uint8_t b = 0b11001010;

// 按位与：两位都为1时结果为1
uint8_t and_result = a & b;  // 0b10000010

// 按位或：任一位为1时结果为1
uint8_t or_result = a | b;   // 0b11111011

// 按位异或：两位不同时结果为1
uint8_t xor_result = a ^ b;  // 0b01111001

// 按位取反：0变1，1变0
uint8_t not_result = ~a;     // 0b01001100

// 左移：向左移动位，右侧补0
uint8_t left_shift = a << 2; // 0b11001100

// 右移：向右移动位
uint8_t right_shift = a >> 2;// 0b00101100（逻辑右移，无符号数）

```

Typical applications of bitwise operations in embedded development include:

**Register bit manipulation**:

```c
// 设置某一位
#define SET_BIT(reg, bit)    ((reg) |= (1 << (bit)))

// 清除某一位
#define CLEAR_BIT(reg, bit)  ((reg) &= ~(1 << (bit)))

// 切换某一位
#define TOGGLE_BIT(reg, bit) ((reg) ^= (1 << (bit)))

// 读取某一位
#define READ_BIT(reg, bit)   (((reg) >> (bit)) & 1)

// 示例：配置GPIO
SET_BIT(GPIOA->MODER, 10);    // 设置PA5的模式位
CLEAR_BIT(GPIOA->ODR, 5);     // 清除PA5的输出

```

**Bit-field mask**:

```c
#define STATUS_READY    0x01  // 0b00000001
#define STATUS_BUSY     0x02  // 0b00000010
#define STATUS_ERROR    0x04  // 0b00000100
#define STATUS_TIMEOUT  0x08  // 0b00001000

uint8_t status = 0;
status |= STATUS_READY;              // 设置就绪标志
if (status & STATUS_ERROR) {         // 检查错误标志
    // 处理错误
}
status &= ~STATUS_BUSY;              // 清除忙碌标志

```

### 2.3 Relational and Logical Operators

Relational operators are used for comparisons and return an integer result (0 for false, non-zero for true):

```c
int a = 5, b = 10;
int equal = (a == b);        // 等于：0
int not_equal = (a != b);    // 不等于：1
int less = (a < b);          // 小于：1
int greater = (a > b);       // 大于：0
int less_equal = (a <= b);   // 小于等于：1
int greater_equal = (a >= b);// 大于等于：0

```

Logical operators feature short-circuit evaluation, which we can leverage for conditional optimization in embedded programming:

```c
// 逻辑与：左侧为假时不评估右侧
if (ptr != NULL && *ptr == 0) {  // 安全检查，防止空指针解引用
    // 处理
}

// 逻辑或：左侧为真时不评估右侧
if (error_flag || check_critical_condition()) {
    // 当error_flag为真时，不会调用函数
}

// 逻辑非
if (!is_ready) {
    // 等待就绪
}

```

### 2.4 Other Important Operators

The **ternary conditional operator** is the only ternary operator in C, and it can simplify simple if-else statements:

```c
int max = (a > b) ? a : b;  // 等价于 if (a > b) max = a; else max = b;

// 在嵌入式中的应用
uint8_t clamp(uint8_t value, uint8_t min, uint8_t max) {
    return (value < min) ? min : ((value > max) ? max : value);
}

```

The `sizeof` operator returns the size in bytes of a type or object. It is evaluated at compile time and is commonly used for calculating array sizes:

```c
uint32_t array[10];
size_t array_size = sizeof(array);           // 40字节（假设uint32_t为4字节）
size_t element_count = sizeof(array) / sizeof(array[0]);  // 10个元素

// 在嵌入式中用于缓冲区管理
uint8_t buffer[256];
void clear_buffer(void) {
    memset(buffer, 0, sizeof(buffer));
}

```

The **comma operator** evaluates expressions from left to right and returns the value of the rightmost expression:

```c
int x = (a = 5, b = a + 10, b * 2);  // x = 30

// 在for循环中常见
for (int i = 0, j = 10; i < j; i++, j--) {
    // 同时更新两个变量
}

```

## 3. Control Flow Statements

### 3.1 Conditional Statements

The **if-else statement** is the most basic conditional branch:

```c
if (temperature > TEMP_HIGH_THRESHOLD) {
    activate_cooling();
} else if (temperature < TEMP_LOW_THRESHOLD) {
    activate_heating();
} else {
    maintain_temperature();
}

```

In embedded systems, for multiple mutually exclusive conditions, using an else-if chain avoids unnecessary conditional checks and improves execution efficiency.

The **switch statement** is suitable for multi-way branching. Compilers usually optimize it into a jump table, which can be more efficient than multiple if-else statements in some cases:

```c
switch (command) {
    case CMD_START:
        start_operation();
        break;

    case CMD_STOP:
        stop_operation();
        break;

    case CMD_PAUSE:
        pause_operation();
        break;

    case CMD_RESUME:
        resume_operation();
        break;

    default:
        handle_unknown_command();
        break;
}

```

In embedded development, the switch statement is often used to implement state machines:

```c
typedef enum {
    STATE_IDLE,
    STATE_RUNNING,
    STATE_PAUSED,
    STATE_ERROR
} SystemState;

SystemState current_state = STATE_IDLE;

void state_machine_update(void) {
    switch (current_state) {
        case STATE_IDLE:
            if (start_button_pressed()) {
                current_state = STATE_RUNNING;
                initialize_operation();
            }
            break;

        case STATE_RUNNING:
            perform_operation();
            if (error_detected()) {
                current_state = STATE_ERROR;
            } else if (pause_button_pressed()) {
                current_state = STATE_PAUSED;
            }
            break;

        case STATE_PAUSED:
            if (resume_button_pressed()) {
                current_state = STATE_RUNNING;
            }
            break;

        case STATE_ERROR:
            handle_error();
            if (reset_button_pressed()) {
                current_state = STATE_IDLE;
            }
            break;
    }
}

```

### 3.2 Loop Statements

**for loops** are typically used when the number of iterations is known:

```c
// 传统for循环
for (int i = 0; i < 10; i++) {
    array[i] = i * i;
}

// 在嵌入式中常见的循环模式
for (size_t i = 0; i < ARRAY_SIZE; i++) {
    process_element(array[i]);
}

// 无限循环（在嵌入式主循环中常见）
for (;;) {
    // 永远执行
    process_tasks();
}

```

We use a **while loop** when the condition is unknown or depends on calculations within the loop body:

```c
while (uart_data_available()) {
    uint8_t data = uart_read();
    process_data(data);
}

// 嵌入式中的典型等待循环
while (!is_ready()) {
    // 等待就绪
}

```

The **do-while loop** executes the loop body at least once, making it suitable for certain initialization scenarios:

```c
uint8_t retry_count = 0;
do {
    result = attempt_communication();
    retry_count++;
} while (result != SUCCESS && retry_count < MAX_RETRIES);

```

In embedded systems, an infinite loop is the standard structure for the main program:

```c
int main(void) {
    system_init();
    peripherals_init();

    while (1) {  // 或 for(;;)
        // 主循环
        read_sensors();
        process_data();
        update_outputs();
        handle_communication();
    }
}

```

### 3.3 Jump Statements

The **break statement** is used to exit a loop or switch statement early:

```c
for (int i = 0; i < MAX_ITEMS; i++) {
    if (items[i] == target) {
        found_index = i;
        break;  // 找到目标，退出循环
    }
}

```

The **`continue` statement** skips the remainder of the current iteration and proceeds to the next iteration:

```c
for (int i = 0; i < data_count; i++) {
    if (data[i] == INVALID_VALUE) {
        continue;  // 跳过无效数据
    }
    process_valid_data(data[i]);
}

```

Although the **goto statement** is often criticized, in embedded C, it has legitimate use cases for error handling and resource cleanup:

```c
int initialize_system(void) {
    if (!init_hardware()) {
        goto error_hardware;
    }

    if (!init_peripherals()) {
        goto error_peripherals;
    }

    if (!init_communication()) {
        goto error_communication;
    }

    return SUCCESS;

error_communication:
    cleanup_peripherals();
error_peripherals:
    cleanup_hardware();
error_hardware:
    return ERROR;
}

```

## 4. Functions

I recall that functions are also known as subroutines. A function is simply a block of logic designed to be read by humans. From this perspective, functions are the foundation of modular programming in C.

> I have actually met developers who believed that function calls were a waste of time and argued against writing functions. They were right about the overhead, but wrong about the conclusion, because they clearly overlooked how modern compilers optimize away unnecessary function calls via inlining (inserting code directly at the call site to save time spent pushing/popping the stack and flushing the pipeline). Besides, do you really need to optimize to the point where you must worry about the time taken by a function jump?

### 4.1 Function Definition and Declaration

```c
// 函数声明（原型）
int calculate_checksum(const uint8_t* data, size_t length);

// 函数定义
int calculate_checksum(const uint8_t* data, size_t length) {
    int checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum += data[i];
    }
    return checksum & 0xFF;
}

```

### 4.2 Function Parameter Passing

C uses pass-by-value, but we can achieve the effect of pass-by-reference using pointers:

```c
// 值传递：修改不影响原变量
void swap_wrong(int a, int b) {
    int temp = a;
    a = b;
    b = temp;
}

// 指针传递：可以修改原变量
void swap_correct(int* a, int* b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

// 使用
int x = 10, y = 20;
swap_correct(&x, &y);  // x和y被交换

```

In embedded development, we should use pointers when passing large structures to avoid expensive copies:

```c
typedef struct {
    uint32_t timestamp;
    float temperature;
    float humidity;
    uint16_t pressure;
} SensorData;

// 低效：传递整个结构体
void process_data_inefficient(SensorData data) {
    // 处理数据
}

// 高效：传递指针
void process_data_efficient(const SensorData* data) {
    // 处理数据，使用data->temperature访问成员
}

```

### 4.3 Inline Functions

In modern C++, `inline` no longer implies **inline expansion**—this is a crucial distinction to keep in mind when writing C++. Instead, it signifies that multiple definitions are allowed. By effectively eliminating a distinct symbol encoding, it avoids linkage conflicts—modern C compilers handle optimization automatically anyway. Therefore, only use this keyword if you find that your compiler actually requires it; otherwise, there is no need to write it.

```c
// C99标准的内联函数
static inline uint16_t swap_bytes(uint16_t value) {
    return (value >> 8) | (value << 8);
}

// 宏定义方式（传统方法，但类型不安全）
#define SWAP_BYTES(x) (((x) >> 8) | ((x) << 8))
```

### 4.4 Function Pointers and Callbacks

Function pointers are a basic building block for implementing callbacks. A callback is exactly what it sounds like—calling back. We store the address of a function, and when needed, we **call back** to it. This is effectively storing the control flow

```c
// 定义函数指针类型
typedef void (*EventCallback)(void* context);

// 回调注册系统
typedef struct {
    EventCallback callback;
    void* context;
} EventHandler;

EventHandler button_handler;

void register_button_callback(EventCallback callback, void* context) {
    button_handler.callback = callback;
    button_handler.context = context;
}

// 在中断或主循环中调用
void handle_button_event(void) {
    if (button_handler.callback != NULL) {
        button_handler.callback(button_handler.context);
    }
}

```

Function pointers can also be used to implement simple polymorphism. We recall an excellent embedded C tutorial that included a good example of C-based polymorphism, but unfortunately, we have forgotten the title (sweat).

```c
typedef int (*MathOperation)(int, int);

int add(int a, int b) { return a + b; }
int subtract(int a, int b) { return a - b; }
int multiply(int a, int b) { return a * b; }

int perform_operation(MathOperation op, int x, int y) {
    return op(x, y);
}

// 使用
int result = perform_operation(add, 10, 5);  // 15

```

## 5. Pointers

Pointers are the most powerful yet error-prone feature of the C language, and they play a particularly crucial role in embedded programming. Since this is a rapid review, we will briefly brush up on C pointers.

### 5.1 Pointer Basics

```c
int value = 42;
int* ptr = &value;       // ptr存储value的地址
int deref = *ptr;        // 解引用，deref = 42
*ptr = 100;              // 通过指针修改value

// 空指针
int* null_ptr = NULL;    // 应始终初始化指针

// 指针算术
int array[5] = {1, 2, 3, 4, 5};
int* p = array;
p++;                     // 指向array[1]
int val = *(p + 2);      // 访问array[3]，val = 4

```

### 5.2 Pointers and Arrays

In most cases, an array name decays into a pointer to its first element. However, we must be careful—arrays are not pointers!

```c
int numbers[10];
int* ptr = numbers;      // 等价于 &numbers[0]

// 数组访问的两种方式
numbers[3] = 42;         // 下标方式
*(ptr + 3) = 42;         // 指针方式，等价

// 指针遍历数组
for (int* p = numbers; p < numbers + 10; p++) {
    *p = 0;
}

```

### 5.3 Multi-level Pointers

This concept reminds me of a meme—a person pointing at a person pointing at a person. Yes, that is exactly what it means. It is a pointer variable pointing to a pointer variable pointing to a pointer variable pointing to a variable. It is enough to make your head spin. We suggest avoiding this unless absolutely necessary; otherwise, you are setting a nasty trap for your colleagues.

```c
int value = 42;
int* ptr = &value;
int** ptr_ptr = &ptr;    // 指向指针的指针

// 解引用
int val1 = *ptr;         // 42
int val2 = **ptr_ptr;    // 42

```

Multilevel pointers are useful when dynamically allocating two-dimensional arrays, but we should be cautious when using dynamic memory allocation in embedded systems.

### 5.4 Pointers and const

Combinations of `const` and pointers can have various meanings:

```c
int value = 42;

// 指向常量的指针：不能通过ptr修改value
const int* ptr1 = &value;
// *ptr1 = 100;  // 错误
ptr1 = &other;   // 可以，指针本身可以改变

// 常量指针：指针本身不能改变
int* const ptr2 = &value;
*ptr2 = 100;     // 可以，可以修改指向的值
// ptr2 = &other;  // 错误，指针不能改变

// 指向常量的常量指针：都不能改变
const int* const ptr3 = &value;
// *ptr3 = 100;    // 错误
// ptr3 = &other;  // 错误

```

## 6. Arrays and Strings

### 6.1 Arrays

Arrays are contiguous collections of elements of the same type:

```c
// 一维数组
int numbers[10];                     // 声明
int primes[] = {2, 3, 5, 7, 11};    // 初始化，大小自动推导为5
int matrix[3][4];                    // 二维数组

// 数组初始化
int zeros[100] = {0};                // 全部初始化为0
int partial[10] = {1, 2};           // 前两个元素为1和2，其余为0

// 指定初始化器（C99）
int sparse[100] = {[5] = 10, [20] = 30};

```

In embedded systems, arrays are commonly used for buffers and lookup tables:

```c
// 串口接收缓冲区
uint8_t uart_rx_buffer[256];
volatile size_t rx_head = 0;
volatile size_t rx_tail = 0;

// 查找表（节省计算资源）
const uint8_t sin_table[360] = {
    // 预计算的正弦值（0-255范围）
    128, 130, 133, 135, // ...
};

```

### 6.2 Strings

Strings in C are character arrays terminated by a null character `'\0'`:

```c
char str1[10] = "Hello";             // 字符串字面量初始化
char str2[] = "World";               // 大小自动推导为6（包括'\0'）
char str3[10];                       // 未初始化

// 字符串操作（需要包含string.h）
#include <string.h>

strcpy(str3, str1);                  // 复制字符串
strcat(str3, str2);                  // 连接字符串
int len = strlen(str1);              // 获取长度
int cmp = strcmp(str1, str2);        // 比较字符串

```

In embedded systems, we should prioritize using secure function versions with length limits:

```c
char buffer[32];
strncpy(buffer, source, sizeof(buffer) - 1);
buffer[sizeof(buffer) - 1] = '\0';   // 确保以空字符结尾

// 更安全的做法
snprintf(buffer, sizeof(buffer), "Value: %d", value);

```

Considerations for string handling:

- Ensure the destination buffer is large enough.
- Always ensure strings are null-terminated with `'\0'`.
- In resource-constrained systems, consider using fixed-size buffers to avoid dynamic allocation.

## 7. Structures, Unions, and Enumerations

### 7.1 Structures

Structures allow us to combine data of different types into a single unit:

```c
// 定义结构体
struct Point {
    int x;
    int y;
};

// 使用typedef简化
typedef struct {
    int x;
    int y;
} Point;

// 创建和初始化
Point p1 = {10, 20};                 // 顺序初始化
Point p2 = {.y = 30, .x = 40};      // 指定初始化器（C99）

// 访问成员
p1.x = 100;
int y_value = p1.y;

// 指针访问
Point* ptr = &p1;
ptr->x = 200;                        // 等价于 (*ptr).x = 200

```

In embedded development, we widely use structures to represent configurations, states, and data packets:

```c
// 传感器数据结构
typedef struct {
    uint32_t timestamp;
    float temperature;
    float humidity;
    uint16_t light_level;
    uint8_t status;
} SensorReading;

// 通信协议数据包
typedef struct {
    uint8_t header;
    uint8_t command;
    uint16_t length;
    uint8_t data[256];
    uint16_t checksum;
} __attribute__((packed)) ProtocolPacket;  // 禁用对齐填充

```

### 7.2 Bit Fields

Bit fields allow us to allocate storage within a struct in units of bits, which is extremely useful when dealing with hardware registers:

```c
// 寄存器位域定义
typedef struct {
    uint32_t EN      : 1;   // 使能位
    uint32_t MODE    : 2;   // 模式选择（2位）
    uint32_t RESERVED: 5;   // 保留位
    uint32_t PRIORITY: 3;   // 优先级（3位）
    uint32_t         : 21;  // 未命名位域，填充
} ControlRegister;

// 使用
volatile ControlRegister* ctrl_reg = (ControlRegister*)0x40000000;
ctrl_reg->EN = 1;
ctrl_reg->MODE = 2;
ctrl_reg->PRIORITY = 7;

```

> **Note:** The implementation of bit fields depends on the compiler and platform. Use caution when precise control is required.

### 7.3 Unions

All members of a union share the same memory space, which is used to save space or perform type punning:

```c
// 基本联合体
union Data {
    int i;
    float f;
    char bytes[4];
};

union Data d;
d.i = 0x12345678;
printf("%02X", d.bytes[0]);  // 访问字节表示

```

In embedded programming, unions are commonly used for data type conversion and protocol handling:

```c
// 多类型数据容器
typedef union {
    uint32_t word;
    uint16_t halfword[2];
    uint8_t byte[4];
} DataConverter;

DataConverter dc;
dc.word = 0x12345678;
// 现在可以按字节访问：dc.byte[0], dc.byte[1], ...

// 结构体与联合体结合
typedef struct {
    uint8_t type;
    union {
        int int_value;
        float float_value;
        char string_value[16];
    } data;
} Variant;

```

### 7.4 Enumerations

Enumerations define a set of named integer constants, which improves code readability:

```c
// 基本枚举
enum Color {
    RED,      // 0
    GREEN,    // 1
    BLUE      // 2
};

// 指定值
enum Status {
    STATUS_OK = 0,
    STATUS_ERROR = -1,
    STATUS_BUSY = 1,
    STATUS_TIMEOUT = 2
};

// 使用typedef
typedef enum {
    STATE_IDLE,
    STATE_RUNNING,
    STATE_PAUSED,
    STATE_ERROR
} SystemState;

```

Enumerations are frequently used in embedded development to define states, command codes, and configuration options:

```c
// 命令定义
typedef enum {
    CMD_NOOP = 0x00,
    CMD_READ = 0x01,
    CMD_WRITE = 0x02,
    CMD_ERASE = 0x03,
    CMD_RESET = 0xFF
} Command;

// 错误码
typedef enum {
    ERR_NONE = 0,
    ERR_INVALID_PARAM = 1,
    ERR_TIMEOUT = 2,
    ERR_HARDWARE_FAULT = 3,
    ERR_OUT_OF_MEMORY = 4
} ErrorCode;

```

## 8. Preprocessor

The preprocessor handles source code before compilation begins. It is a key source of flexibility in the C language and plays a particularly important role in embedded development.

### 8.1 Macro Definitions

```c
// 对象宏
#define MAX_SIZE 100
#define PI 3.14159f
#define LED_PIN 13

// 函数宏
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ABS(x) ((x) < 0 ? -(x) : (x))

// 多行宏
#define SWAP(a, b, type) do { \
    type temp = (a);          \
    (a) = (b);                \
    (b) = temp;               \
} while(0)

```

### Cautions for Macros

- Parameters should be enclosed in parentheses to avoid precedence issues.
- Multi-line macros should be wrapped in `do-while(0)`.
- Macros do not perform type checking; use them with care.

### Typical Applications in Embedded Development

```c
// 寄存器位操作宏
#define BIT(n) (1UL << (n))
#define SET_BIT(reg, bit) ((reg) |= BIT(bit))
#define CLEAR_BIT(reg, bit) ((reg) &= ~BIT(bit))
#define READ_BIT(reg, bit) (((reg) >> (bit)) & 1UL)
#define TOGGLE_BIT(reg, bit) ((reg) ^= BIT(bit))

// 数组大小
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// 范围检查
#define IN_RANGE(x, min, max) (((x) >= (min)) && ((x) <= (max)))

// 字节对齐
#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))
```

### 8.2 Conditional Compilation

Conditional compilation allows us to selectively include or exclude code based on specific conditions. It is a fundamental tool for implementing cross-platform functionality.

```c
// 基本条件编译
#ifdef DEBUG
    #define DEBUG_PRINT(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

// 使用
DEBUG_PRINT("Value: %d\n", value);  // 仅在DEBUG定义时输出

// 平台相关代码
#if defined(STM32F4) || defined(STM32F7)
    #define MCU_FAMILY_STM32F4_F7
    #include "stm32f4xx.h"
#elif defined(STM32L4)
    #define MCU_FAMILY_STM32L4
    #include "stm32l4xx.h"
#else
    #error "Unsupported MCU family"
#endif

// 功能开关
#define FEATURE_USB 1
#define FEATURE_ETHERNET 0

#if FEATURE_USB
    void usb_init(void);
#endif

#if FEATURE_ETHERNET
    void ethernet_init(void);
#endif
```

### 8.3 File Inclusion

```c
// 系统头文件
#include <stdio.h>
#include <stdint.h>

// 用户头文件
#include "config.h"
#include "hal.h"

// 防止重复包含（头文件保护）
#ifndef CONFIG_H
#define CONFIG_H

// 头文件内容

#endif // CONFIG_H

// 或使用#pragma once（非标准但广泛支持）
#pragma once
```

### 8.4 Predefined Macros

Compilers provide several useful predefined macros:

```c
// 文件和行号
#define LOG_ERROR(msg) \
    fprintf(stderr, "Error in %s:%d - %s\n", __FILE__, __LINE__, msg)

// 函数名
void some_function(void) {
    DEBUG_PRINT("Entered %s\n", __func__);
}

// 日期和时间
printf("Compiled on %s at %s\n", __DATE__, __TIME__);

// 标准版本
#if __STDC_VERSION__ >= 199901L
    // C99或更高版本
#endif
```

## 9. Storage Classes and Scope

### 9.1 Storage Classes

The C language provides several storage class specifiers:

**auto**: This is the default storage class for local variables and is rarely used explicitly:

```c
void function(void) {
    auto int x = 10;  // 等价于 int x = 10;
}

```

**static**: This keyword has two main uses.

A static local variable preserves its value between function calls:

```c
void counter(void) {
    static int count = 0;  // 仅初始化一次
    count++;
    printf("Called %d times\n", count);
}

```

Static global variables and functions limit their scope to the current file:

```c
static int file_scope_var = 0;  // 只在本文件可见

static void helper_function(void) {
    // 只能在本文件内调用
}

```

**extern**: Declares that a variable or function is defined in another file:

```c
// file1.c
int global_counter = 0;

// file2.c
extern int global_counter;  // 声明，不分配存储空间
void increment(void) {
    global_counter++;
}

```

**register**: Suggests to the compiler that the variable be stored in a register (modern compilers usually ignore this):

```c
void fast_loop(void) {
    register int i;
    for (i = 0; i < 1000000; i++) {
        // 循环变量建议存储在寄存器
    }
}

```

### 9.2 Scope Rules

C has four types of scope: file scope, function scope, block scope, and function prototype scope.

In embedded development, proper use of scope helps us avoid naming conflicts and unintended side effects:

```c
// 文件作用域（全局）
int global_var = 0;
static int file_static_var = 0;  // 仅本文件可见

void function(void) {
    // 函数作用域
    int local_var = 0;

    if (condition) {
        // 块作用域
        int block_var = 0;
        // local_var和block_var都可见
    }
    // block_var在这里不可见
}

```

## 10. Memory Management

### 10.1 Dynamic Memory Allocation

Although we should generally avoid dynamic memory allocation in embedded systems due to memory fragmentation and non-determinism, understanding these functions remains important:

```c
#include <stdlib.h>

// 分配内存
int* array = (int*)malloc(10 * sizeof(int));
if (array == NULL) {
    // 分配失败处理
}

// 分配并清零
int* zeros = (int*)calloc(10, sizeof(int));

// 重新分配
array = (int*)realloc(array, 20 * sizeof(int));

// 释放内存
free(array);
array = NULL;  // 良好的实践

```

### 10.2 Memory Layout

Understanding the memory layout of a program is crucial for embedded development. We will cover this in detail in a later, dedicated section, so we will just provide a brief overview here.

```cpp

+------------------+  高地址
|      栈(Stack)   |  向下增长，存放局部变量和函数调用
+------------------+
|        ↓         |
|                  |
|     未分配       |
|                  |
|        ↑         |
+------------------+
|     堆(Heap)     |  向上增长，动态分配内存
+------------------+
|   BSS段          |  未初始化的全局变量和静态变量
+------------------+
|   数据段(Data)   |  初始化的全局变量和静态变量
+------------------+
|   代码段(Text)   |  程序代码（只读）
+------------------+  低地址

```

In embedded systems, we often need to precisely control the storage location of variables:

```c
// 放置在特定内存区域（编译器扩展）
__attribute__((section(".ccmram")))
static uint32_t fast_buffer[1024];

// 对齐要求
__attribute__((aligned(4)))
uint8_t dma_buffer[256];

// 禁止优化
__attribute__((used))
const uint32_t version = 0x01020304;

```
