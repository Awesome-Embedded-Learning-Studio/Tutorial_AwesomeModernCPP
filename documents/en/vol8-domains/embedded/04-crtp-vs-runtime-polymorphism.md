---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Comparing CRTP and virtual function polymorphism
difficulty: intermediate
order: 4
platform: stm32f1
prerequisites:
- 'Chapter 1: 构建工具链'
reading_time_minutes: 8
tags:
- cpp-modern
- intermediate
- stm32f1
title: CRTP vs runtime polymorphism
---
# Compile-Time Polymorphism vs Runtime Polymorphism

When engineers talk about "polymorphism," the first thing that comes to mind is usually `virtual` and interfaces—what we call runtime polymorphism.

But modern C++ gives us another equally powerful set of tools: templates, CRTP, `std::variant`, and type erasure, which together form the world of **compile-time polymorphism**. The two might seem to differ only in "when the behavior is decided," but they actually involve trade-offs across performance, Flash and RAM usage, testability, ABI stability, compile times, and debugging experience. For embedded systems, these trade-offs are rarely academic—they are real engineering constraints.

## Aligning on Concepts

The most native form of polymorphism in C++ is **runtime polymorphism (dynamic polymorphism)**. This most common form typically involves calling virtual functions through a base class pointer or reference: the base class contains `virtual` functions, derived classes override them, and at runtime the object's actual type is used to index into the vtable and execute the corresponding implementation. The key point is that at the call site, only the base class is known at compile time; the actual binding happens at runtime. This relies on a vtable (one per class with virtual functions) plus a vptr inside the object (a pointer to the vtable).

Because of this, runtime polymorphism involves function forwarding.

**Compile-time polymorphism (static polymorphism)** uses templates, overloading, `constexpr`, CRTP (Curiously Recurring Template Pattern), and algebraic data types (`std::variant`/`std::visit`) to dispatch, inline, and optimize away different implementations at compile time. Function calls are resolved and expanded into direct calls or inlined during compilation, eliminating the cost of runtime indirection.

From an implementation perspective, runtime polymorphism generates one or more vtables, each object carries a vptr (consuming RAM), and every virtual function call is an indirect jump (which can hurt branch prediction). Compile-time polymorphism, on the other hand, typically generates multiple concrete function instances (template instantiation) that can be inlined and optimized, bringing call overhead close to that of a regular function call—or even achieving zero-overhead abstraction.

------

## Typical Code Comparison: Device Driver Interface

Imagine a simple scenario: abstracting a `Sensor` with a read operation. First, the runtime polymorphism version:

```cpp
struct ISensor {
    virtual ~ISensor() = default;
    virtual int read() = 0;
};

struct ADCSensor : ISensor {
    int read() override {
        // 直接访问 ADC 寄存器
        return read_adc_hw();
    }
};

void poll(ISensor* s) {
    int v = s->read(); // 虚函数调用
    // ...处理 v
}

```

Now the compile-time polymorphism (template) version:

```cpp
template<typename Sensor>
void poll(Sensor& s) {
    int v = s.read(); // 非虚，编译期解析
    // ...处理 v
}

struct ADCSensor {
    int read() { return read_adc_hw(); }
};

```

The difference is immediate: at `poll<ADCSensor>`, the template version can inline `read()`, eliminating the indirect call. The runtime polymorphism version, however, retains the vtable, indirect jump, and the object's vptr in the binary.

------

## Performance and Space (Two Resources Embedded Systems Care About)

### Execution Speed

Compile-time polymorphism wins with "zero-overhead abstraction"—hot paths in embedded systems (such as driver calls inside an ISR, or real-time paths) are perfect candidates for templates, enabling inlining and optimization. Runtime polymorphism adds an extra memory read (fetching the vptr to access the vtable) and an indirect jump on every call. Since the jump target is unfriendly to branch prediction, the resulting latency is not negligible in real-time scenarios.

### RAM and Flash

Runtime polymorphism: Each object typically carries a pointer to the vtable (vptr), consuming RAM (usually one pointer in size). The vtable itself resides in read-only memory (Flash), but the vptr in each object consumes noticeable RAM, especially when there are many objects. On the other hand, runtime polymorphism allows multiple objects to share function implementations through a single vtable, keeping Flash usage low (only one copy of each function body is generated).

Compile-time polymorphism: Template instantiation generates code (function/class instances) for each distinct template parameter, which can lead to binary growth (code bloat)—increasing Flash usage. However, the objects themselves do not need a vptr (saving RAM). On embedded devices where Flash is plentiful but RAM is tight, this is often a worthwhile trade: exchanging runtime overhead and RAM usage for increased Flash consumption.

### Startup Time and Predictability

Static initialization from template instantiation can be very explicit, with no hidden dynamic construction pitfalls (unless complex global objects are used). The vtable mechanism may indirectly depend on static construction or dynamic initialization order (especially when combined with non-`constexpr` static objects), complicating the startup process. In systems that require highly predictable startup behavior, compile-time polymorphism is easier to reason about and verify.

## CRTP (A Form of Static Polymorphism)

CRTP enforces interface checks on concrete implementations at compile time, and allows code reuse in the base class by calling into derived class implementations:

```cpp
template<typename Derived>
struct SensorBase {
    int read_and_scale() {
        int v = static_cast<Derived*>(this)->read();
        return scale(v);
    }
    // ...
};
struct ADCSensor : SensorBase<ADCSensor> {
    int read() { return read_adc_hw(); }
};

```

The advantage of CRTP is that it provides static dispatch while enabling code reuse. It is commonly used in driver frameworks and state machine implementations.

## `std::variant` / `std::visit`

When you need closed polymorphism (not arbitrary extension, but a finite set of known variants), `std::variant` + `std::visit` is an excellent choice: it enumerates all variants clearly at compile time, and `visit` generates branch tables or inlined logic at compile time. This avoids vtable overhead while being more flexible than passing template parameters (you can store objects of different types in a container).

```cpp
// 定义不同的消息类型
struct StartEvent { int priority; };
struct StopEvent { int reason_code; };

using Event = std::variant<StartEvent, StopEvent>;

// 使用 std::visit 处理事件
std::visit([](auto&& e) {
    // 处理不同类型
}, event);

```

In embedded systems, we need to watch `std::variant`'s memory footprint (it allocates space for the size of the widest variant)—but it stores type information internally, requiring no external vptr.

## Type Erasure

Through `std::function` or custom type-erased wrappers (typically with small-buffer optimization), we can achieve "near compile-time efficiency" interfaces without exposing template parameters, while maintaining runtime replaceability. The cost is implementation complexity and potential memory overhead (small buffer plus virtual-like calls). This approach is often used at the library or API layer to hide implementation details.

------

## Summary: There Is No Absolute "Better," Only "More Appropriate"

Compile-time polymorphism and runtime polymorphism are not opposing dogmas—they are two different tools in the toolbox. The embedded engineer's job is to select and mix them based on the target platform's constraints and the engineering workflow. My recommendations are:

- Start with the clearest, most understandable implementation (usually runtime polymorphism or simple functions), and nail down the functionality, interfaces, and tests first;
- When performance or resources become a bottleneck, identify hot spots and apply local optimizations using compile-time polymorphism (templates/CRTP/`constexpr`);
- Enable LTO and link-time deduplication to mitigate binary bloat from templates;
- Keep runtime polymorphism interfaces for cross-module, plugin-style architectures to preserve ABI stability and replaceability;
- At the design level, clearly separate "variation points" from "stable points": push invariant logic into compile time, and leave logic that needs flexible replacement to runtime.
