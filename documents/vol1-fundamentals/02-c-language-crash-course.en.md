---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: A quick review of fundamental C syntax, covering core concepts such as
  data types, operators, control flow, pointers, arrays, and structs.
difficulty: beginner
order: 2
platform: host
prerequisites: []
reading_time_minutes: 40
related: []
tags:
- cpp-modern
- host
- intermediate
title: C Quick Review
translation:
  source: documents/vol1-fundamentals/02-c-language-crash-course.md
  source_hash: 78bfd7c6549bb701b4dcadf7c1f8066d4c634a89a0ec01cdf332192c3144f824
  translated_at: '2026-04-20T03:01:11.465436+00:00'
  engine: anthropic
  token_count: 5758
---
# A Quick C Language Review

> The full repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to check it out, and if you like it, give it a Star to encourage the author.

Although it should be noted that C++ can no longer be described as a **simple C superset** today, by design, C++ strives to be compatible with C. Here, we assume everyone has a solid enough grasp of C to write working business logic for one or more embedded systems. Therefore, this section serves as a quick, supplementary overview of C language essentials for the sake of completeness.

## 1. Basic Data Types and Type Modifiers

It is worth mentioning that C itself is a **strongly typed** programming language. Clarifying what a variable is has been a required standard practice since C's inception.

> I know some might bring up `auto`. While `auto` is indeed great for saving time when writing complex types, my stance is not to overuse it.

C's type system is the foundation of the entire language. In embedded development, accurately understanding the size and range of data types is especially important because hardware resources are often constrained. We must keep this in mind when writing C++ as well.

### 1.1 Integer Family

C provides a rich set of integer types, each with its specific use case and range. Note that except for the `char` type, which is fixed at 8 bits on some platforms, the actual size of other integers is implementation-defined.

```c
char c = 'A';              // 至少8位，通常用于字符
short s = 100;             // 至少16位
int i = 1000;              // 至少16位，通常为32位
long l = 100000L;          // 至少32位
long long ll = 100000LL;   // 至少64位（C99标准引入）

```

In embedded systems, we often need precise control over the size of data types. The `stdint.h` header introduced by the C99 standard provides fixed-width integers, which is extremely important when writing portable embedded code. This is particularly true for foundational libraries that might be used on both 32-bit and 64-bit platforms (I've noticed that 64-bit chips for embedded platforms are slowly emerging, so this is genuinely something to care about).

```c
#include <stdint.h>

int8_t   i8 = -128;        // 精确8位有符号整数
uint8_t  u8 = 255;         // 精确8位无符号整数
int16_t  i16 = -32768;     // 精确16位有符号整数
uint16_t u16 = 65535;      // 精确16位无符号整数
int32_t  i32 = -2147483648;// 精确32位有符号整数
uint32_t u32 = 4294967295U;// 精确32位无符号整数

```

So the question is: when do you use which size? Well, this doesn't need to be overly rigid, but one thing must be noted—**your data range must be sufficient**. Which leads to the next question: **how much can an N-bit value actually hold?** For an **unsigned integer**, N bits can represent **2ⁿ values**, with a range of **0 ~ 2ⁿ − 1**. What about a **signed integer**? The most significant bit is used as the sign bit. Using two's complement representation, the range is **−2ⁿ⁻¹ ~ 2ⁿ⁻¹ − 1**. We are all embedded programmers, so we should all be able to do this binary math.

### 1.2 Floating-Point Types

Floating-point types are used to represent real numbers, but using floating-point arithmetic in embedded systems requires extra caution because many MCUs lack hardware floating-point support, and software emulation incurs significant performance overhead.

```c
float f = 3.14f;           // 单精度，通常32位，精度约7位十进制数
double d = 3.14159265359;  // 双精度，通常64位，精度约15位十进制数
long double ld = 3.14L;    // 扩展精度，至少与double相同

```

In extremely resource-constrained embedded systems, if floating-point arithmetic is absolutely necessary, prefer `float` over `double` because it consumes less memory and computational resources. `double` can sometimes be too expensive.

### 1.3 Type Modifiers

Type modifiers can alter the properties of basic types, holding special importance in embedded programming.

#### signed and unsigned

The `unsigned` modifier extends the representation range of an integer variable to non-negative values only, which is very useful when handling hardware register values and bit masks:

```c
unsigned int counter = 0;       // 范围：0 到 4294967295（32位系统）
signed int temperature = -40;   // 范围：-2147483648 到 2147483647

```

#### const Modifier

The `const` keyword declares a variable as read-only, which serves multiple purposes in embedded development. First, it helps the compiler optimize by placing **constant data in ROM or Flash rather than RAM**, saving precious RAM resources. Second, it provides compile-time safety checks to prevent accidental modification of data that shouldn't change. This is sometimes very important, essentially emphasizing that within the current logic, this is an invariant (of course, C++ provides the even more powerful `constexpr`, which we will discuss when we get to C++).

```c
const int MAX_BUFFER_SIZE = 256;           // 常量整数
const uint8_t lookup_table[] = {0, 1, 4, 9, 16, 25};  // 常量数组，可存放在Flash中

```

Using `const` in function parameters clearly indicates that the function will not modify the passed data. This is a good practice when designing APIs:

```c
void process_data(const uint8_t* data, size_t length) {
    // 函数承诺不修改data指向的内容
}

```

#### volatile Modifier

The literal meaning of `volatile` is "changeable." It is an extremely important yet easily misunderstood keyword in embedded C programming. Its core purpose is not to "disable compiler optimization," but rather to **explicitly tell the compiler: the value of this variable might change outside the current program's control flow**. In embedded systems, such "out-of-control-flow" changes typically come from hardware peripherals, interrupt service routines (ISRs), DMA (Direct Memory Access), or other concurrently executing contexts.

Because of this, when the compiler encounters an object modified by `volatile`, **it cannot assume the variable remains unchanged between two accesses**. Every read and write of a `volatile` variable is considered an **observable behavior** in the abstract machine model, and must actually occur in memory. It cannot be cached in a register, merged, or directly eliminated. This doesn't mean the compiler "can't optimize at all," but rather that it cannot make a "value stability" assumption about the `volatile` object. Other unrelated code can still be optimized normally.

In embedded programming, the most common use case for `volatile` is passing state information between interrupts and the main loop. For example, an event flag that is set in an interrupt callback and polled in the main loop must be declared as `volatile`. Otherwise, at higher optimization levels, the compiler might assume the variable is never modified in the main loop, leading it to hoist, cache, or even optimize away the read operation, causing the program's behavior to deviate severely from expectations.

Looking at it from another angle, if a normal variable is written to with different values consecutively within the same execution path, but no observable behavior in between depends on it, then without `volatile`, the compiler has every reason to consider these writes "redundant" and eliminate them. Once the variable is declared as `volatile`, however, these writes all become non-eliminable memory accesses that must occur strictly in order.

It is particularly important to emphasize that `volatile` only solves **compiler-level visibility issues**. It does not guarantee atomicity, nor does it provide any thread synchronization or memory order semantics. Compound operations on `volatile` variables (such as incrementing) can still produce race conditions in interrupt or multi-threaded environments. If a program requires atomicity or synchronization guarantees, it must rely on disabling interrupts, locks, atomic instructions, or specialized concurrency primitives. This is why any operating system must encapsulate and provide lock primitives.

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

Additionally, when accessing hardware registers, it is usually necessary to use both `volatile` and `const` simultaneously. I believe anyone who has read an SDK knows this.

```c
#define RCC_BASE    0x40023800
#define RCC_AHB1ENR (*(volatile uint32_t*)(RCC_BASE + 0x30))  // 可读可写的寄存器
```

## 2. Operators and Expressions

### 2.1 Arithmetic Operators

C provides standard arithmetic operators, but when using them in embedded systems, we need to be mindful of overflow and type promotion:

```c
int a = 10, b = 3;
int sum = a + b;        // 加法：13
int diff = a - b;       // 减法：7
int product = a * b;    // 乘法：30
int quotient = a / b;   // 整数除法：3（截断）
int remainder = a % b;  // 取模：1

```

In embedded development, division and modulo operations are usually expensive, especially on MCUs without a hardware divider. In performance-critical code, we should avoid division operations, or replace divisions by powers of two with bit shifts:

```c
uint32_t value = 1024;
uint32_t div_by_2 = value >> 1;   // 相当于 value / 2，但更快
uint32_t div_by_8 = value >> 3;   // 相当于 value / 8

```

### 2.2 Bitwise Operators

Bitwise operators are core tools in embedded programming. They directly manipulate the binary bits of data and are commonly used for hardware register configuration, flag management, and efficient mathematical operations.

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

**Bit-field masking**:

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

Relational operators are used for comparison and return an integer result (0 for false, non-zero for true):

```c
int a = 5, b = 10;
int equal = (a == b);        // 等于：0
int not_equal = (a != b);    // 不等于：1
int less = (a < b);          // 小于：1
int greater = (a > b);       // 大于：0
int less_equal = (a <= b);   // 小于等于：1
int greater_equal = (a >= b);// 大于等于：0

```

Logical operators have short-circuit behavior, which can be used for conditional optimization in embedded programming:

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

The **sizeof operator** returns the byte size of a type or object. It is evaluated at compile time and is commonly used for calculating array sizes:

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

In embedded systems, for multiple mutually exclusive conditions, using an else-if chain can avoid unnecessary conditional checks and improve execution efficiency.

The **switch statement** is suitable for multi-way branching. Compilers typically optimize it into a jump table, making it more efficient than multiple if-else statements in certain cases:

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

In embedded development, switch statements are commonly used to implement state machines:

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

The **for loop** is typically used when the number of iterations is known:

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

The **while loop** is used when the condition is unknown or depends on calculations within the loop body:

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

The **do-while loop** executes the loop body at least once and is suitable for certain initialization scenarios:

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

The **continue statement** skips the remainder of the current iteration and proceeds to the next one:

```c
for (int i = 0; i < data_count; i++) {
    if (data[i] == INVALID_VALUE) {
        continue;  // 跳过无效数据
    }
    process_valid_data(data[i]);
}

```

Although the **goto statement** is often criticized, in embedded C, it has legitimate use cases in error handling and resource cleanup scenarios:

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

Functions, which I remember also being called subroutines, are blocks of code that complete a specific piece of logic and are meant to be read by humans. From this perspective, the foundation of modular programming in C is the function.

> I've actually met people who believe that function calls waste time and therefore functions shouldn't be written—well, the first part is true, but the second part is wrong. They clearly don't know that modern compilers optimize unnecessary function calls by inlining them directly (i.e., inserting the code snippet at the call site, saving the time consumed by pushing/popping the stack and triggering pipeline flushes). Besides, do you really need to worry about the time taken by a function call?

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

C uses pass-by-value, but we can achieve the effect of pass-by-reference through pointers:

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

In embedded development, when passing large structures, we should use pointers to avoid expensive copies:

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

In modern C++, `inline` no longer means an **inline function**—this is something everyone must be aware of when writing C++. It actually means "allowed to have duplicate definitions." Because it eliminates a unique symbol encoding to some extent, thereby avoiding linker conflicts—modern C compilers will also proactively optimize on their own. So, if you find that your compiler actually respects this keyword, then use it; otherwise, there is no need to write it.

```c
// C99标准的内联函数
static inline uint16_t swap_bytes(uint16_t value) {
    return (value >> 8) | (value << 8);
}

// 宏定义方式（传统方法，但类型不安全）
#define SWAP_BYTES(x) (((x) >> 8) | ((x) << 8))
```

### 4.4 Function Pointers and Callbacks

Function pointers are a basic building block for implementing callbacks. A callback is exactly what it sounds like—calling back. We save the address of a function, and when needed, we **call it back**, which is equivalent to storing our processing flow!

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

Function pointers can also be used to implement simple polymorphism. I remember a pretty good embedded C tutorial that had a nice example of C-based polymorphism, but unfortunately, I've forgotten the title of the book (sweatdrop.

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

Pointers are C's most powerful yet error-prone feature, and they are especially important in embedded programming. Since this is a quick review, we will just briefly run through C pointers.

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

In most contexts, an array name decays into a pointer to its first element. But hey, we must note this—arrays are not pointers!!!

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

This reminds me of a meme—a person pointing at a person pointing at a person.jpg. Yeah, that's exactly what it means. A pointer to a pointer variable that points to a pointer variable that points to a variable. Yeah, it makes your head spin. My advice is: unless absolutely necessary, don't play this game. You are just setting a massive trap for your colleagues.

```c
int value = 42;
int* ptr = &value;
int** ptr_ptr = &ptr;    // 指向指针的指针

// 解引用
int val1 = *ptr;         // 42
int val2 = **ptr_ptr;    // 42

```

Multi-level pointers are useful when dynamically allocating two-dimensional arrays, but dynamic memory allocation should be used cautiously in embedded systems.

### 5.4 Pointers and const

The combination of `const` and pointers has multiple meanings:

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

Strings in C are character arrays terminated by a null character (`'\0'`):

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

In embedded systems, we should prefer safe function versions with length limits:

```c
char buffer[32];
strncpy(buffer, source, sizeof(buffer) - 1);
buffer[sizeof(buffer) - 1] = '\0';   // 确保以空字符结尾

// 更安全的做法
snprintf(buffer, sizeof(buffer), "Value: %d", value);

```

Things to keep in mind when handling strings:

- Ensure the destination buffer is large enough
- Always ensure the string is terminated with `'\0'`
- In resource-constrained systems, consider using fixed-size buffers to avoid dynamic allocation

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

In embedded development, structures are widely used to represent configurations, states, and data packets:

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

### 7.2 Bit-Fields

Bit-fields allow allocating storage in a structure on a bit-by-bit basis, which is extremely useful when dealing with hardware registers:

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

Note: The implementation of bit-fields depends on the compiler and platform. Use them with caution when precise control is required.

### 7.3 Unions

All members of a union share the same block of memory, which is used to save space or for type punning:

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

Enumerations define a set of named integer constants, improving code readability:

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

Enumerations in embedded development are commonly used to define states, command codes, and configuration options:

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

The preprocessor processes source code before compilation. It is an important source of flexibility in C and is especially crucial in embedded development.

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

Things to keep in mind with macros:

- Parameters should be enclosed in parentheses to avoid precedence issues
- Multi-line macros should be wrapped with `do-while(0)`
- Macros do not perform type checking, so use them carefully

Typical applications in embedded development:

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

Conditional compilation allows us to selectively include or exclude code based on conditions. This is a fundamental tool for cross-platform implementation.

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

Compilers provide some useful predefined macros:

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

C provides several storage class specifiers:

**auto**: The default storage class for local variables, rarely used explicitly:

```c
void function(void) {
    auto int x = 10;  // 等价于 int x = 10;
}

```

**static**: Has two main uses.

Static local variables retain their values between function calls:

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

**register**: Suggests to the compiler that the variable should be stored in a register (modern compilers usually ignore this):

```c
void fast_loop(void) {
    register int i;
    for (i = 0; i < 1000000; i++) {
        // 循环变量建议存储在寄存器
    }
}

```

### 9.2 Scope Rules

C has four scopes: file scope, function scope, block scope, and function prototype scope.

In embedded development, using scope properly can avoid naming conflicts and unexpected side effects:

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

Although dynamic memory allocation should generally be avoided in embedded systems (due to memory fragmentation and non-determinism), understanding these functions is still important:

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

Understanding a program's memory layout is crucial for embedded development. We will cover this in a more dedicated section later, so we will just touch on it here.

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

In embedded systems, we often need precise control over where variables are stored:

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
