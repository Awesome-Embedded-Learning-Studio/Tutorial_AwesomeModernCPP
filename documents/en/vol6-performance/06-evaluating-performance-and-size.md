---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Learn how to evaluate program performance and size overhead, and compare
  the behavior of C and C++ in embedded environments through actual measurements.
difficulty: beginner
order: 6
platform: host
prerequisites: []
reading_time_minutes: 44
related: []
tags:
- cpp-modern
- host
- intermediate
title: Performance and Size Evaluation
translation:
  source: documents/vol6-performance/06-evaluating-performance-and-size.md
  source_hash: 8f239d8b3cc3df3a3dc27eb08425c56cf467d163cb010c46344ce41c8b53a80d
  translated_at: '2026-06-15T09:30:43.435662+00:00'
  engine: anthropic
  token_count: 6915
---
# Modern Embedded C++ Tutorial — Does C++ Necessarily Cause Code Bloat?

Regarding performance evaluation and program size, I believe most programmers have a better intuition for the former, while the latter might feel slightly unfamiliar—especially for those developing on host machines. I believe that in an era where storage feels increasingly cheap, few people care about the installer size of desktop applications anymore. However, in the embedded industry, where Flash is as precious as gold, it is still necessary to consider program size.

This brings us to a question. You know this is the "Modern Embedded C++ Tutorial" (though sometimes I write it as the Embedded Modern C++ Tutorial), but this is an age-old yet forever controversial topic: **Does C++ inevitably cause code bloat?**

## Before We Start: Sharpening the Axe

Before we dive into the code battle, make sure your toolbox contains these tools:

#### arm-none-eabi-gcc / arm-none-eabi-g++

This is the cross-compiler for the x86_64 host targeting the ARM platform. Let's give it a try:

```bash
arm-none-eabi-gcc --version
```

If you see a version number, congratulations! If you see "command not found," you might need to download the toolchain from the official ARM website first. I'm on Arch Linux, so I just use `pacman` or `yay` to install it.

> Note: The package name is `gcc-arm-none-eabi`. Otherwise, standard dependencies will be missing. Try installing `arm-none-eabi-gcc` first. If the demo doesn't build, it's because the standard EABI is missing.

```bash
sudo pacman -S gcc-arm-none-eabi
```

> `-fno-exceptions` and `-fno-rtti` are the "diet pills" for using C++ in embedded systems. Without these two, your firmware might bloat like a steamed bun with baking powder due to the exception handling mechanism code.

------

## Starting with Blinking: GPIO Driver (It's just a light, how hard can it be?)

Our first task is to ground the previous content into reality. Let's see how our code looks and actually performs across different languages and programming paradigms.

### Task Brief

We want to implement a GPIO driver to control an LED. This is the "Hello World" of the embedded world, as classic as printing "Hello World" when learning programming. The features include:

- Turn light on/off (well...)
- Toggle state
- PWM dimming (just to show off)

#### C Version — Plain and Simple

```c
typedef struct {
    volatile uint32_t* mod;   // Mode register
    volatile uint32_t* set;   // Set register
    volatile uint32_t* clr;   // Clear register
    uint32_t mask;            // Pin mask
} GPIO_C;

void gpio_init(GPIO_C* gpio, volatile uint32_t* mod, volatile uint32_t* set, volatile uint32_t* clr, uint32_t mask) {
    gpio->mod = mod;
    gpio->set = set;
    gpio->clr = clr;
    gpio->mask = mask;
    *mod |= (1 << mask);  // Configure as output
}

void gpio_write(GPIO_C* gpio, bool state) {
    if (state)
        *gpio->set = gpio->mask;
    else
        *gpio->clr = gpio->mask;
}

void gpio_toggle(GPIO_C* gpio) {
    *gpio->set = gpio->mask;  // Simplified for demo
}
```

This is my C programming style. Some friends might not like structs. I still recommend using structs, but don't pass them by value (triggering a copy); instead, pass a pointer to the object.

#### C++ Version — OOP

```cpp
class GPIO_CPP {
public:
    GPIO_CPP(volatile uint32_t* mod, volatile uint32_t* set, volatile uint32_t* clr, uint32_t mask)
        : mod_(mod), set_(set), clr_(clr), mask_(mask) {
        *mod_ |= (1 << mask_);
    }

    void write(bool state) {
        if (state)
            *set_ = mask_;
        else
            *clr_ = mask_;
    }

    void toggle() {
        *set_ = mask_;
    }

private:
    volatile uint32_t* mod_;
    volatile uint32_t* set_;
    volatile uint32_t* clr_;
    uint32_t mask_;
};
```

A classic use of C++ is adopting the Object-Oriented Programming (OOP) paradigm.

Of course, some might argue—who told you C++ is an OOP language? It's also a generic programming language. True, I have no objection. My own GPIO library is written using templates, but here, let's stick with OOP.

### Battle Analysis: Is there really a big difference?

Let's not judge yet; let's look at the differences!

Save the C code above as `demo.c` and use the full compilation command as follows:

```bash
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os -c demo.c -o demo_c.o
```

Huh? You say you just click a single button in the IDE? Alright, let's talk about what this is actually doing.

------

#### `-mcpu=cortex-m4`

Specifies the **target CPU core model**:

- Generates **instructions specific to Cortex-M4**
- Enables M4-specific features (like DSP instructions)
- Ensures the instruction set matches the actual MCU perfectly.

Of course, if you want to try testing for M1, that works too. Just swap in `cortex-m1` and give it a try.

------

#### `-mthumb`

Forces the use of the **Thumb instruction set**:

- The Cortex-M series **only supports Thumb**
- Instructions are more compact, offering higher code density
- It is the "default working mode" for the M series.

For Cortex-M, this is a **mandatory option, not just an optimization**.

------

#### `-Os`

**Optimization level targeting minimum code size**:

- Prioritizes reducing Flash usage
- Based on `-O2`, deliberately avoids code bloat
- Is the **most common and safest** optimization level in embedded development.

------

#### `-c`: **Compile only, do not link**

- Input: `demo.c`
- Output: `demo.o`
- Does not generate an executable file.

- Only `.o` files can be used for `size` analysis
- Allows for precise evaluation of the code size of "a specific source file itself"

------

#### `-o demo_c.o`

Specifies the output filename:

```bash
size demo_c.o
```

Avoids using the default `a.out`. This is especially clear when doing **multi-language / multi-version comparison experiments**.

------

### Let's See the Results

| Implementation | text (Code) | data | bss  | Total   |
| -------------- | ----------- | ---- | ---- | ------- |
| C Version      | 96 bytes    | 0    | 0    | 96      |
| C++ Version    | 24 bytes    | 0    | 0    | 24      |
| Difference     | **-72 bytes** | 0    | 0    | **-72** |

**Surprised? Unexpected?**

The C++ version is actually **72 bytes smaller**, reducing code size by 75%! This reduction buys you:

- ✅ Better encapsulation (private members won't be accidentally modified)
- ✅ Automatic initialization (won't forget to call `init`)
- ✅ Type safety (won't pass wrong pointers)
- ✅ More intuitive syntax (`led.write(true)` is much nicer than `gpio_write(&led, true)`)

**Key Finding**: C++'s inline optimization makes the entire `example_cpp` function only 24 bytes, smaller than the sum of multiple functions in the C version! The compiler optimized all operations into direct register manipulations.

### The Truth at the Assembly Level

Don't believe it? Let's look at the assembly code generated by the compiler (this is the compiler's "X-ray vision"):

**C version `example_c` (96 bytes, containing multiple function calls):**

```asm
example_c:
    push    {r3, lr}
    mov     r3, r0
    bl      gpio_init
    movs    r0, #1
    mov     r1, r3
    bl      gpio_write
    mov     r0, r3
    bl      gpio_toggle
    pop     {r3, pc}
```

**C++ version `example_cpp` (only 24 bytes, fully inlined):**

```asm
example_cpp:
    movs    r2, #5
    str     r2, [r0, #12]
    movs    r2, #16
    str     r2, [r0, #8]
    movs    r2, #20
    str     r2, [r0, #4]
    bx      lr
```

**See? The C++ version is more concise and efficient!**

The compiler inlined all C++ class methods, eliminating function call overhead and generating optimal register operations directly. The C version, due to function separation, required extra stack operations and function jumps.

**Conclusion**: C++ encapsulation is a "zero-overhead abstraction"—not only zero overhead, but in many cases, even more efficient! This isn't marketing hype; it's real!

------

## Round Two: Ring Buffer (UART's Best Friend)

### Task Brief

The Ring Buffer is the "Swiss Army Knife" of embedded systems. When UART data floods in like a tidal wave, you need a place to temporarily store it. This is where the ring buffer shines—a data container where the end connects to the beginning, never wasting space.

Imagine a sushi conveyor belt; plates go around in a circle. You put plates on (write), and others take plates off (read). As long as the belt isn't full, it keeps spinning.

#### C Version — Plain and Simple

```c
typedef struct {
    uint8_t* buffer;
    uint32_t size;
    uint32_t head;
    uint32_t tail;
} RingBuffer_C;

void rb_init(RingBuffer_C* rb, uint8_t* buffer, uint32_t size) {
    rb->buffer = buffer;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
}

bool rb_put(RingBuffer_C* rb, uint8_t data) {
    uint32_t next = (rb->head + 1) % rb->size;
    if (next == rb->tail) return false;
    rb->buffer[rb->head] = data;
    rb->head = next;
    return true;
}

bool rb_get(RingBuffer_C* rb, uint8_t* data) {
    if (rb->tail == rb->head) return false;
    *data = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % rb->size;
    return true;
}
```

#### C++ Version — Generic

Alright, let's write this generically—generics have a known issue: code bloat.

```cpp
template<uint32_t Size>
class RingBuffer_CPP {
public:
    void init() {
        head_ = 0;
        tail_ = 0;
    }

    bool put(uint8_t data) {
        uint32_t next = (head_ + 1) % Size;
        if (next == tail_) return false;
        buffer_[head_] = data;
        head_ = next;
        return true;
    }

    bool get(uint8_t& data) {
        if (tail_ == head_) return false;
        data = buffer_[tail_];
        tail_ = (tail_ + 1) % Size;
        return true;
    }

private:
    uint8_t buffer_[Size];
    uint32_t head_ = 0;
    uint32_t tail_ = 0;
};
```

------

### Part 1: Ring Buffer Implementation Comparison

Let's see the results:

| Implementation | text (Code) | data | bss  | Total   |
| -------------- | ----------- | ---- | ---- | ------- |
| C Version      | 218 bytes   | 0    | 0    | 218     |
| C++ Version    | 150 bytes   | 0    | 0    | 150     |
| Difference     | **-68 bytes** | 0    | 0    | **-68** |

**Surprised? Unexpected?**

The C++ version is actually **68 bytes smaller**, reducing code size by 31%! And this is while implementing full ring buffer functionality. This reduction buys you:

- ✅ Better encapsulation (internal indices won't be modified externally)
- ✅ Automatic constructor initialization (won't forget to call `init`)
- ✅ Type safety (won't pass wrong pointers)
- ✅ More intuitive method calls (`rb.put(data)` is much nicer than `rb_put(&rb, data)`)

**Key Finding**: C++ eliminates function call overhead through inline optimization, and the compiler can better optimize class methods. The C version needs multiple independent functions (`rb_init`, `rb_put`, `rb_get`, `rb_available`, `rb_free_space`, `rb_clear`), while the C++ version fuses these operations more compactly through smart inlining.

### The Truth at the Assembly Level

Don't believe it? Let's look at the assembly code generated by the compiler:

**C version `example_c_rb` (relies on multiple functions):**

```asm
example_c_rb:
    push    {r4, r5, lr}
    mov     r5, r0
    bl      rb_init
    movs    r0, #42
    mov     r1, r5
    bl      rb_put
    mov     r1, r5
    bl      rb_get
    pop     {r4, r5, pc}
```

**C++ version `example_cpp_rb` (fully inlined):**

```asm
example_cpp_rb:
    str     r0, [sp, #4]
    movs    r0, #42
    ldr     r3, [sp, #4]
    strb    r0, [r3]
    ldrb    r0, [r3]
    bx      lr
```

**See? The C++ version eliminated all function calls!**

The compiler inlined all methods together, reducing stack operations, function jumps, and register saves. Because the C version separates functions, every `rb_put` and `rb_get` requires extra `bl` instructions and stack frame setup.

------

## Round Three: State Machine (The Art of Button Debouncing)

### Task Brief

Button debouncing is a "required course" for embedded engineers. Mechanical buttons chatter when pressed and released (like a spring vibrating back and forth). If not handled, one press might be registered as a dozen.

We want to implement a state machine to:

- Detect button press
- Detect button release
- Detect long press (holding for more than 1 second)
- Debounce (ignore chatter within 50ms)

### C Version: Classic State Machine

```c
typedef enum { IDLE, PRESSED, HELD } State;

typedef struct {
    State state;
    uint32_t last_time;
} Button_C;

void button_update(Button_C* btn, bool pressed, uint32_t now) {
    switch (btn->state) {
        case IDLE:
            if (pressed) {
                btn->state = PRESSED;
                btn->last_time = now;
            }
            break;
        case PRESSED:
            if (!pressed) btn->state = IDLE;
            else if (now - btn->last_time > 1000) btn->state = HELD;
            break;
        case HELD:
            if (!pressed) btn->state = IDLE;
            break;
    }
}
```

### C++ Version: Object-Oriented State Machine

```cpp
class Button_CPP {
public:
    using Callback = void(*)();

    Button_CPP(Callback on_press) : on_press_(on_press) {}

    void update(bool pressed, uint32_t now) {
        switch (state_) {
            case IDLE:
                if (pressed) {
                    state_ = PRESSED;
                    last_time_ = now;
                }
                break;
            case PRESSED:
                if (!pressed) state_ = IDLE;
                else if (now - last_time_ > 1000) {
                    state_ = HELD;
                    if (on_press_) on_press_();
                }
                break;
            case HELD:
                if (!pressed) state_ = IDLE;
                break;
        }
    }

private:
    enum State { IDLE, PRESSED, HELD };
    State state_ = IDLE;
    uint32_t last_time_ = 0;
    Callback on_press_;
};
```

### Battle Analysis: The Cost of std::function

| Implementation        | text (Code) | data | bss  | Total    |
| --------------------- | ----------- | ---- | ---- | -------- |
| C Version             | 172 bytes   | 0    | 0    | 172      |
| C++ Version (std::function) | 306 bytes   | 0    | 0    | 306      |
| Difference            | **+134 bytes** | 0    | 0    | **+134** |

**This time the difference is obvious!** The C++ version increased code size by **78%**. The cost of these 134 bytes comes from:

- The type erasure mechanism of `std::function` (requires a vtable)
- Extra overhead for lambda captures
- Runtime support code for dynamic polymorphism

So, the point here is to tell you—not all abstractions in C++ are zero overhead. Taking **`std::function` as an example: it brings significant code bloat (78% growth)**. Moreover: **lambda captures have hidden costs, because each lambda requires extra storage and management code. Those familiar with lambdas should know this—it generates a closure type with an `operator()` call, storing a structure for every captured object**:

Here is a simple alternative:

```cpp
// Use function pointer instead of std::function
using Callback = void(*)();
```

## Discussion

#### Code Size Comparison Table

Let's review:

**Case 1: GPIO Operation Encapsulation**

In the GPIO operation scenario, the C++ class encapsulation showed surprising advantages. The C version required 96 bytes to implement `gpio_init`, `gpio_write`, `gpio_toggle`, and other functions. The C++ version, through compiler inline optimization, compressed the entire operation sequence to just 24 bytes, reducing code size by 75%. This huge difference comes from the compiler's ability to fully inline C++ member function calls, eliminating function call overhead and stack frame management.

**Case 2: Ring Buffer Implementation**

The ring buffer implementation further validates C++'s advantages. The C version required implementing six independent functions: `rb_init`, `rb_put`, `rb_get`, `rb_available`, `rb_free_space`, `rb_clear`, totaling 218 bytes. The C++ version reduced code size to 150 bytes through class encapsulation and method inlining, saving 31% space. The key is that the compiler can see the complete call chain, allowing for more aggressive optimization.

**Case 3: The Warning of std::function**

Not all C++ features are suitable for embedded development. When using `std::function` to implement callbacks, code swelled from the C version's 172 bytes to 306 bytes, an increase of 78%. This is because `std::function` requires type erasure mechanisms, vtable support, and management code for lambda captures. This case reminds us that in resource-constrained environments, we must carefully choose which C++ features to use.

| Feature                    | Code Growth  | Recommendation                                   |
| -------------------------- | ------------ | ------------------------------------------------ |
| Class Encapsulation (Basic)| -75% to -31% | Highly Recommended (Actually smaller in tests)   |
| Class Encapsulation (Templates) | +4%      | Highly Recommended (Almost zero overhead)        |
| Virtual Functions          | +20-40%      | Use with caution (Consider CRTP alternative)    |
| Exception Handling         | +50-100%     | Disable (`-fno-exceptions`)                      |
| RTTI                       | +30-50%      | Disable (`-fno-rtti`)                            |
| std::function              | +78%         | Use with caution (Replace with function pointers or templates) |
| Templates (Generic Containers) | +4%      | Highly Recommended (Compile-time optimization)   |

### Performance Comparison Table

Based on cycle count analysis at the assembly level:

| Category              | C Implementation | C++ Implementation | Difference |
| --------------------- | ---------------- | ------------------ | ---------- |
| GPIO Single Operation | 8-10 cycles      | 8-10 cycles        | 0%         |
| Buffer Read/Write     | 12-15 cycles     | 12-15 cycles       | 0%         |
| Inlined Full Operation| Requires function call | Fully inlined | C++ is faster |

**Key Finding**: With optimizations enabled, C++'s zero-overhead abstraction is not a marketing slogan, but a verifiable fact. The assembly code generated by the compiler shows that C++ class methods and C functions are identical at the single operation level, while in complex operation scenarios, C++ is even faster due to inline optimization.

------

## Best Practices: How to Use C++ Elegantly in Embedded Systems

### 1. Compiler Options (Slimming Configuration)

The golden compiler configuration for embedded C++ development is as follows:

```bash
-fno-exceptions -fno-rtti -Os -ffunction-sections -fdata-sections
```

This configuration ensures C++ code remains efficient and compact in an embedded environment. Tests show that correctly configured C++ code can achieve a size comparable to or even smaller than C.

### 2. Recommended C++ Features

The following features are proven by testing to perform excellently in embedded systems:

**Classes and Objects (Highly Recommended)**

Class encapsulation is a core advantage of C++, allowing hardware resources to be abstracted as objects. Tests show that simple class encapsulation not only doesn't increase code size but actually reduces it due to compiler optimization. For example, encapsulating GPIO registers as a class provides type safety and a better interface while maintaining zero overhead.

**Constructors and Destructors (Highly Recommended)**

Constructors provide automatic initialization, and destructors implement the RAII pattern. This is C++'s most powerful resource management mechanism. In embedded systems, destructors can automatically close peripherals and release resources, avoiding leaks. Compilers can usually fully inline simple constructors.

**Templates (Highly Recommended)**

Templates provide compile-time code generation with absolutely zero runtime overhead. Ring buffer tests show the template version increases code size by only 4% while providing type safety and size parameterization. Compared to C macros, templates are safer and easier to debug.

**constexpr (Highly Recommended)**

`constexpr` functions are calculated at compile time, with results embedded directly in code. They can be used for calculating configuration parameters, lookup table generation, etc., with completely zero runtime overhead.

**References and Inline Functions (Highly Recommended)**

References avoid unnecessary copies, and inline functions eliminate function call overhead. In embedded systems, using references appropriately can significantly improve performance, especially when passing structs.

**Operator Overloading (Moderately Recommended)**

Operator overloading makes code more intuitive, for example, using `buffer << data` instead of `buffer_put(data)`. As long as it's not abused, operator overloading incurs no extra cost.

### 3. C++ Features to Use with Caution

The following features have some overhead and need to be weighed against the actual situation:

**Virtual Functions (Use with Caution)**

Virtual functions introduce a vtable, adding a 4-byte pointer overhead per object and requiring an indirect jump for every call. If you truly need polymorphism, consider using CRTP (Curiously Recurring Template Pattern) to achieve compile-time polymorphism and avoid runtime overhead.

**std::function (Use with Caution)**

Tests show `std::function` causes 78% code bloat. If you need a callback mechanism, prioritize function pointers (same overhead as C) or template callbacks (zero overhead). Only consider `std::function` when you need lambdas that capture state.

**Dynamic Memory Allocation (Use with Caution)**

`new` and `delete` can lead to memory fragmentation in embedded systems. It is recommended to use placement new with a static memory pool, or use stack-based objects. If you must use dynamic memory, consider a custom allocator.

**STL Containers (Use with Caution)**

Standard library containers like `std::vector` and `std::map` can have large implementations. It is recommended to test code size first or use libraries specifically optimized for embedded systems (like EASTL). For simple scenarios, hand-rolling fixed-size containers might be more appropriate.

### 4. C++ Features to Prohibit

The following features should be completely avoided in embedded systems:

**Exception Handling (Prohibited)**

The exception handling mechanism increases code size by 50-100% and introduces unpredictable execution paths. Embedded systems need deterministic behavior; use error codes or assertions instead of exceptions. Always add the `-fno-exceptions` compiler option.

**RTTI (Prohibited)**

Run-Time Type Information increases code size by 30-50% and is rarely needed in embedded systems. Disable with `-fno-rtti`. If type identification is needed, you can manually implement a simple type tag system.

**iostream Library (Prohibited)**

`std::cout` and `std::cin` introduce huge amounts of code (tens of KB), far beyond what an embedded system can bear. Use traditional `printf`/`scanf` or specialized embedded logging libraries.

**Multiple Inheritance (Prohibited)**

Multiple inheritance increases complexity and code size, and can lead to the diamond problem. In embedded systems, single inheritance or composition patterns are sufficient.

------

## Practical Advice: When to Use C, When to Use C++?

### Scenarios for Choosing C

**Extremely Resource-Constrained Environments**

When the target hardware has less than 8KB Flash and less than 1KB RAM, C is the safer choice. Such systems are usually simple sensor nodes or controllers that don't require complex abstractions.

**Team Skill Stack Limitations**

If team members are unfamiliar with C++ or the project timeline is tight, forcing the use of C++ might do more harm than good. C has a gentler learning curve and is easier to master.

**Pure C Codebase Integration**

When integrating a large amount of existing C code, using C avoids the hassle of mixed programming. Although C++ can call C code, in some cases a pure C project is simpler.

**Insufficient Toolchain Support**

Some older or specialized compilers have incomplete C++ support and may produce inefficient code. In this case, C is the more reliable choice.

### Scenarios for Choosing C++

**Medium to High Resource Systems**

When Flash is greater than 16KB and RAM is greater than 2KB, C++ advantages start to appear. Such systems have enough space to accommodate C++ abstraction mechanisms while benefiting from encapsulation and type safety.

**Complex State Management**

When implementing complex logic like state machines, protocol stacks, or sensor fusion, C++ class encapsulation can significantly reduce complexity. Objects can encapsulate state and behavior, making code easier to maintain.

**Need Code Reuse**

When there are multiple similar modules (like multiple UARTs or timers), C++ templates are safer and easier to debug than C macros. Templates provide compile-time type checking and parameterization.

**Modern Development Practices**

If the team is familiar with modern C++ (C++11 and later) and can correctly use features like smart pointers, move semantics, and lambdas, development efficiency will improve significantly.

### Mixed Usage (Best Practice)

Many successful embedded projects adopt a layered mixed strategy:

**Low-Level Driver Layer: Use C**

Low-level drivers that directly manipulate registers are written in C to ensure stability and portability. This code is usually not complex, and C is sufficient.

**Middle Abstraction Layer: Use C++**

Wrap low-level drivers into C++ classes to provide an object-oriented interface. For example, wrapping a UART driver as a `SerialPort` class provides a safer, more easy-to-use API.

**Application Logic Layer: Use C++**

Implement business logic, state machines, and data processing in C++, utilizing features like classes, templates, and RAII to simplify code.

**Module Interfaces: Use `extern "C"`**

Use `extern "C"` declarations for interfaces between modules to ensure C and C++ modules can collaborate seamlessly. This maintains flexibility while avoiding name mangling issues.

------

## Run Online

Compare C and C++ GPIO encapsulation and ring buffer differences in code behavior and `sizeof` online:

<OnlineCompilerDemo
  title="Performance and Size Evaluation"
  source-path="code/examples/vol6/13_perf_eval.cpp"
  description="Compare C and C++ GPIO encapsulation and ring buffer implementations, observe sizeof differences"
  allow-run
  allow-x86-asm
  arm-source-path="code/examples/compiler_explorer/perf_eval_arm.cpp"
  allow-arm-asm
/>

## Exercise Time: Try It Yourself

### Exercise 1: Actual Measurement

Implement the three examples above on your development board and measure:

1. Flash usage (using `size`)
2. RAM usage (check `.bss` and `.data` sections)
3. Execution time (using DWT cycle counter)

### Exercise 2: Optimization Challenge

Try optimizing the ring buffer:

1. When Size is a power of two, replace modulo with bitwise operations (`% Size` → `& (Size - 1)`)
2. Implement zero-copy `peek` operation
3. Add an interrupt-safe version (disable interrupts or use atomic operations)

### Exercise 3: Design Decisions

Choose C or C++ for the following scenarios:

1. Simple UART driver (only send/receive) → **Your choice?**
2. Sensor fusion algorithm (Kalman filter) → **Your choice?**
3. 1ms real-time control loop → **Your choice?**
4. OTA firmware upgrade module → **Your choice?**

### Exercise 4: Code Review

Find the problems in the following C++ code:

```cpp
// Bad: Virtual function + std::function + exceptions
class BadButton {
public:
    virtual void update(bool pressed) {  // Virtual function overhead
        if (pressed && on_click_) {      // std::function overhead
            on_click_();                 // Potential exception throw
        }
    }
    std::function<void()> on_click_;    // Type erasure overhead
};
```

**Improved Version**:

```cpp
// Good: Static polymorphism + function pointer + no exceptions
class GoodButton {
public:
    using Callback = void(*)();          // Simple function pointer
    void update(bool pressed) {
        if (pressed && on_click_) {
            on_click_();                 // No exceptions allowed
        }
    }
    Callback on_click_ = nullptr;        // No overhead
};
```

## Final Words

Quoting Bjarne Stroustrup (the father of C++):

> "C++ is not a language you have to use in its entirety, it is a language you can choose to use."

In embedded systems, we need to be smart choosers, not blind followers. Use the powerful features of C++ to improve code quality while avoiding those that don't fit in resource-constrained environments.
