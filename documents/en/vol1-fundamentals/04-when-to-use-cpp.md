---
title: When to use C++ and which C++ features to use
description: Exploring when to choose C++ over C, and how to wisely use C++ features
  in embedded environments, including recommended, compromised, and prohibited features.
chapter: 0
order: 4
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
difficulty: beginner
reading_time_minutes: 18
prerequisites: []
related: []
cpp_standard:
- 11
- 14
- 17
- 20
platform: host
---
# When to Use C++ and Which Features to Use

Honestly, whenever I see the "C vs C++" holy wars erupt in the embedded community, I find it quite frustrating. The debates quickly devolve into matters of faith—C developers treating C++ as a cult, and C++ developers treating C as the Stone Age. But the real question is: for this specific project, on this specific hardware, is using this language actually worth it? No one can answer that question for you, but I can share the lessons learned from real-world projects to help you avoid some common pitfalls.

In this chapter, we need to clarify two things: first, what kind of projects are worth migrating to C++; and second, once we adopt C++, which features we should embrace, which we should use with caution, and which we should avoid altogether.

## When C++ Is Worth It

Let's start with a major premise: if your project's codebase exceeds tens of thousands of lines and includes multiple subsystems that require clear interface boundaries, the advantages of C++ will start to shine. Of course, you can maintain projects of this scale in C, but your team will need to invest significant effort into maintaining the code's organizational structure—manually managing module divisions, hand-writing interface abstractions, and manually ensuring type safety. C++ features like classes, namespaces, and templates handle exactly these tasks at the language level. Especially when multiple subsystems require strict interface definitions, C++'s type system can catch a large number of interface misuse errors at compile time, whereas in C, these often don't surface until runtime.

Type safety is literally a matter of life and death in safety-critical systems. Automotive electronics, medical devices, aerospace—in these domains, the ubiquitous implicit type conversions and loose typing commonly seen in C are ticking time bombs. C++'s strong type system, enum classes, reference semantics, and correctness guarantees can prevent a massive number of low-level errors right at the compiler level. This isn't some fancy theoretical advantage; it tangibly reduces the probability of bugs making it into the final product.

Code reuse requirements are another important consideration. If your project needs to reuse components across multiple product lines, or if there are many similar but not identical functional modules, C++'s template mechanism can truly flex its muscles—it can generate type-safe code at compile time with zero runtime overhead. Compared to the "generics" cobbled together with macros and `void*` in C, C++'s approach is both safe and elegant.

That being said, adopting C++ requires the team to have the corresponding technical readiness. If everyone on the team has only ever written C, has never heard of RAII (Resource Acquisition Is Initialization), and there are no training or code review mechanisms in place, rushing into C++ will most likely end in disaster. Conversely, if the team has members familiar with modern C++ practices who can establish and enforce reasonable coding standards, the advantages of C++ can truly be realized.

### When C Remains the Better Choice

On the flip side, in certain scenarios, sticking with C is the more pragmatic choice. When the target platform is extremely resource-constrained—for example, a low-cost MCU (Microcontroller Unit) with less than 32KB of Flash and less than 4KB of RAM—the simplicity and predictability of C are its greatest advantages. For simple applications with a very small codebase (say, under five thousand lines), introducing C++ only adds unnecessary complexity. Additionally, if the project requires deep integration with a large amount of legacy C code, or if the target platform's toolchain has incomplete C++ support (which is not uncommon on some niche chips), continuing to use C is often the most hassle-free decision.

## Our Best Friends: Core Features We Recommend

Alright, let's assume you've decided to go with C++. Next, we need to figure out which features should become part of your everyday development toolkit. All of these features adhere to the zero-overhead abstraction principle—you get better code organization without paying a runtime price.

### Classes and Encapsulation

Classes and encapsulation are among the most fundamental and valuable features in C++. Let's look at a practical example—a sensor driver. In C, you might be used to writing something like this:

```c
// C 风格：全局变量 + 裸函数
volatile uint32_t* sensor_reg = (volatile uint32_t*)0x40010000;

void sensor_enable(void) {
    *sensor_reg |= 0x01;
}

uint16_t sensor_read(void) {
    return (uint16_t)(*sensor_reg >> 16);
}
```

The problem with this approach is obvious: the register is a global variable, and anything can directly manipulate it with nothing to stop it. The C++ approach encapsulates the register addresses and access logic inside a class, exposing only the `init` and `read` interfaces to the outside world:

```cpp
class SensorDriver {
private:
    uint32_t base_address_;
    volatile uint32_t* const reg_;

public:
    explicit SensorDriver(uint32_t addr)
        : base_address_(addr),
          reg_(reinterpret_cast<volatile uint32_t*>(addr)) {}

    void enable() {
        *reg_ |= 0x01;
    }

    uint16_t read() const {
        return static_cast<uint16_t>(*reg_ >> 16);
    }
};
```

The key point here is that the code generated by the compiler is virtually indistinguishable from the machine code of the C version above—member functions are inlined by default, so there is no performance penalty. However, external code can no longer directly touch the registers, drastically reducing the chance of errors.

### Namespaces

Naming collisions in large projects are a headache, especially after integrating several third-party libraries. The traditional C approach is to prefix function names, such as `sensor_init`, `sensor_read`, `sensor_deinit`. It works, but it's not elegant. C++ namespaces provide a more systematic solution—by organizing related functions and classes into logical groups, the problem of naming collisions is eliminated at its root:

```cpp
namespace drivers {
namespace gpio {
    void init();
    void set_pin_mode(uint8_t pin, PinMode mode);
    bool read_pin(uint8_t pin);
}

namespace uart {
    void init(uint32_t baud_rate);
    void send(const uint8_t* data, size_t len);
}
}

// 调用时一目了然
drivers::gpio::init();
drivers::uart::init(115200);
```

The best part is that namespaces are a purely compile-time feature and incur absolutely no runtime overhead.

### Reference Semantics

Compared to pointers, references have two key advantages: first, references cannot be null, so there is no need for null pointer checks; second, reference syntax more clearly expresses a function's intent. When we need to pass a large struct without copying it, a `const` reference is both efficient and safe; when a function needs to modify a passed parameter, a non-`const` reference clearly indicates this intent:

```cpp
// 用 const 引用传递大型结构体——避免拷贝，且不能被修改
void process_data(const SensorData& data) {
    uint16_t value = data.temperature;
    // ...
}

// 非 const 引用表明函数会修改参数
bool try_read(SensorData& output) {
    if (data_available()) {
        output.temperature = read_temperature();
        output.humidity = read_humidity();
        return true;
    }
    return false;
}
```

Compared to the C pointer approach, we eliminate the `NULL` check, and the code is more concise. Under the hood, a reference is usually just a pointer, so there is no additional performance overhead.

### Compile-Time Calculation (constexpr)

`constexpr` is a killer feature of modern C++ in embedded development. It allows the compiler to complete calculations during the compilation phase, generating code that directly contains the resulting value with zero runtime overhead. For example, calculating the divider coefficient for a serial port baud rate:

```cpp
constexpr uint32_t calculate_baud_rate_divisor(uint32_t sysclk, uint32_t baud) {
    return sysclk / (16 * baud);
}

// 编译期就算好了，生成的代码里直接是结果值 39
constexpr uint32_t divisor = calculate_baud_rate_divisor(72000000, 115200);
```

The traditional approach is to perform the division at runtime, but with a `constexpr` function, this division is completed during the compilation phase. When the program runs, the value of the variable is already the final result, requiring no calculation at all. This not only improves performance but also makes the code's intent much clearer. It can even be used directly as an array size:

```cpp
constexpr size_t kBufferSize = calculate_baud_rate_divisor(1000, 10);
uint8_t buffer[kBufferSize];
```

### Strongly-Typed Enums (enum class)

Traditional C enums have a few headache-inducing problems: they implicitly convert to integers, values from different enums can be mixed, and enum names pollute the enclosing scope. The `enum class` introduced in C++11 solves all of these problems at once:

```cpp
enum class PinMode : uint8_t {
    kInput = 0,
    kOutput = 1,
    kAlternate = 2,
    kAnalog = 3
};

enum class PullMode : uint8_t {
    kNoPull = 0,
    kPullUp = 1,
    kPullDown = 2
};

void set_mode(uint8_t pin, PinMode mode) {
    switch (mode) {
        case PinMode::kInput:  /* ... */ break;
        case PinMode::kOutput: /* ... */ break;
        default: break;
    }
}
```

Now, if you try to pass in the wrong type, the compiler will directly report an error, rather than silently accepting it like a C enum and giving you a runtime surprise:

```cpp
set_mode(5, PinMode::kOutput);        // 正确
// set_mode(5, PullMode::kPullUp);    // 编译错误：类型不匹配
// set_mode(5, 1);                    // 编译错误：不能隐式转换
```

Furthermore, compilers typically optimize an `enum class` into a plain integer, so there is absolutely no performance loss.

## Templates: Don't Swing a Good Sword Blindly

Templates are the most powerful but also the most easily abused feature in C++. In embedded environments, we need to strike a balance between code reuse and code bloat.

### Simple Templates: Use Freely

Simple function templates are usually safe because they are often inlined by the compiler, and the最终 generated code is exactly the same as a hand-written type-specific version:

```cpp
template<typename T>
inline void swap(T& a, T& b) {
    T temp = a;
    a = b;
    b = temp;
}

uint32_t x = 10, y = 20;
swap(x, y);  // 编译器生成 swap<uint32_t>
```

### Class Templates: Depends on the Scenario

Class templates are also very useful in appropriate scenarios. A typical example is a fixed-size ring buffer. By making the element type and size template parameters, we achieve a generic yet zero-overhead buffer:

```cpp
template<typename T, size_t N>
class CircularBuffer {
private:
    T buffer_[N];
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;

public:
    bool push(const T& item) {
        if (count_ >= N) return false;
        buffer_[tail_] = item;
        tail_ = (tail_ + 1) % N;
        ++count_;
        return true;
    }

    bool pop(T& item) {
        if (count_ == 0) return false;
        item = buffer_[head_];
        head_ = (head_ + 1) % N;
        --count_;
        return true;
    }

    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    bool full() const { return count_ >= N; }
};
```

Because the size is determined at compile time, the compiler can perform thorough optimizations.

⚠️ But there is a pitfall to watch out for: every different combination of template parameters generates a separate piece of code. If you instantiate both `RingBuffer<uint8_t, 32>` and `RingBuffer<uint8_t, 64>`, you will have two nearly identical copies of code in Flash. So, use templates, but don't abuse them.

### SFINAE (Substitution Failure Is Not An Error) and if constexpr: Use If Needed, But Don't Overcomplicate

More advanced template techniques, such as SFINAE (Substitution Failure Is Not An Error) and type traits, should be used with caution in embedded environments. C++17's `if constexpr` is much clearer than traditional SFINAE. If you genuinely need to select different implementations based on type, prefer using it:

```cpp
template<typename T>
void serialize(const T& value, uint8_t* buffer) {
    if constexpr (std::is_integral<T>::value) {
        // 整数类型：直接写入
        *reinterpret_cast<T*>(buffer) = value;
    } else if constexpr (std::is_floating_point<T>::value) {
        // 浮点类型：同样直接写入
        *reinterpret_cast<T*>(buffer) = value;
    }
}
```

Only consider these techniques when you truly need compile-time type constraints, and try to keep things simple. If you make template metaprogramming too complex in an embedded context, even you won't understand it two weeks later.

## Features That Require a Disclaimer

Some C++ features aren't completely off-limits, but they require extra care. The following features are double-edged swords in embedded projects—used well, they are powerful tools; used poorly, they are ticking time bombs.

### Constructors and Destructors

Simple, fast construction and destruction are perfectly fine. RAII (Resource Acquisition Is Initialization) style resource management is the best example—acquiring resources in the constructor and automatically releasing them in the destructor is both safe and elegant:

```cpp
class ScopedLock {
private:
    Mutex& mutex_;

public:
    explicit ScopedLock(Mutex& m) : mutex_(m) {
        mutex_.lock();
    }

    ~ScopedLock() noexcept {
        mutex_.unlock();
    }

    // 禁止拷贝和赋值
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
};
```

Using it is extremely simple; it automatically releases when leaving scope, and even if you `return` early, you won't forget to unlock:

```cpp
void critical_section() {
    ScopedLock lock(global_mutex);
    // 临界区代码...
}  // 自动释放锁
```

⚠️ But if you do dynamic memory allocation (`new`) in a constructor, call hardware initialization that might fail, or create objects requiring destruction in an interrupt context, you're just asking for trouble. The correct approach is to keep constructors simple and use an explicit `init` function to handle initializations that might fail:

```cpp
class GoodDriver {
    static constexpr size_t kBufferSize = 1024;
    uint8_t buffer_[kBufferSize];  // 栈上分配，不用 new
    bool initialized_ = false;

public:
    GoodDriver() = default;  // 简单的默认构造

    bool init() {
        if (!init_hardware()) {
            return false;
        }
        initialized_ = true;
        return true;
    }

    ~GoodDriver() noexcept = default;
};
```

Additionally, destructors must always be marked `noexcept`—if an exception is thrown during destruction, the program will directly call `std::terminate`, which in an embedded system means a crash.

### Exception Handling: Disable by Default

In embedded projects, my recommendation is to simply turn off exceptions via the `-fno-exceptions` compiler flag. This isn't prejudice—turning off exceptions can reduce code size by 10% to 30%, eliminate unpredictable execution times, and avoid the extra RAM overhead brought by stack unwinding. Moreover, many embedded toolchains have incomplete exception support to begin with, making debugging nearly impossible if something goes wrong.

So, how do we handle errors? Use error codes. While not as elegant as exceptions, they are predictable, efficient, and easy to use for worst-case analysis:

```cpp
enum class ErrorCode : uint8_t {
    kOk = 0,
    kInvalidParameter,
    kTimeout,
    kHardwareError,
    kBufferFull,
    kNotInitialized
};

ErrorCode init_sensor(uint8_t address) {
    if (address == 0 || address > 127) {
        return ErrorCode::kInvalidParameter;
    }

    if (!check_hardware()) {
        return ErrorCode::kHardwareError;
    }

    return ErrorCode::kOk;
}
```

For scenarios where you need to return both a value and an error state, you can use a simple struct to separate the two:

```cpp
struct Result {
    ErrorCode error;
    uint16_t value;

    bool is_ok() const { return error == ErrorCode::kOk; }
};

Result read_sensor() {
    Result res;
    if (!is_initialized()) {
        res.error = ErrorCode::kNotInitialized;
        res.value = 0;
        return res;
    }
    res.error = ErrorCode::kOk;
    res.value = read_hardware_register();
    return res;
}
```

⚠️ Unless your system has abundant Flash and RAM, relaxed real-time requirements, a toolchain with complete exception support, and a team with extensive experience using exceptions—don't touch exceptions.

### RTTI: Just Turn It Off

Runtime Type Information (RTTI) should also be disabled by default using the `-fno-rtti` compiler flag. RTTI increases code size, requires extra metadata storage, and introduces performance overhead. In the vast majority of embedded scenarios, if you need to determine an object's type, adding a type identifier enum to the base class is entirely sufficient. There is absolutely no need for `dynamic_cast`.

### Virtual Functions: Use Sparingly

Virtual functions provide runtime polymorphism, which is indeed useful when designing driver abstraction layers. But the cost is very real: every object containing virtual functions needs a vtable pointer (4 to 8 bytes), virtual function calls are 5% to 10% slower than direct calls, and they can prevent the compiler from performing inline optimizations.

If you only need compile-time polymorphism, passing the concrete type via a template parameter is sufficient, with zero runtime overhead. Virtual functions should only be used in scenarios that genuinely require runtime polymorphism, and they should be avoided on performance-critical paths where they would be called frequently.

## Features to Avoid If Possible

For the following features, our advice in embedded environments is to avoid them if at all possible.

### Dynamic Memory Allocation

`new`, `delete`, `malloc`, `free`—these operations, which are commonplace in desktop development, are risk sources in embedded systems. Heap fragmentation can cause memory allocation to fail after the system has been running for a while, and such failures are extremely difficult to reproduce and debug. The non-deterministic execution time of dynamic allocation also makes worst-case analysis impossible.

The alternative is to use fixed-size data structures. The standard library's `std::array` is a safe choice, as it involves no dynamic memory. If you need dynamically sized containers, you can implement statically-capacity versions, or use a memory pool—pre-allocating a fixed number of equally sized memory blocks where allocation and deallocation are both O(1) and fragmentation is impossible.

### Most STL Containers

`std::vector`, `std::list`, `std::map`, `std::string`—these containers all rely on dynamic memory allocation and are unsuitable for embedded environments. The reference counting in `std::shared_ptr` involves atomic operations, which carry significant overhead on certain platforms. `std::iostream` should be avoided entirely; a simple `std::cout << "hello"` can introduce over 50KB of code.

However, it's not that the entire standard library is off-limits. Algorithms in `std::sort` and `std::copy` (note that some allocate temporary memory), compile-time utilities like `std::array`, and type traits like `std::is_integral` and `std::is_same` from `<type_traits>`—these are all great, zero-overhead or low-overhead tools.

If you really need containers, check out the Embedded Template Library (ETL), which provides fixed-size containers that don't use dynamic memory and are compatible with STL interfaces.

### Standard Multithreading Library

Components like `std::thread` and `std::mutex` have large code footprints and rely on specific operating system support. In embedded systems, we typically use the primitives provided directly by the RTOS—FreeRTOS tasks, semaphores, and queues, or the standard CMSIS-RTOS interfaces. These are optimized for embedded environments and consume fewer resources.

## A Few Final Words

Choosing the right features is only the first step. To truly implement them in a project, you also need to establish clear coding standards that specify what is allowed, what is forbidden, and what requires review. Code reviews must become standard practice, with special attention paid to whether forbidden features are being used secretly, whether templates are too complex and causing code bloat, and whether virtual functions are appearing where they shouldn't. Static analysis tools can help us automatically detect many of these issues, so don't skip this step.

On the performance side, regularly check the compiled binary size to ensure there is no unexpected bloat. Perform actual measurements on performance-critical code paths instead of relying on gut feelings. There are many compiler optimization options, but their effects need to be verified through actual testing—don't experiment directly in production environments; get it working on the development board first.

Language choice is not a matter of faith, but an engineering problem. Let data do the talking, select tools on a per-module basis, and enforce constraints using automated means. Just remember this one sentence: use the right tool for the right job, and don't turn tools into beliefs.
