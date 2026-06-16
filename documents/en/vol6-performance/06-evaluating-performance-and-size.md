---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Learn how to evaluate program performance and size overhead, and compare
  C and C++ behavior in embedded environments through actual measurements.
difficulty: beginner
order: 6
platform: host
prerequisites: []
reading_time_minutes: 31
related: []
tags:
- cpp-modern
- host
- intermediate
title: Performance and Size Evaluation
translation:
  source: documents/vol6-performance/06-evaluating-performance-and-size.md
  source_hash: 02e4a1266e0ee238aa7c3f7ad26794b9ba1e094ee183a3f1f96c7cb762a18ba9
  translated_at: '2026-06-16T04:07:58.091499+00:00'
  engine: anthropic
  token_count: 6915
---
# Modern Embedded C++ Tutorial — Does C++ Necessarily Cause Code Bloat?

Regarding performance evaluation and program size, I believe most programmers have a better feel for the former, while the latter might feel slightly unfamiliar—especially for friends working on host development. I believe that in an era where storage feels increasingly cheap, few people care about the distribution package size of desktop applications anymore. However, in the embedded industry, where a bit of Flash is as precious as gold, it is still necessary to consider program size.

This raises a question. You know this is the "Modern Embedded C++ Tutorial" (sometimes, the author writes it as "Embedded Modern C++ Tutorial"), but this is an old yet always controversial topic: **Does C++ inevitably cause code bloat?**

## Before We Start: Sharpen Your Axe

Before we start our code battle, let's make sure your toolbox contains these tools:

#### arm-none-eabi-gcc / arm-none-eabi-g++

This is the cross-compiler for the ARM platform from X86_64. Let's run through it:

```bash
arm-none-eabi-gcc --version
```

If you see the version number, congratulations! If you see "command not found", you might need to go to the ARM official website to download the toolchain first. The author uses Arch Linux, so I just use pacman or yay to install it.

> Note: The package name is `gcc-arm-none-eabi`, otherwise it will be missing standard dependencies. Try `arm-none-eabi-gcc` first. If the demo doesn't pull through, it's the standard EABI issue.

```bash
-fno-exceptions -fno-rtti
```

> These two parameters are the "diet pills" for using C++ in embedded systems. Without these, your firmware might bloat like dough with yeast due to the exception handling code.

------

## Round One: Start with Blinking an LED: GPIO Driver (It's just a light, how hard can it be?)

Our first task is to ground the previous content into reality. Let's see: with different languages and different programming paradigms, what does our code look like, and how does it actually perform?

### Task Brief

We want to implement a GPIO driver to control an LED. This is the "Hello World" of the embedded world, as classic as printing "Hello World" when learning programming. The features include:

- Turn light on/off (Um...)
- Toggle state
- PWM dimming (Show off a bit)

#### C Language Version — Plain and Simple

```c
typedef struct {
    volatile uint32_t* mod;   // Mode register
    uint32_t pin;             // Pin number
} GPIO_C;

void gpio_init(GPIO_C* gpio, volatile uint32_t* mod, uint32_t pin) {
    gpio->mod = mod;
    gpio->pin = pin;
    *mod |= (1 << pin);       // Set as output
}

void gpio_write(GPIO_C* gpio, bool state) {
    if (state)
        *gpio->mod |= (1 << gpio->pin);
    else
        *gpio->mod &= ~(1 << gpio->pin);
}

void gpio_toggle(GPIO_C* gpio) {
    *gpio->mod ^= (1 << gpio->pin);
}
```

This is the author's C programming style. Of course, some friends might not like structs. Well, I still recommend using structs, but don't pass them by value triggering a copy; instead, pass a pointer pointing to this object.

#### C++ Version — OOP

```cpp
class GPIO_CPP {
public:
    GPIO_CPP(volatile uint32_t* mod, uint32_t pin) : mod_(mod), pin_(pin) {
        *mod_ |= (1 << pin_); // Constructor initializes hardware
    }

    void write(bool state) {
        if (state)
            *mod_ |= (1 << pin_);
        else
            *mod_ &= ~(1 << pin_);
    }

    void toggle() {
        *mod_ ^= (1 << pin_);
    }

private:
    volatile uint32_t* mod_;
    uint32_t pin_;
};
```

A classic use of C++ is to adopt the Object-Oriented Programming (OOP) paradigm.

Of course, some friends might argue—who told you C++ is an OOP language? It's also a generic programming language. True, I have no objection; my own GPIO library is written with templates. But here, let's consider OOP first.

### Battle Analysis: Is the Difference Really Huge?

Let's not judge yet; let's look at the differences!

We save the C code above as `demo.c`, then use the full compilation command as follows:

```bash
arm-none-eabi-gcc -march=armv7-m -mcpu=cortex-m4 -mthumb -Os -c demo.c -o demo_c.o
```

Huh? You say you just single-click the IDE? Okay, let's talk about what this is doing.

------

#### `-march=armv7-m`

Specifies the use of an **ARM bare-metal cross-compiler**:

- `arm`: Target architecture is ARM
- `none`: No operating system (bare-metal)
- `eabi`: Embedded ABI

The generated code **cannot run on Linux / Windows**, but is used for MCU Flash.

------

#### `-mcpu=cortex-m4`

Specifies the **target CPU core model**:

- Generates **instructions specific to Cortex-M4**
- Enables M4-specific features (like DSP instructions)
- Ensures the instruction set matches the actual MCU exactly

Of course, if you want to try testing for M1, that works too. Switch to `cortex-m1`, you can try them all.

------

#### `-mthumb`

Forces the use of the **Thumb instruction set**:

- Cortex-M series **only supports Thumb**
- Instructions are more compact, code density is higher
- It is the "default working mode" for the M series

For Cortex-M, this is a **mandatory option, not an optimization option**.

------

#### `-Os`

**Optimization level targeting minimum code size**:

- Prioritizes reducing Flash usage
- On top of `-O2` / `-O1`, deliberately avoids code bloat
- Is the **most common and safest** optimization level in embedded systems

------

#### `-c`: **Compile only, do not link**

- Input: `.c` / `.cpp`
- Output: `.o` (object file)
- Does not generate an executable file

- Only `.o` files can be used for `size`
- Can accurately evaluate the code size of "a specific source file itself"

------

#### `-o demo_c.o`

Specifies the output file name:

```bash
-o demo_c.o
```

Avoids using the default `a.out`, which is especially clear when doing **multi-language / multi-version comparison experiments**.

------

### Let's Look at the Results

| Implementation | text (Code) | data | bss  | Total   |
| -------------- | ----------- | ---- | ---- | ------- |
| C Version      | 96 bytes    | 0    | 0    | 96      |
| C++ Version    | 24 bytes    | 0    | 0    | 24      |
| Difference     | **-72 bytes** | 0    | 0    | **-72** |

**Surprised? Unexpected?**

The C++ version is actually **72 bytes smaller**, a 75% reduction in code size! This reduction buys us:

- ✅ Better encapsulation (private members won't be randomly modified)
- ✅ Automatic initialization (won't forget to call `init`)
- ✅ Type safety (won't pass wrong pointers)
- ✅ More intuitive syntax (`gpio.write(true)` is much nicer than `gpio_write(&gpio, true)`)

**Key Discovery**: C++'s inline optimization makes the entire `example_cpp` function only 24 bytes, smaller than the C version's multiple functions combined! The compiler optimized all operations into direct register operations.

### The Truth at the Assembly Level

If you don't believe it, let's look at the assembly code generated by the compiler (this is the compiler's "X-ray vision"):

**C version `example_c` (96 bytes, containing multiple function calls):**

```asm
example_c:
    push    {r4, lr}
    mov     r4, r0
    bl      gpio_init
    mov     r0, r4
    movs    r1, #1
    bl      gpio_write
    mov     r0, r4
    bl      gpio_toggle
    pop     {r4, pc}
```

**C++ version `example_cpp` (Only 24 bytes, fully inlined):**

```asm
example_cpp:
    ldr     r3, [r0, #4]
    movs    r2, #1
    str     r2, [r3]
    ldr     r3, [r0, #4]
    ldr     r2, [r3]
    eors    r2, r2, #1
    str     r2, [r3]
    bx      lr
```

**See? The C++ version is more concise and efficient!**

The compiler inlined all C++ class methods, eliminating function call overhead and generating optimal register operations directly. The C version, due to function separation, required extra stack operations and function jumps.

**Conclusion**: C++ encapsulation is "zero-overhead abstraction"—not only zero overhead, but in many cases, even more efficient! This isn't marketing hype; it's real!

------

## Round Two: Ring Buffer (UART's Best Friend)

### Task Brief

The Ring Buffer is the "Swiss Army Knife" of embedded systems. When UART data floods in like a torrent, you need a place to temporarily store them. This is where the ring buffer comes in—a data container where the head and tail connect, and nothing is wasted.

Imagine a sushi conveyor belt; plates go around in a circle. You put plates down (write), and others take plates (read). As long as the belt isn't full, it keeps spinning.

#### C Language Version — Just Plain

```c
typedef struct {
    uint8_t buffer[256];
    volatile uint32_t head;
    volatile uint32_t tail;
} RingBuffer_C;

void rb_init(RingBuffer_C* rb) {
    rb->head = 0;
    rb->tail = 0;
}

bool rb_put(RingBuffer_C* rb, uint8_t data) {
    uint32_t next = (rb->head + 1) % 256;
    if (next == rb->tail) return false;
    rb->buffer[rb->head] = data;
    rb->head = next;
    return true;
}

bool rb_get(RingBuffer_C* rb, uint8_t* data) {
    if (rb->tail == rb->head) return false;
    *data = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % 256;
    return true;
}
```

#### C++ Version — Generic

Okay, here we write generic code—generics have a fault, which is the code bloat issue.

```cpp
template<size_t Size>
class RingBuffer_CPP {
    std::array<uint8_t, Size> buffer_;
    size_t head_ = 0;
    size_t tail_ = 0;
public:
    bool put(uint8_t data) {
        size_t next = (head_ + 1) % Size;
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
};
```

------

### Part 1: Ring Buffer Implementation Comparison

Let's look at the results:

| Implementation | text (Code) | data | bss  | Total   |
| -------------- | ----------- | ---- | ---- | ------- |
| C Version      | 218 bytes   | 0    | 0    | 218     |
| C++ Version    | 150 bytes   | 0    | 0    | 150     |
| Difference     | **-68 bytes** | 0    | 0    | **-68** |

**Surprised? Unexpected?**

The C++ version is actually **68 bytes smaller**, a 31% reduction in code size! This is while implementing full ring buffer functionality. This reduction buys us:

- ✅ Better encapsulation (internal indices won't be modified externally)
- ✅ Automatic constructor initialization (won't forget to call `init`)
- ✅ Type safety (won't pass wrong pointers)
- ✅ More intuitive method calls (`rb.put(data)` is much nicer than `rb_put(&rb, data)`)

**Key Discovery**: C++ eliminates function call overhead through inline optimization, and the compiler can better optimize class methods. The C version needs multiple independent functions (`rb_init`, `rb_put`, `rb_get`, `rb_available`, `rb_free_space`, `rb_clear`), while the C++ version fuses these operations more compactly through smart inlining.

### The Truth at the Assembly Level

If you don't believe it, let's look at the assembly code generated by the compiler:

**C version `example_c_rb` (depends on multiple functions):**

```asm
example_c_rb:
    push    {r4, lr}
    mov     r4, r0
    bl      rb_init
    movs    r2, #42
    mov     r0, r4
    bl      rb_put
    mov     r0, r4
    movs    r1, #0
    bl      rb_get
    pop     {r4, pc}
```

**C++ version `example_cpp_rb` (fully inlined):**

```asm
example_cpp_rb:
    movs    r2, #42
    str     r2, [r0]
    ldrb    r3, [r0]
    str     r3, [r0, #1]
    bx      lr
```

**See? The C++ version eliminated all function calls!**

The compiler inlined all methods together, reducing stack operations, function jumps, and register saves. The C version, because of function separation, needs extra `bl` instructions and stack frame setup for every `rb_put` and `rb_get`.

------

## Round Three: State Machine (The Art of Button Debouncing)

### Task Brief

Button debouncing is a "required course" for embedded engineers. Mechanical buttons generate chatter (bouncing) when pressed and released (like a spring vibrating back and forth). If not handled, one press might be registered as a dozen.

We want to implement a state machine to:

- Detect button press
- Detect button release
- Detect long press (holding for more than 1 second)
- Debounce (ignore chatter within 50ms)

### C Language Version: Classic State Machine

```c
typedef enum { IDLE, PRESSED, HOLD } State;
typedef void (*Callback)(void);

typedef struct {
    State state;
    uint32_t last_time;
    Callback on_press;
    Callback on_release;
} Button_C;

void button_init(Button_C* btn, Callback press_cb, Callback release_cb) {
    btn->state = IDLE;
    btn->on_press = press_cb;
    btn->on_release = release_cb;
}

void button_update(Button_C* btn, bool pin_state, uint32_t now) {
    switch (btn->state) {
        case IDLE:
            if (pin_state && (now - btn->last_time > 50)) {
                btn->state = PRESSED;
                if (btn->on_press) btn->on_press();
            }
            break;
        // ... other states
    }
    btn->last_time = now;
}
```

### C++ Version: Object-Oriented State Machine

```cpp
class Button_CPP {
    enum class State { Idle, Pressed, Hold };
    State state_ = State::Idle;
    uint32_t last_time_ = 0;
    std::function<void()> on_press_;
    std::function<void()> on_release_;

public:
    Button_CPP(std::function<void()> press_cb, std::function<void()> release_cb)
        : on_press_(press_cb), on_release_(release_cb) {}

    void update(bool pin_state, uint32_t now) {
        switch (state_) {
            case State::Idle:
                if (pin_state && (now - last_time_ > 50)) {
                    state_ = State::Pressed;
                    if (on_press_) on_press_();
                }
                break;
            // ... other states
        }
        last_time_ = now;
    }
};
```

### Battle Analysis: The Cost of std::function

| Implementation        | text (Code) | data | bss  | Total    |
| --------------------- | ----------- | ---- | ---- | -------- |
| C Version             | 172 bytes   | 0    | 0    | 172      |
| C++ Version (std::function) | 306 bytes   | 0    | 0    | 306      |
| Difference            | **+134 bytes** | 0    | 0    | **+134** |

**This time the difference is obvious!** The C++ version increased **code size by 78%**. The cost of these 134 bytes comes from these places:

- `std::function`'s type erasure mechanism (requires virtual function tables)
- Extra overhead for lambda captures
- Runtime support code for dynamic polymorphism

So, this is trying to tell you—our C++ doesn't mean all abstractions are zero overhead. Taking **`std::function` as an example: it brings significant code bloat (78% growth)**. Moreover: **lambda captures have hidden costs, because each lambda requires extra storage and management code. Friends familiar with Lambdas should know this—it generates a struct with an `operator()` call, storing every captured object**:

The alternative here is also simple:

```cpp
// Use function pointer or template callback instead
template<typename Callback>
class Button {
    Callback on_press_;
public:
    Button(Callback press_cb) : on_press_(press_cb) {}
    // ...
};
```

## Let's Talk

#### Code Size Comparison Table

Let's review:

**Case 1: GPIO Operation Encapsulation**

In the GPIO operation scenario, C++ class encapsulation showed surprising advantages. The C version required 96 bytes to implement `gpio_init`, `gpio_write`, `gpio_toggle`, and other functions, while the C++ version compressed the entire operation sequence to just 24 bytes through compiler inline optimization, reducing code size by 75%. This huge difference comes from the compiler's ability to fully inline C++ member function calls, eliminating function call overhead and stack frame management.

**Case 2: Ring Buffer Implementation**

The ring buffer implementation further validates C++'s advantages. The C version required implementing six independent functions: `rb_init`, `rb_put`, `rb_get`, `rb_available`, `rb_free_space`, `rb_clear`, totaling 218 bytes. The C++ version reduced code size to 150 bytes through class encapsulation and method inlining, saving 31% of space. The key is that the compiler can see the complete call chain, allowing for more aggressive optimization.

**Case 3: The Warning of std::function**

Not all C++ features are suitable for embedded development. When using `std::function` to implement callbacks, code swelled from the C version's 172 bytes to 306 bytes, an increase of 78%. This is because `std::function` requires type erasure mechanisms, virtual table support, and management code for lambda captures. This case reminds us that in resource-constrained environments, we must carefully choose which C++ features to use.

| Feature               | Code Growth  | Suggestion                                           |
| --------------------- | ------------ | ----------------------------------------------------- |
| Class Encapsulation (Basic) | -75% to -31% | Highly Recommended (Actually smaller in tests)       |
| Class Encapsulation (With Templates) | +4%          | Highly Recommended (Almost zero overhead)             |
| Virtual Functions     | +20-40%      | Use with caution (Consider CRTP as alternative)       |
| Exception Handling    | +50-100%     | Disable (`-fno-exceptions`)                           |
| RTTI                  | +30-50%      | Disable (`-fno-rtti`)                                 |
| std::function         | +78%         | Use with caution (Replace with function pointer or template) |
| Templates (Generic Containers) | +4%          | Highly Recommended (Compile-time optimization)        |

### Performance Comparison Table

Based on cycle count analysis at the assembly level:

| Category             | C Implementation | C++ Implementation | Difference |
| -------------------- | ----------------- | ------------------ | ---------- |
| Single GPIO Operation | 8-10 cycles       | 8-10 cycles       | 0%         |
| Buffer Read/Write    | 12-15 cycles      | 12-15 cycles      | 0%         |
| Complete Inlined Op  | Needs function call | Fully inlined     | C++ Faster |

**Key Discovery**: With optimizations enabled, C++'s zero-overhead abstraction is not a marketing slogan, but a verifiable fact. The assembly code generated by the compiler shows that C++ class methods and C functions are identical at the single operation level, while in complex operation scenarios, C++ is even faster due to inline optimization.

------

## Best Practices: How to Elegantly Use C++ in Embedded Systems

### 1. Compiler Options (Slimming Configuration)

The golden compiler configuration for embedded C++ development is as follows:

```bash
-fno-exceptions -fno-rtti -Os -ffunction-sections -fdata-sections
```

This configuration ensures C++ code remains efficient and compact in embedded environments. Tests show that correctly configured C++ code can achieve a size comparable to or even smaller than C.

### 2. Recommended C++ Features

The following features are verified by tests to perform excellently in embedded systems:

**Classes and Objects (Highly Recommended)**

Class encapsulation is a core advantage of C++, capable of abstracting hardware resources into objects. Tests show that simple class encapsulation not only doesn't increase code size but actually reduces it due to compiler optimization. For example, encapsulating GPIO registers into a class provides type safety and better interfaces while maintaining zero overhead.

**Constructors and Destructors (Highly Recommended)**

Constructors provide automatic initialization, and destructors implement the RAII pattern. This is C++'s most powerful resource management mechanism. In embedded systems, destructors can automatically shut down peripherals and release resources, avoiding leaks. Compilers can usually fully inline simple constructors.

**Templates (Highly Recommended)**

Templates provide compile-time code generation with absolutely zero runtime overhead. Ring buffer tests show the template version increases code size by only 4% while providing type safety and size parameterization. Compared to C macros, templates are safer and easier to debug.

**constexpr (Highly Recommended)**

`constexpr` functions calculate at compile time, embedding results directly into code. Can be used for calculating configuration parameters, lookup table generation, etc., with completely zero runtime overhead.

**References and Inline Functions (Highly Recommended)**

References avoid unnecessary copies, and inline functions eliminate function call overhead. In embedded systems, reasonable use of references can significantly improve performance, especially when passing structs.

**Operator Overloading (Moderately Recommended)**

Operator overloading makes code more intuitive, e.g., using `buffer << data` instead of `buffer_write(data)`. As long as it's not abused, operator overloading brings no extra overhead.

### 3. Cautiously Used C++ Features

The following features have certain overheads and need to be weighed based on the actual situation:

**Virtual Functions (Use with Caution)**

Virtual functions introduce a vtable, adding a 4-byte pointer overhead per object, and each call requires an indirect jump. If polymorphism is truly needed, consider using CRTP (Curiously Recurring Template Pattern) to implement compile-time polymorphism, avoiding runtime overhead.

**std::function (Use with Caution)**

Tests show `std::function` causes 78% code bloat. If a callback mechanism is needed, prioritize function pointers (same overhead as C) or template callbacks (zero overhead). Only consider `std::function` when lambdas with captured state are needed.

**Dynamic Memory Allocation (Use with Caution)**

`new` and `delete` can lead to memory fragmentation in embedded systems. Suggest using placement new with a static memory pool, or using stack objects. If dynamic memory must be used, consider custom allocators.

**STL Containers (Use with Caution)**

Standard library containers like `std::vector` and `std::map` can be large in implementation. Suggest testing code size first, or using container libraries specifically optimized for embedded (like EASTL). For simple scenarios, hand-rolled fixed-size containers might be more appropriate.

### 4. Forbidden C++ Features

The following features should be completely avoided in embedded systems:

**Exception Handling (Forbidden)**

The exception handling mechanism can cause code bloat of 50-100% and introduces unpredictable execution paths. Embedded systems need deterministic behavior; use error codes or assertions instead of exceptions. Must add the `-fno-exceptions` compiler option.

**RTTI (Forbidden)**

Run-Time Type Information increases code by 30-50% and is rarely needed in embedded systems. Disable with `-fno-rtti`. If type identification is needed, a simple manual type tag system can be implemented.

**iostream Library (Forbidden)**

`std::cout` and `std::cin` introduce huge code (tens of KB), far beyond what embedded systems can bear. Use traditional `printf`/`scanf` or specialized embedded logging libraries.

**Multiple Inheritance (Forbidden)**

Multiple inheritance increases complexity and code size, and can lead to the diamond problem. In embedded systems, single inheritance or composition patterns are sufficient.

------

## Practical Advice: When to Use C, When to Use C++?

### Scenarios for Choosing C

**Extremely Resource-Constrained Environments**

When the target hardware has less than 8KB Flash and less than 1KB RAM, C is the safer choice. Such systems are usually simple sensor nodes or controllers that don't require complex abstractions.

**Team Skill Stack Limitations**

If team members are unfamiliar with C++, or the project timeline is tight, forcing C++ might do more harm than good. C has a gentler learning curve and is easier to master.

**Pure C Codebase Integration**

When integrating a large amount of existing C code, using C avoids the hassle of mixed programming. Although C++ can call C code, in some cases, a pure C project is simpler.

**Insufficient Toolchain Support**

Some older or specialized compilers have incomplete C++ support and may produce inefficient code. In this case, C is the more reliable choice.

### Scenarios for Choosing C++

**Medium to High Resource Systems**

When Flash is greater than 16KB and RAM is greater than 2KB, C++ advantages start to show. Such systems have enough space to accommodate C++ abstraction mechanisms while benefiting from encapsulation and type safety.

**Complex State Management**

When implementing complex logic like state machines, protocol stacks, or sensor fusion, C++ class encapsulation can significantly reduce complexity. Objects can encapsulate state and behavior, making code easier to maintain.

**Need Code Reuse**

When there are multiple similar modules (e.g., multiple UARTs, multiple timers), C++ templates are safer and easier to debug than C macros. Templates provide compile-time type checking and parameterization.

**Modern Development Practices**

If the team is familiar with modern C++ (C++11 and later) and can correctly use features like smart pointers, move semantics, and lambdas, development efficiency will significantly improve.

### Mixed Usage (Best Practice)

Many successful embedded projects adopt a layered mixed strategy:

**Low-Level Driver Layer: Use C**

Low-level drivers that directly manipulate registers are written in C to ensure stability and portability. This code is usually not complex, and C is sufficient.

**Middle Abstraction Layer: Use C++**

Wrap low-level drivers into C++ classes to provide object-oriented interfaces. For example, wrapping a UART driver into a `SerialPort` class provides a safer, more easy-to-use API.

**Application Logic Layer: Use C++**

Business logic, state machines, and data processing are implemented in C++, utilizing features like classes, templates, and RAII to simplify code.

**Module Interface: Use `extern "C"`**

Interfaces between modules use `extern "C"` declarations to ensure C and C++ modules can collaborate seamlessly. This maintains flexibility while avoiding name mangling issues.

------

## Online Run

Compare C and C++ GPIO encapsulation and ring buffer differences in code behavior and `sizeof` online:

<OnlineCompilerDemo
  title="Performance and Size Evaluation"
  source-path="code/examples/vol6/13_perf_eval.cpp"
  description="Compare C and C++ GPIO encapsulation and ring buffer implementation, observe sizeof differences"
  allow-run
  allow-x86-asm
  arm-source-path="code/examples/compiler_explorer/perf_eval_arm.cpp"
  allow-arm-asm
/>

## Exercise Time: Try It Yourself

### Exercise 1: Actual Measurement

Implement the three examples above on your development board and measure:

1. Flash usage (use `size`)
2. RAM usage (check `.bss` and `.data` sections)
3. Run time (use DWT cycle counter)

### Exercise 2: Optimization Challenge

Try to optimize the ring buffer:

1. When Size is a power of 2, replace modulo with bitwise operations (`% Size` → `& (Size - 1)`)
2. Implement zero-copy `peek` operation
3. Add interrupt-safe version (disable interrupts or use atomic operations)

### Exercise 3: Design Decisions

Choose C or C++ for the following scenarios:

1. Simple UART driver (only send/receive) → **Your choice?**
2. Sensor fusion algorithm (Kalman filter) → **Your choice?**
3. 1ms real-time control loop → **Your choice?**
4. OTA firmware upgrade module → **Your choice?**

### Exercise 4: Code Review

Find the problems in the following C++ code:

```cpp
class BadExample {
    std::vector<int> data;
public:
    void add(int val) { data.push_back(val); }
    void process() {
        for (auto& d : data) {
            // Complex processing
        }
    }
};
```

**Improved Version**:

```cpp
template<size_t N>
class GoodExample {
    std::array<int, N> data;
    size_t count = 0;
public:
    bool add(int val) {
        if (count >= N) return false;
        data[count++] = val;
        return true;
    }
    void process() {
        for (size_t i = 0; i < count; ++i) {
            // Complex processing
        }
    }
};
```

## Final Words

Quoting Bjarne Stroustrup (Father of C++):

> "C++ is not a language you have to use in its entirety, but a language you can choose to use."

In embedded systems, we need to be smart choosers, not blind followers. Use C++'s powerful features to improve code quality while avoiding those that don't fit in resource-constrained environments.
