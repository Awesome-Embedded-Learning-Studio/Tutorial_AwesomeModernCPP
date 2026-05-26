---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Deeply understanding the C++ zero-overhead abstraction principle
difficulty: intermediate
order: 1
platform: stm32f1
prerequisites:
- 'Chapter 1: 构建工具链'
reading_time_minutes: 19
tags:
- cpp-modern
- intermediate
- stm32f1
title: Zero-Overhead Abstraction
translation:
  source: documents/vol8-domains/embedded/01-zero-overhead-abstraction.md
  source_hash: 03af5e7165b9cd5be865db1fc88856b77044d27ba20f47537356944291f35c6d
  translated_at: '2026-05-26T12:10:33.521058+00:00'
  engine: anthropic
  token_count: 2369
---
# Modern C++ for Embedded Systems—Zero-Overhead Abstraction

## Preface

We often get the feeling—and it is most people's first reaction—that complex code abstractions will impact execution time. For example, compared to using classes, I have genuinely seen friends who prefer to just write scattered functions and go all in. They believe that using classes incurs a time overhead.

This is actually a very common misconception. Many people instinctively assume that terms like "object-oriented," "classes," and "templates" must be slower than C. After all, abstraction sounds like wrapping several layers around originally simple code—how could it not be slower?

I'm not sure if Bjarne Stroustrup actually said this (I haven't verified the quote), but the sentiment holds true: **"You don't pay for what you don't use, and what you do use, you couldn't hand-code any better."** Therefore, C++'s advanced abstraction features (such as classes, templates, and inline functions) should not produce extra runtime overhead after compilation. Their performance should be on par with hand-written low-level code. This is the pursuit of C++.

To put it plainly, we want the code written in C++ to be nearly as efficient as hand-written assembly, while being more maintainable. This sounds a bit like "having your cake and eating it too," but it is precisely the original design intent of C++—to give you high-level abstraction capabilities without making you pay a performance price.

#### Why is this important in embedded systems?

In desktop applications or server development, we might not be sensitive to a difference of a few clock cycles. But in embedded systems, the situation is completely different.

Embedded systems typically have strict resource constraints:

- **Limited CPU performance** - Every clock cycle is precious. Many MCUs might only run at a few tens of MHz, unlike your computer which easily hits several GHz.
- **Constrained memory** - ROM/RAM capacity is limited. An entire program might only have a few tens of KB of Flash, and a few KB of RAM.
- **Real-time requirements** - Tasks must be completed within a deterministic time. A delay of a few milliseconds can cause system failure.
- **Power constraints** - Extra instructions mean more power consumption. For battery-powered devices, executing one more instruction drains a little more power.

So in embedded development, we want code that is easy to maintain and understand, without sacrificing performance. Zero-overhead abstraction allows us to use modern C++ features to improve code maintainability without sacrificing performance. This is why we need to thoroughly understand this concept.

## Practical Case Analysis

Enough theory—let's look at some actual code. After all, we all know a very classic saying—`talk is cheap, show me the code`.

#### Example: GPIO Control

It is quite easy to write code like this:

```cpp
// 直接操作寄存器
#define GPIO_PORT_A ((volatile uint32_t*)0x40020000)
#define PIN_5 (1 << 5)

void set_pin() {
    *GPIO_PORT_A |= PIN_5;  // 容易出错,魔法数字
}

```

What is wrong with this approach? First, there are magic numbers everywhere. What is `0x40020000`? Without looking at the manual, you have no idea. `PIN_5` might look meaningful, but its definition `(1 << 5)` is copy-pasted all over the codebase. If it ever needs to change, you have to do a global search and replace.

Even worse, this approach has no type safety. You can pass in a completely unrelated address, and the compiler will not complain. You could even accidentally write `*GPIO_PORT_A = PIN_5`, which directly overwrites the entire register instead of setting a specific bit.

But in C++, we can make this much safer:

```cpp
// 类型安全的抽象
template<uint32_t Address>
class GPIO_Port {
    static volatile uint32_t& reg() {
        return *reinterpret_cast<volatile uint32_t*>(Address);
    }
public:
    static void set_pin(uint8_t pin) {
        reg() |= (1 << pin);
    }

    static void clear_pin(uint8_t pin) {
        reg() &= ~(1 << pin);
    }
};

using GPIOA = GPIO_Port<0x40020000>;

void set_pin() {
    GPIOA::set_pin(5);  // 类型安全,可读性强
}

```

It looks like there is more code, right? But think about it—this "extra" code is all template definitions that get processed at compile time. The final generated machine code is exactly the same as the C version above!

You can try it yourself. In my previous tests, I even found the overhead to be smaller than C—because the compiler has more contextual information to leverage when optimizing template code.

More importantly, you now have type safety. `GPIO_Port<0x40020000>` and `GPIO_Port<0x40020400>` are two completely different types and cannot be mixed up. Furthermore, all operations go through explicit interfaces, so there is no risk of accidentally overwriting a register.

<OnlineCompilerDemo
  title="GPIO Bit Manipulation: C Macros vs C++ Type-Safe Abstraction"
  source-path="code/examples/chapter02/01_zero_overhead/gpio_example.cpp"
  arm-source-path="code/examples/compiler_explorer/gpio_zero_overhead_arm.cpp"
  description="This example contains real MMIO addresses and is suitable for directly observing the optimized assembly. It does not perform register writes on the host machine."
  allow-x86-asm
  allow-arm-asm
/>

#### Example: State Machine Implementation

State machines are extremely common in embedded systems. Button handling, protocol parsing, motor control—state machines are everywhere.

**C Style (using switch-case)**

We have all written the traditional C implementation:

```cpp
enum State { IDLE, RUNNING, STOPPED };
State current_state = IDLE;

void process_event(int event) {
    switch(current_state) {
        case IDLE:
            if(event == START) current_state = RUNNING;
            break;
        case RUNNING:
            if(event == STOP) current_state = STOPPED;
            break;
        case STOPPED:
            if(event == RESET) current_state = IDLE;
            break;
    }
}

```

This approach is simple and direct, but it has a few problems. First, the state and event handling logic are all mixed into one large function, making it hard to maintain once the number of states grows. Second, adding a new state requires modifying code in multiple places. Most importantly, it is very difficult for the compiler to deeply optimize this kind of dynamic switch-case.

**Zero-Overhead C++ Abstraction (using compile-time polymorphism)**

We can use C++'s compile-time polymorphism to implement this:

```cpp
// 编译时多态 - 无虚函数开销
template<typename StateImpl>
class State {
public:
    auto handle_event(int event) {
        return static_cast<StateImpl*>(this)->on_event(event);
    }
};

class IdleState : public State<IdleState> {
public:
    auto on_event(int event) { /* ... */ }
};

class RunningState : public State<RunningState> {
public:
    auto on_event(int event) { /* ... */ }
};

// 使用std::variant实现零开销状态切换
using StateMachine = std::variant<IdleState, RunningState, StoppedState>;

```

This looks complex, but the magic is that this is **compile-time polymorphism**, not runtime polymorphism. Note that we are using CRTP (Curiously Recurring Template Pattern), not virtual functions. The compiler knows the exact type of each state at compile time and can directly generate targeted code without needing a virtual function table lookup.

Combined with `std::variant`, we can also ensure type safety for state transitions at compile time. Moreover, the implementation of `std::variant` is typically zero-overhead as well—it is essentially a union plus a tag, exactly the same as if you had hand-written a union.

#### RAII Resource Management

RAII (Resource Acquisition Is Initialization) is a very powerful concept in C++. In embedded systems, we frequently need to manage various resources: clocks, interrupts, DMA channels, and so on.

**Manual Management (prone to leaks)**

First, let's look at the problems with manual management:

```cpp
void configure_peripheral() {
    enable_clock();
    configure_pins();
    // 如果这里异常,时钟不会被禁用!
    do_something();
    disable_clock();
}

```

This code looks fine, but there is a hidden danger: if something goes wrong in `do_something()` (although we usually don't use exceptions in embedded systems, there might be other forms of error handling), or if you return early somewhere in the middle, `disable_clock()` will not be executed. The clock stays on, wasting power for nothing.

**Zero-Overhead RAII**

Using the RAII approach, we can write it like this:

```cpp
class ClockGuard {
    uint32_t peripheral_id;
public:
    ClockGuard(uint32_t id) : peripheral_id(id) {
        enable_clock(peripheral_id);
    }
    ~ClockGuard() {
        disable_clock(peripheral_id);  // 自动清理
    }
};

void configure_peripheral() {
    ClockGuard clock(PERIPH_GPIOA);
    configure_pins();
    do_something();
    // clock自动析构,即使发生异常
}

```

The beauty of this approach is that no matter how your function exits—normal return, early return, or even an exception—the destructor of `ClockGuard` will be called. This is guaranteed by the C++ language.

The key point is that the compiler will inline the constructor and destructor, generating the exact same code as manual management! You gain the convenience of automatic resource management without paying any performance price. This is the essence of zero-overhead abstraction.

## constexpr - Compile-Time Computation

`constexpr` is a killer feature in modern C++. It allows you to perform computations at compile time rather than at runtime.

```cpp
// 运行时计算(浪费CPU)
uint32_t calculate_baud_divisor(uint32_t cpu_freq, uint32_t baud) {
    return cpu_freq / (16 * baud);
}

// 编译期计算(零运行时开销)
constexpr uint32_t calculate_baud_divisor(uint32_t cpu_freq, uint32_t baud) {
    return cpu_freq / (16 * baud);
}

// 这个值在编译时计算,直接嵌入代码
constexpr uint32_t DIVISOR = calculate_baud_divisor(72000000, 115200);

```

You might think, what is the difference? Isn't it just adding the `constexpr` keyword?

The difference is huge! In the first version, the division operation must be executed every time the function is called. Division is a relatively slow operation on many MCUs and might take dozens of clock cycles.

In the second version, the compiler calculates the result at compile time. In the final machine code, `DIVISOR` is simply a constant written directly into the code, requiring no computation at all. This is a massive advantage for embedded systems—it saves CPU time and makes code execution time predictable (which is crucial for real-time systems).

Even better, you can write very complex `constexpr` functions, including loops, conditional logic, and so on. As long as the parameters are known at compile time, the compiler can calculate the result. This allows you to move a lot of configuration calculations to compile time, rather than computing them on every boot.

<OnlineCompilerDemo
  title="constexpr Baud Rate Division: Runtime Results and Optimized Output"
  source-path="code/examples/chapter02/01_zero_overhead/constexpr_example.cpp"
  arm-source-path="code/examples/compiler_explorer/constexpr_baud_arm.cpp"
  description="This demo can run on the host machine, and you can also compare the optimized output between x86-64 and Cortex-M."
  allow-run
  allow-x86-asm
  allow-arm-asm
/>

## Practical Tips

After covering so much theory, let's look at some practical tips. These are techniques I have used in real projects that genuinely improve code quality without impacting performance.

### 1. Use Inline Functions Instead of Macros

Macros are an artifact of the C era. In C++, in most cases, you should use inline functions to replace macros.

```cpp
// 不推荐:宏没有类型检查
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// 推荐:内联函数零开销且类型安全
template<typename T>
inline constexpr T max(T a, T b) {
    return (a > b) ? a : b;
}

```

Macros have too many problems. First, they have no type checking—you can pass anything into them. Second, they have weird side effects. For example, with `MAX(i++, j++)`, this macro expands to `((i++) > (j++) ? (i++) : (j++))`, causing `i` or `j` to be incremented twice!

Inline functions do not have these problems. The compiler performs type checking, and parameters are only evaluated once. At the same time, because it is `inline`, the compiler directly inserts the function body at the call site, so there is no function call overhead.

Add `constexpr`, and if the parameters are compile-time constants, the compiler can even calculate the result at compile time. This is something macros cannot do.

### 2. Template Metaprogramming

Template metaprogramming sounds very sophisticated, but the concept is simple: let the compiler do some work for you at compile time.

```cpp
// 编译期循环展开
template<size_t N>
struct UnrollLoop {
    template<typename Func>
    static void execute(Func f) {
        f(N-1);
        UnrollLoop<N-1>::execute(f);
    }
};

template<>
struct UnrollLoop<0> {
    template<typename Func>
    static void execute(Func) {}
};

// 使用
UnrollLoop<4>::execute([](size_t i) {
    process_data(i);  // 完全展开,无循环开销
});

```

What does this code do? It unrolls the loop at compile time. The final generated code is equivalent to:

```cpp
process_data(0);
process_data(1);
process_data(2);
process_data(3);

```

There is no loop structure, no loop counter, and no conditional branching. For loops with a small iteration count, this kind of unrolling can significantly improve performance because it avoids branch misprediction and loop overhead.

Of course, loop unrolling is not a silver bullet. If the loop count is large, unrolling will lead to code bloat. But for the small loops commonly seen in embedded systems (such as processing data from a few ADC channels), this is an excellent optimization technique.

### 3. Strong Types Instead of Primitive Types

Type safety is not just about preventing errors; it also makes code clearer.

```cpp
// 易错:单位混淆
void delay(uint32_t time);  // 是毫秒还是微秒?

// 零开销强类型
struct Milliseconds { uint32_t value; };
struct Microseconds { uint32_t value; };

void delay(Milliseconds ms);
void delay_us(Microseconds us);

// 编译期检查,运行时无开销
delay(Milliseconds{100});  // 清晰明确

```

Look at the first version: `delay(100)`—what unit is this 100? You have to look at the documentation or comments. Moreover, it is very easy to get mixed up:

```cpp
delay(1000);  // 想延迟1秒,但如果delay是微秒单位就惨了

```

With strong types, this is not a problem. `delay(Milliseconds{1000})` clearly tells you this is 1000 milliseconds. And if you accidentally write `delay(Microseconds{1000})`, the compiler will directly report an error because the types do not match.

The key point is that these strong types are completely zero-overhead at runtime. `Milliseconds` is essentially just a `uint32_t`, and the compiler will completely optimize away this wrapper. You gain type safety without any performance loss.

## Verifying Zero Overhead—Seeing Is Believing

After talking so much about "zero overhead," you might be thinking: is it really true? How can you prove it?

The most direct method is to look at the assembly code. Don't be afraid of assembly; it is actually not that complicated. You just need to compare whether the assembly generated by the C version and the C++ version are the same.

### Using Compiler Explorer

I highly recommend using Compiler Explorer (<https://godbolt.org/>). This is an online tool that lets you see what assembly your code compiles into in real time.

You can write two versions of the code:

- Write the C-style code on the left
- Write the C++ abstracted code on the right

Then compare the assembly generated by both sides. If the assembly is exactly the same (or has only minor differences), that proves the abstraction is zero-overhead.

### Local Verification

If you want to verify locally, you can use this command:

```bash

# 编译时查看汇编
arm-none-eabi-g++ -O2 -S -fverbose-asm code.cpp

```

`-O2` enables optimization (this is very important, as zero-overhead abstraction relies on compiler optimization), `-S` generates an assembly file, and `-fverbose-asm` adds comments in the assembly to make it easier to read.

### Key Compiler Flags

Speaking of optimization, here are a few important compiler flags:

```bash
-O2 或 -O3    # 优化级别,至少要O2
-flto         # 链接时优化,可以跨编译单元优化
-fno-rtti     # 禁用RTTI(运行时类型识别),嵌入式常用
-fno-exceptions  # 禁用异常,可选(很多嵌入式项目会禁用)

```

**Important note**: With `-O0` or without optimization, many zero-overhead abstractions will have overhead. This is because the compiler does not perform inlining, constant folding, and other optimizations. So when testing zero-overhead abstractions, you must enable optimization!

In real embedded projects, your Release build configuration should always have at least `-O2` optimization enabled. For the Debug configuration, you can use `-Og` (to optimize the debugging experience) or `-O0`.

## My Casual Ramblings

#### "Abstraction always has overhead"

Wrong. **Correct abstractions are zero-overhead after compilation**. The keyword here is "correct"—you need to use compile-time abstractions (templates, inline functions, constexpr, etc.), not runtime abstractions (virtual functions, dynamic allocation, etc.).

Many people are biased against abstraction because they have seen terrible abstractions. For example, using virtual functions everywhere, using dynamic memory everywhere. This kind of abstraction确实确实 does have overhead. But this is not a problem with abstraction itself; rather, it is a case of using the wrong tools.

Modern C++ provides a large number of compile-time abstraction tools that let you write code that is both abstract and efficient.

#### "Embedded must use C"

This notion is both outdated and not outdated. However, modern C++ is perfectly suited for embedded development and has many advantages:

- Better type safety
- Better resource management (RAII)
- More powerful compile-time computation capabilities
- Easier to maintain code

I have seen far too many embedded projects written in C where the code is full of global variables, magic numbers, and duplicated code snippets. This kind of code is hard to maintain and very prone to bugs.

After rewriting them with modern C++, the code volume might actually be smaller, and much clearer. Performance? There is absolutely no need to worry, provided you use the right features. **But it is precisely this "using the right features"** that makes me pessimistic about using C++ in embedded systems. Using C++ features correctly is not an easy task. The learning curve is indeed much steeper.

#### "Templates increase code size"

Yes! But this needs to be looked at on a case-by-case basis. Templates generate a separate copy of code for each type used, so if you instantiate the same template for 100 different types, it will indeed increase code size.

But in actual embedded projects, you usually would not do this. Moreover, in many cases, using templates reasonably can actually **reduce** code size, because:

- It avoids code duplication
- The compiler can optimize better
- You can use compile-time computation to replace runtime computation

My advice is: don't blindly worry about code size. First, write clear code, then compile it and check the actual size. In most cases, you will find that the template version is not much larger than the hand-written version, and might even be smaller.
