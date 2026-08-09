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
reading_time_minutes: 7
tags:
- cpp-modern
- intermediate
- stm32f1
title: CRTP vs Runtime Polymorphism
translation:
  source: documents/vol8-domains/embedded/04-crtp-vs-runtime-polymorphism.md
  source_hash: 592c34a15b79332d8944a2045734500997409534cf3fa736acc2b62f4f8c8385
  translated_at: '2026-06-24T01:11:29.055770+00:00'
  engine: anthropic
  token_count: 1038
---
# Compile-Time Polymorphism vs. Runtime Polymorphism

In engineering practice, when we mention "polymorphism," the first reaction is often `virtual` functions and interfaces—that is, runtime polymorphism.

However, modern C++ provides us with an equally powerful set of tools: templates, CRTP, `std::variant`, type erasure, and others. These constitute the world of **compile-time polymorphism**. While the two approaches may seem to differ only in "when the behavior is determined," they actually involve trade-offs across multiple dimensions: performance, flash and RAM usage, testability, ABI stability, compilation time, and debugging experience. For embedded systems, these trade-offs are often not academic; they are real engineering constraints.

## Unifying Concepts

The form of polymorphism native to C++ is **runtime polymorphism (dynamic polymorphism)**. This most common form typically refers to calling virtual functions through base class pointers or references: the base class contains `virtual` functions, derived classes override them, and at runtime, the actual implementation is executed by indexing the vtable based on the object's actual type. The key point is that the call site only knows about the base class at compile time; the actual binding happens at runtime. Its implementation relies on a vtable (for each class with virtual functions) + a vptr in the object (a pointer to the vtable).

As you can see, runtime polymorphism involves function forwarding.

**Compile-time polymorphism** (static polymorphism), on the other hand, uses templates, overloading, `constexpr`, CRTP (Curiously Recurring Template Pattern), and algebraic data types (`std::variant`/`std::visit`) to dispatch, inline, and optimize different implementations during the compilation phase. Function calls are determined and expanded at compile time into direct calls or inlined code, thereby eliminating the cost of runtime indirection.

From an implementation perspective, runtime polymorphism generates one or more vtables and requires every object to carry a vptr (consuming RAM). Every virtual function call is an indirect jump (which can affect branch prediction). Compile-time polymorphism, however, usually generates multiple concrete function instances (via template instantiation). These can be inlined and optimized, making the call overhead similar to a normal function call, or even achieving zero-overhead abstraction.

------

## Typical Code Comparison: Device Driver Interface

Imagine a simple scenario: abstracting a `Sensor` with a read operation. Let's first look at the runtime polymorphism version:

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

Let's look at the compile-time polymorphism (template) version again:

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

The difference is immediate: the template version allows `read()` to be inlined at `poll<ADCSensor>`, eliminating the indirect call; the runtime polymorphism version retains the vtable/indirect jump and the object's vptr in the binary.

<OnlineCompilerDemo
  title="Compile-time Polymorphism: Inlining Opportunities in Template Poll"
  source-path="code/examples/chapter02/04_crtp_polymorphism/compile_time_polymorphism.cpp"
  arm-source-path="code/examples/compiler_explorer/static_polymorphism_arm.cpp"
  description="This example is runnable; when viewing the assembly, observe the optimization space the template version offers on concrete Sensor types."
  allow-run
  allow-x86-asm
  allow-arm-asm
/>

------

## Performance and Space (Two Major Resources in Embedded Systems)

### Execution Speed

Compile-time polymorphism excels at "zero-overhead abstraction"—hot spots in electronic systems (such as driver calls in an ISR or real-time paths) are excellent candidates for templates to facilitate inlining and optimization. Runtime polymorphism incurs an extra memory read (reading the vptr pointing to the vtable) and an indirect jump on every call. Furthermore, such jump targets are unfriendly to branch prediction, and the resulting latency is significant in real-time scenarios.

### RAM and Flash

Runtime polymorphism: Each object typically carries a pointer to the vtable (vptr), which consumes the object's RAM (usually the size of one pointer). The vtable itself resides in read-only memory (Flash), but the object's vptr consumes considerable RAM, especially when there are many objects. On the other hand, runtime polymorphism allows multiple objects to share function implementations via a single vtable, resulting in smaller Flash usage (only one copy of the function body is generated).

Compile-time polymorphism: Template instantiation generates code (function/class instances) for each distinct template argument, which can lead to binary growth (code bloat), increasing Flash usage. However, the objects themselves do not need to retain a vptr (saving RAM). On embedded devices where Flash space is sufficient but RAM is constrained, this is often a worthwhile trade: trading runtime overhead and RAM usage for increased Flash consumption.

### Startup Time and Predictability

Static initialization resulting from template instantiation can be very explicit, without the risks of dynamic construction (unless complex global objects are used). The vtable mechanism may indirectly rely on static construction/dynamic initialization order (especially when combined with non-`constexpr` static objects), complicating the startup process. In systems requiring highly predictable startup behavior, compile-time polymorphism is easier to reason about and verify.

## CRTP (A Form of Static Polymorphism)

CRTP enforces the interface of concrete implementations at compile time and allows the base class to reuse code while calling the derived class's implementation:

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

The advantages of CRTP include static dispatch combined with code reuse, making it suitable for driver frameworks, state machine implementations, and more.

## `std::variant` / `std::visit`

When we need closed polymorphism (limited to a finite set of known variants rather than arbitrary extension), `std::variant` + `std::visit` is an excellent choice. It enumerates all variants explicitly at compile time, and `visit` generates a branch table or inlined logic during compilation. This avoids the overhead of a vtable while offering more flexibility than template parameter passing, as it allows storing objects of different types within a container.

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

`std::variant` requires attention regarding memory consumption in embedded systems (it allocates memory for the size of the widest variant)—but it stores type information internally, eliminating the need for an external vptr.

## Type Erasure

Through `std::function` or custom type-erased wrappers (usually featuring small-buffer optimization), we can achieve "near compile-time efficiency" interfaces without exposing template parameters, while maintaining runtime substitutability. The trade-offs are implementation complexity and potential memory overhead (small buffer + virtual-like calls). This approach is often used in library layers or API layers to hide implementation details.

------

## Summary: There is no absolute "better," only "more suitable"

Compile-time polymorphism and runtime polymorphism are not opposing theological doctrines; they are simply two tools in the toolbox. The task of an embedded engineer is to select and mix them based on the constraints of the target platform and the engineering workflow. My suggestions are:

- Start with the clearest, most understandable implementation (usually runtime polymorphism or simple functions) to thoroughly develop functionality, interfaces, and tests.
- When performance or resources become a bottleneck, identify hotspots and apply compile-time polymorphism (templates/CRTP/`constexpr`) for localized optimization.
- Enable LTO (Link Time Optimization) and link-level deduplication to mitigate binary bloat caused by templates.
- Retain runtime polymorphism interfaces for cross-module or plugin architectures to ensure ABI stability and replaceability.
- At the design level, clearly distinguish between "variation points" and "stable points": push invariant logic to compile-time, and leave logic requiring flexible replacement to runtime.
