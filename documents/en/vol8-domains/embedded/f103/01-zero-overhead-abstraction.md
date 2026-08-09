---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Deep Dive into C++ Zero-Overhead Abstraction Principle
difficulty: intermediate
order: 1
platform: stm32f1
prerequisites:
- 'Chapter 1: 构建工具链'
reading_time_minutes: 15
tags:
- cpp-modern
- intermediate
- stm32f1
title: zero-overhead abstraction
translation:
  source: documents/vol8-domains/embedded/01-zero-overhead-abstraction.md
  source_hash: ec936eab770dff02ad30ac3b0ec4357856b3e638866e55649d1def158c44b320
  translated_at: '2026-06-16T04:10:36.721943+00:00'
  engine: anthropic
  token_count: 2369
---
# Modern Embedded C++ Tutorial — Zero-Overhead Abstractions

## Preface

We often share a common intuition, which is also the first reaction for most people—complex code abstractions negatively impact execution time. For instance, compared to using classes, I have genuinely seen friends who prefer to just "go all-in" with scattered functions because they believe using classes incurs a time overhead.

This is actually a very common misconception. Many people instinctively assume that terms like "Object-Oriented," "Class," and "Template" imply slowness compared to C. After all, abstraction sounds like wrapping layers upon layers on top of simple code, so how could it not be slow?

I'm not sure if Bjarne Stroustrup said this—I haven't verified the source—but the saying certainly holds merit: **"You don't pay for what you don't use, and you can't write better hand-optimized code than what you use."** Therefore, C++'s advanced abstraction features (such as classes, templates, and inline functions) should not generate additional runtime overhead after compilation; their performance should be on par with hand-written low-level code. This is the pursuit of C++.

To put it simply, we want the code we write in C++ to have efficiency nearly identical to hand-written assembly, while being more maintainable. This sounds a bit like "having your cake and eating it too," but it is precisely the original design intent of C++—to give you high-level abstraction capabilities without forcing you to pay a performance penalty.

#### Why is this important in embedded systems?

In desktop application or server development, we might not be sensitive to a difference of a few clock cycles. However, in embedded systems, the situation is completely different.

Embedded systems usually have strict resource constraints:

- **Limited CPU performance** - Every clock cycle is precious. Many MCUs might only run at a few tens of MHz, unlike your PC which easily hits several GHz.
- **Constrained memory** - ROM/RAM capacity is limited. The entire program might only have a few dozen KB of Flash and a few KB of RAM.
- **Real-time requirements** - Tasks must be completed within a deterministic time. A delay of a few milliseconds can cause system failure.
- **Power constraints** - Extra instructions mean more power consumption. For battery-powered devices, executing one more instruction consumes a bit more power.

Therefore, in embedded development, we want code that is maintainable and understandable, yet we cannot sacrifice performance. Zero-overhead abstractions allow us to use modern C++ features to improve code maintainability without sacrificing performance. This is why we need to understand this concept thoroughly.

## Practical Case Studies

Enough theory; let's look at actual code. After all, we all know a very classic saying—`talk is cheap, show me the code`.

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

What problems does this style have? First, there are magic numbers everywhere. What is `0x40020000`? Without looking at the manual, you have no idea. Although `PIN_5` looks meaningful, its definition `(1 << 5)` is actually copied and pasted all over the code. If you need to change it, you have to do a global search and replace.

Even worse, this style has no type safety. You can pass a completely unrelated address in, and the compiler won't complain. You could even accidentally write `*GPIO_PORT_A = PIN_5`, overwriting the entire register instead of setting a specific bit.

But in C++, we can do this more safely:

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

The code looks longer, right? But think carefully: these "extra" lines are actually template definitions that are processed at compile time. The final generated machine code is exactly the same as the C version above!

You can try it out. In my previous tests, I even found the overhead to be smaller than C—because the compiler has more contextual information to utilize when optimizing template code.

More importantly, you now have type safety. `GPIO_Port<0x40020000>` and `GPIO_Port<0x40020400>` are two completely different types and won't be confused. Also, all operations are performed through explicit interfaces, so there's no risk of accidentally overwriting registers.

<OnlineCompilerDemo
  title="GPIO Bit Manipulation: C Macros vs C++ Type-Safe Abstractions"
  source-path="code/examples/chapter02/01_zero_overhead/gpio_example.cpp"
  arm-source-path="code/examples/compiler_explorer/gpio_zero_overhead_arm.cpp"
  description="This example contains real MMIO addresses and is suitable for directly observing optimized assembly; it does not perform register writes on the host."
  allow-x86-asm
  allow-arm-asm
/>

#### Example: State Machine Implementation

State machines are ubiquitous in embedded systems. Button handling, protocol parsing, motor control... state machines are everywhere.

**C Style (using switch-case)**

We've all written the traditional C implementation:

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

This style is simple and direct, but it has several issues. First, state and event handling logic are mixed in one big function, making it hard to maintain as the number of states grows. Second, adding new states requires modifying code in multiple places. Most importantly, it is difficult for the compiler to perform deep optimization on this kind of dynamic switch-case.

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

This looks complex, but the magic lies in this being **compile-time polymorphism**, not runtime polymorphism. Note that we use CRTP (Curiously Recurring Template Pattern), not virtual functions. The compiler knows the specific type of each state at compile time and can directly generate targeted code without needing a virtual function table lookup.

Combined with `std::variant`, we can also ensure type safety during state transitions at compile time. Furthermore, the implementation of `std::variant` is usually zero-overhead—it is essentially a union plus a tag, just like a hand-written union.

#### RAII Resource Management

RAII (Resource Acquisition Is Initialization) is a very powerful concept in C++. In embedded systems, we often need to manage various resources: clocks, interrupts, DMA channels...

**Manual Management (Prone to Leaks)**

First, let's look at the problem with manual management:

```cpp
void configure_peripheral() {
    enable_clock();
    configure_pins();
    // 如果这里异常,时钟不会被禁用!
    do_something();
    disable_clock();
}

```

This code looks fine, but there is a hidden pitfall: if something goes wrong in `do_something()` (although we usually don't use exceptions in embedded systems, there might be other forms of error handling), or if you return early somewhere in the middle, `disable_clock()` will not be executed. The clock stays on, wasting power.

**Zero-Overhead RAII**

Using the RAII philosophy, we can write it like this:

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

The beauty of this style is that no matter how your function exits—normal return, early return, or even exception—the destructor of `ClockGuard` will be called. This is guaranteed by the C++ language.

The key is that the compiler will inline the constructor and destructor, generating code identical to manual management! You gain the convenience of automatic resource management without paying any performance cost. This is the essence of zero-overhead abstraction.

## constexpr - Compile-Time Calculation

`constexpr` is a killer feature in modern C++. It allows you to perform calculations at compile time instead of at runtime.

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

You might think, what's the difference? Isn't it just adding a `constexpr` keyword?

The difference is huge! In the first version, the division operation is executed every time the function is called. Division is a relatively slow operation on many MCUs, potentially taking dozens of clock cycles.

In the second version, the compiler calculates the result at compile time. In the final machine code, `DIVISOR` is just a constant, written directly into the code without any calculation. This is a huge advantage for embedded systems—it saves CPU time and makes execution time predictable (important for real-time systems).

Even better, you can write very complex `constexpr` functions, including loops, conditional branching, etc. As long as the parameters are known at compile time, the compiler can calculate the result. This allows you to offload a lot of configuration calculation to compile time, rather than calculating it every time the system starts.

<OnlineCompilerDemo
  title="constexpr Baud Rate Divider: Runtime Results and Optimized Output"
  source-path="code/examples/chapter02/01_zero_overhead/constexpr_example.cpp"
  arm-source-path="code/examples/compiler_explorer/constexpr_baud_arm.cpp"
  description="This demo can run on the host, or compare optimized output between x86-64 and Cortex-M."
  allow-run
  allow-x86-asm
  allow-arm-asm
/>

## Practical Tips

We've covered a lot of theory; now let's look at some practical tips. These are techniques I have used in actual projects that genuinely improve code quality without affecting performance.

### 1. Use Inline Functions Instead of Macros

Macros are a relic of the C era. In C++, in most cases, you should use inline functions instead of macros.

```cpp
// 不推荐:宏没有类型检查
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// 推荐:内联函数零开销且类型安全
template<typename T>
inline constexpr T max(T a, T b) {
    return (a > b) ? a : b;
}

```

Macros have too many problems. First, they have no type checking; you can pass anything in. Second, they have strange side effects. For example, `MAX(i++, j++)` expands to `((i++) > (j++) ? (i++) : (j++))`, so `i` or `j` would be incremented twice!

Inline functions don't have these problems. The compiler performs type checking, and parameters are only evaluated once. Also, because they are `inline`, the compiler inserts the function body directly at the call point, so there is no function call overhead.

With `constexpr`, if the parameters are compile-time constants, the compiler can even calculate the result at compile time. This is something macros cannot do.

### 2. Template Metaprogramming

Template metaprogramming sounds high-level, but the concept is simple: let the compiler do some work for you at compile time.

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

There are no loop structures, no loop counters, and no conditional branches. For loops with a small iteration count, this unrolling can significantly improve performance because it avoids branch prediction failures and loop overhead.

Of course, loop unrolling isn't a silver bullet. If the loop count is large, unrolling leads to code bloat. But for the small loops common in embedded systems (like processing data for a few ADC channels), this is a great optimization.

### 3. Strong Types Instead of Primitive Types

Type safety isn't just about preventing errors; it also makes code clearer.

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

Look at the first version: `delay(100)`—what unit is this 100? You have to look at the documentation or comments. It's also easy to get confused:

```cpp
delay(1000);  // 想延迟1秒,但如果delay是微秒单位就惨了

```

With strong types, you won't have this problem. `delay(Milliseconds{1000})` clearly tells you this is 1000 milliseconds. If you accidentally write `delay(Microseconds{1000})`, the compiler will directly report an error because the types don't match.

The key is that these strong types are completely zero-overhead at runtime. `Milliseconds` is essentially just a `uint32_t`, and the compiler will optimize away this wrapper completely. You gain type safety without any performance loss.

## Verifying Zero Overhead — Seeing is Believing

After all this talk about "zero overhead," you might be thinking: Really? How do you prove it?

The most direct way is to look at the assembly code. Don't be afraid of assembly; it's not that complex. You just need to compare whether the assembly generated by the C version and the C++ version is the same.

### Using Compiler Explorer

I strongly recommend using Compiler Explorer (<https://godbolt.org>). This is an online tool that lets you see what assembly your code compiles into in real-time.>

You can write two versions of the code:

- C-style code on the left
- C++ abstract code on the right

Then compare the assembly generated by both sides. If the assembly is identical (or has only minor differences), it proves that the abstraction is zero-overhead.

### Local Verification

If you want to verify locally, you can use this command:

```bash

# 编译时查看汇编
arm-none-eabi-g++ -O2 -S -fverbose-asm code.cpp

```

`-O2` means optimization is enabled (this is important; zero-overhead abstractions rely on compiler optimization), `-S` means generate an assembly file, and `-fverbose-asm` adds comments to the assembly, making it easier to understand.

### Key Compiler Options

Speaking of optimization, here are a few important compiler options:

```bash
-O2 或 -O3    # 优化级别,至少要O2
-flto         # 链接时优化,可以跨编译单元优化
-fno-rtti     # 禁用RTTI(运行时类型识别),嵌入式常用
-fno-exceptions  # 禁用异常,可选(很多嵌入式项目会禁用)

```

**Important Note**: With `-O0` or without optimization, many zero-overhead abstractions will have overhead. This is because the compiler doesn't perform inlining, constant folding, and other optimizations. So, when testing zero-overhead abstractions, make sure to turn on optimization!

In actual embedded projects, your Release build configuration should always have at least `-O2` optimization enabled. Debug configuration can use `-Og` (for debug experience) or `-O0`.

## Author's Ramblings

#### "Abstraction always has overhead"

Wrong. **Correct abstractions are zero-overhead after compilation**. The keyword here is "correct"—you should use compile-time abstractions (templates, inline functions, constexpr, etc.), not runtime abstractions (virtual functions, dynamic allocation, etc.).

Many people are biased against abstraction because they have seen bad abstractions. For example, using virtual functions everywhere, or dynamic memory everywhere. This kind of abstraction does have overhead. But this isn't a problem with abstraction itself; it's using the wrong tool.

Modern C++ provides a large number of compile-time abstraction tools, allowing you to write code that is both abstract and efficient.

#### "Embedded must use C"

This concept is outdated, but also not outdated. However, Modern C++ is perfectly suitable for embedded development and has many advantages:

- Better type safety
- Better resource management (RAII)
- More powerful compile-time calculation capabilities
- Easier to maintain code

I have seen too many embedded projects written in C where the code is full of global variables, magic numbers, and repetitive code fragments. This kind of code is hard to maintain and prone to bugs.

After rewriting with Modern C++, the code volume might actually be smaller and clearer. Performance? You don't need to worry at all, provided you use the right features. **But it is precisely this "using the right features"** that makes me pessimistic about using C++ in embedded systems. Using C++ features correctly is not an easy task. The learning curve is indeed much steeper.

#### "Templates increase code size"

Yes! But this depends on the situation. Templates generate a copy of code for each type used, so if you instantiate the same template for 100 types, it will indeed increase code size.

However, in actual embedded projects, you usually won't do this. Moreover, in many cases, using templates reasonably can actually **reduce** code size because:

- It avoids code duplication
- The compiler can optimize better
- You can replace runtime calculations with compile-time calculations

My advice is: don't blindly worry about code size. First, write clear code, then compile and check the actual size. In most cases, you will find that the template version is not much larger than the hand-written version, and might even be smaller.
