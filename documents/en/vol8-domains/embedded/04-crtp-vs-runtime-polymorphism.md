---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Comparing CRTP and Virtual Function Polymorphism
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
title: CRTP vs Runtime Polymorphism
translation:
  source: documents/vol8-domains/embedded/04-crtp-vs-runtime-polymorphism.md
  source_hash: 95df4c381cdb564a05ec206b9b503fd8220265fcfee7bd66011a07c6eb5a71c9
  translated_at: '2026-05-26T12:19:12.971255+00:00'
  engine: anthropic
  token_count: 1038
---
# Compile-Time Polymorphism vs. Runtime Polymorphism

In engineering practice, when we say "polymorphism," the first reaction is often `virtual` functions and interfaces—otherwise known as runtime polymorphism.

But modern C++ gives us another equally powerful set of tools: templates, CRTP, `std::variant`, type erasure, and more. These form the world of **compile-time polymorphism**. The two may seem to differ only in "when the behavior is determined," but in reality, they involve multi-dimensional trade-offs: performance, Flash and RAM usage, testability, ABI stability, compile time, and debugging experience. For embedded systems, these trade-offs are often not academic, but real engineering constraints.

## Aligning Our Concepts First

The most native form of polymorphism supported in C++ from the beginning is **runtime polymorphism (dynamic polymorphism)**. This most common form of polymorphism typically refers to calling virtual functions through a base class pointer or reference: the base class contains `virtual` functions, derived classes override them, and at runtime, the object's actual type is used to index into the vtable to execute the corresponding implementation. The key point is that at the call site, only the base class is known at compile time; the actual binding happens at runtime. Its implementation relies on a vtable (one for each class with virtual functions) plus a vptr inside the object (a pointer to the vtable).

As you can see, runtime polymorphism involves function forwarding.

**Compile-time polymorphism (static polymorphism)**, on the other hand, uses templates, overloading, `constexpr`, CRTP (Curiously Recurring Template Pattern), and algebraic data types (`std::variant`/`std::expected`) to dispatch, inline, and optimize away different implementations during compilation. Function calls can be resolved and expanded into direct calls or inlined at compile time, thereby eliminating the cost of runtime indirect calls.

From an implementation perspective, runtime polymorphism generates one or more vtables, and each object carries a vptr (consuming RAM). Every virtual function call is an indirect jump (which can affect branch prediction). Compile-time polymorphism, however, typically generates multiple concrete function instances (template instantiation), which can be inlined and optimized. The call overhead can approach that of a normal function call, or even achieve zero-overhead abstraction.

------

## Typical Code Comparison: Device Driver Interface

Imagine a simple scenario: abstracting a `Sensor` with a read operation. Let's look at the runtime polymorphism version first:

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

Now let's look at the compile-time polymorphism (template) version:

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

The difference is immediate: the template version can inline `sensor.read()` at ``poll<ADCSensor>``, eliminating the indirect call. The runtime polymorphism version, however, retains the vtable/indirect jump and the object's vptr in the binary.

<OnlineCompilerDemo
  title="Compile-Time Polymorphism: Inlining Opportunities with Template poll"
  source-path="code/examples/chapter02/04_crtp_polymorphism/compile_time_polymorphism.cpp"
  arm-source-path="code/examples/compiler_explorer/static_polymorphism_arm.cpp"
  description="This example is runnable; when viewing the assembly, you can observe the optimization space of the template version on concrete Sensor types."
  allow-run
  allow-x86-asm
  allow-arm-asm
/>

------

## Performance and Space (Two Resources Embedded Systems Often Care About)

### Execution Speed

Compile-time polymorphism wins with "zero-overhead abstraction"—hot paths in electronic systems (such as driver calls in an ISR, or real-time paths) are extremely well-suited for templating to enable inlining and optimization. Runtime polymorphism adds an extra memory read (reading the vptr to access the vtable) and an indirect jump on every call. Furthermore, the jump target is unfriendly to branch prediction, and the resulting latency is not negligible in real-time scenarios.

### RAM and Flash

Runtime polymorphism: Each object typically carries a pointer to the vtable (vptr), which consumes RAM (usually the size of one pointer). The vtable itself resides in read-only memory (Flash), but the object's vptr consumes notable RAM, especially when there are a large number of objects. On the other hand, runtime polymorphism allows multiple objects to share function implementations through a single vtable, resulting in a smaller Flash footprint (only one copy of the function body is generated).

Compile-time polymorphism: Template instantiation generates code (function/class instances) for each distinct template argument, which can lead to binary growth (code bloat), meaning increased Flash usage. However, the objects themselves do not need to retain a vptr (saving RAM). On embedded devices where Flash space is sufficient but RAM is tight, this is often a worthwhile trade-off: exchanging runtime overhead and RAM usage for increased Flash consumption.

### Startup Time and Predictability

Static initialization resulting from template instantiation can be very explicit, without the hidden dangers of dynamic construction (unless complex global objects are used). The vtable mechanism may indirectly depend on static construction/dynamic initialization order (especially when combined with non-`constexpr` static objects), complicating the startup process. In systems that require highly predictable startup behavior, compile-time polymorphism is easier to reason about and verify.

## CRTP (A Form of Static Polymorphism)

CRTP enforces the interface of a concrete implementation at compile time, and allows code reuse in the base class while calling into the derived class's implementation:

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

The advantage of CRTP is that it provides static dispatch while enabling code reuse. It is commonly used in driver frameworks, state machine implementations, and more.

## `std::variant` / `std::expected`

When you need closed polymorphism (not arbitrary extension, but a finite, known set of variants), `std::variant` + `std::visit` is an excellent choice: it clearly enumerates all variants at compile time, and `std::visit` generates a branch table or inlined logic at compile time. This avoids the overhead of a vtable while being more flexible than passing template parameters (allowing objects of different types to be stored in a container).

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

We need to be mindful of `std::variant`'s memory footprint in embedded systems (it allocates space for the size of the widest variant)—but it stores type information internally within the object, requiring no external vptr.

## Type Erasure

Through `std::function`, custom type-erased wrappers (usually featuring small buffer optimization), we can achieve a "near compile-time efficiency" interface without exposing template parameters, while maintaining runtime replaceability. The cost is implementation complexity and potential memory overhead (small buffer plus virtual-like calls). This approach is often used at the library or API layer to hide implementation details.

------

## Summary: There Is No Absolute "Better," Only "More Appropriate"

Compile-time polymorphism and runtime polymorphism are not opposing dogmas, but two different tools in the toolbox. The embedded engineer's task is to select and mix them based on the target platform's constraints and the engineering workflow. My recommendations are:

- Start with the clearest, most understandable implementation (usually runtime polymorphism or simple functions), and thoroughly nail down the functionality, interfaces, and tests first;
- When performance or resources become a bottleneck, identify the hot paths and perform local optimizations using compile-time polymorphism (templates/CRTP/`std::variant`);
- Enable LTO and link-time deduplication to mitigate the binary bloat caused by templates;
- Retain runtime polymorphism interfaces for cross-module, plugin-style architectures to ensure ABI stability and replaceability;
- At the design level, clearly distinguish between "points of variability" and "stable points": put invariant logic at compile time, and leave logic that requires flexible replacement to runtime.
