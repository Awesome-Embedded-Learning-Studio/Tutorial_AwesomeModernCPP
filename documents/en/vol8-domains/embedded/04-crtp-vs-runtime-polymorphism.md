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
  source_hash: 1d669035328035992e6162a4b7dd911eee37951daa9f50f1c356f258c5f18586
  translated_at: '2026-06-16T04:12:33.016105+00:00'
  engine: anthropic
  token_count: 1038
---
# Compile-Time Polymorphism vs. Run-Time Polymorphism

In engineering practice, when we speak of "polymorphism," the immediate reaction is often virtual functions and interfaces—that is, run-time polymorphism.

But modern C++ offers us another equally powerful set of tools: templates, CRTP, concepts, type erasure, and more. These constitute the world of **compile-time polymorphism**. While the two seem to differ only in "when behavior is determined," they actually involve trade-offs across performance, Flash and RAM usage, testability, ABI stability, compile time, and debugging experience. For embedded systems, these trade-offs are often not academic but real engineering constraints.

## Aligning on Concepts

The most native form of polymorphism supported by C++ is **run-time polymorphism (dynamic polymorphism)**. This most common form usually refers to calling virtual functions via base class pointers or references: the base class contains virtual functions, derived classes override them, and at run-time, the actual type of the object indexes the vtable to execute the corresponding implementation. The key point is that the call site only knows about the base class at compile time; the actual binding happens at run-time. Its implementation relies on a vtable (for each class with virtual functions) + a vptr in the object (a pointer to the vtable).

Thus, we can see that run-time polymorphism involves function forwarding.

**Compile-time polymorphism (static polymorphism)**, on the other hand, uses templates, overloading, concepts, CRTP (Curiously Recurring Template Pattern), and algebraic data types (`std::variant`/`std::optional`) to dispatch, inline, and optimize away different implementations during the compilation phase. Function calls are determined at compile time and expanded into direct calls or inlined, thereby eliminating the cost of run-time indirection.

From an implementation perspective, run-time polymorphism generates one or more vtables, and each object carries a vptr (consuming RAM). Every virtual function call is an indirect jump (potentially affecting branch prediction). Compile-time polymorphism, conversely, usually generates multiple concrete function instances (via template instantiation), which can be inlined and optimized, making the call overhead close to that of a normal function call, or even achieving zero-overhead abstraction.

------

## Typical Code Comparison: Device Driver Interface

Imagine a simple scenario: abstracting a sensor driver with a read operation. First, let's look at the run-time polymorphism version:

```cpp
// Runtime Polymorphism: Virtual Functions
class Sensor {
public:
    virtual ~Sensor() = default;
    virtual int read() = 0;
};

class TempSensor : public Sensor {
public:
    int read() override {
        // Read temperature register...
        return 25;
    }
};

void poll(Sensor& s) {
    int val = s.read(); // Indirect call via vtable
    // Use value...
}
```

Now, let's look at the compile-time polymorphism (template) version:

```cpp
// Compile-Time Polymorphism: Templates
template <typename T>
void poll(T& sensor) {
    int val = sensor.read(); // Direct call, likely inlined
    // Use value...
}
```

The difference is immediate: the template version at the call site can inline `sensor.read()`, eliminating the indirect call. The run-time polymorphism version, however, retains the vtable/indirect jump and the object's vptr in the binary.

<OnlineCompilerDemo
  title="Compile-Time Polymorphism: Inlining Opportunities for Template poll"
  source-path="code/examples/chapter02/04_crtp_polymorphism/compile_time_polymorphism.cpp"
  arm-source-path="code/examples/compiler_explorer/static_polymorphism_arm.cpp"
  description="This example is runnable; when viewing assembly, observe the optimization space for the template version on concrete Sensor types."
  allow-run
  allow-x86-asm
  allow-arm-asm
/>

------

## Performance and Space (Two Major Resources in Embedded)

### Execution Speed

Compile-time polymorphism wins on "zero run-time overhead abstraction"—hot paths in electronic systems (e.g., driver calls in ISRs, real-time paths) are extremely well-suited for templating to facilitate inlining and optimization. Run-time polymorphism adds a memory read (reading the vptr to the vtable) and an indirect jump with every call. Furthermore, such jump targets are unfriendly to branch prediction, and the resulting latency is non-negligible in real-time scenarios.

### RAM and Flash

**Run-time polymorphism:** Each object typically carries a pointer to the vtable (vptr), which consumes RAM (usually the size of a pointer). The vtable itself resides in read-only memory (Flash), but the object's vptr consumes noticeable RAM, especially when there are many objects. On the other hand, run-time polymorphism allows multiple objects to share function implementations via a single vtable, resulting in smaller Flash usage (the function body is generated only once).

**Compile-time polymorphism:** Template instantiation generates code (function/class instances) for each distinct template argument, which can lead to binary growth (code bloat), increasing Flash usage. However, the objects themselves do not need to retain a vptr (saving RAM). On embedded devices where Flash is plentiful but RAM is tight, this is often a worthwhile trade: swapping run-time overhead and RAM usage for increased Flash consumption.

### Startup Time and Predictability

Static initialization resulting from template instantiation can be very explicit, without the risks of dynamic construction (unless complex global objects are used). The vtable mechanism may indirectly rely on static construction/dynamic initialization order (especially when combined with non-`constexpr` static objects), complicating the startup process. In systems requiring extremely predictable startup behavior, compile-time polymorphism is easier to reason about and verify.

## CRTP (A Form of Static Polymorphism)

CRTP enforces interface checking of concrete implementations at compile time and allows code reuse in the base class while calling derived class implementations:

```cpp
template <typename Derived>
class SensorInterface {
public:
    void read() {
        // Common logic (e.g., mutex lock)
        static_cast<Derived*>(this)->read_impl();
        // Common logic (e.g., mutex unlock)
    }
};

class TempSensor : public SensorInterface<TempSensor> {
public:
    void read_impl() {
        // Actual implementation
    }
};
```

The advantage of CRTP is that it combines static dispatch with code reuse, often used in driver frameworks and state machine implementations.

## `std::variant` / `std::optional`

When you need closed polymorphism (not arbitrary extension, but a finite set of known variants), `std::variant` + `std::visit` is an excellent choice. It enumerates all variants clearly at compile time, and `std::visit` generates a jump table or inlined logic at compile time. This avoids the overhead of vtables while being more flexible than template parameter passing (it allows storing different types of objects in a container).

```cpp
using SensorVariant = std::variant<TempSensor, PressureSensor>;

void poll(const SensorVariant& s) {
    std::visit([](auto&& sensor) {
        int val = sensor.read(); // Static dispatch inside visit
    }, s);
}
```

Note that `std::variant` in embedded contexts requires attention to its memory footprint (it allocates the size of the widest variant)—but it stores type information internally within the object, requiring no external vptr.

## Type Erasure

Through `std::function` or custom-written type-erased wrappers (usually with small-buffer optimization), we can gain "near compile-time efficiency" interfaces without exposing template parameters, while maintaining run-time replaceability. The cost is implementation complexity and potential memory overhead (small buffer + virtual-like calls). This approach is often used at the library or API layer to hide implementation details.

------

## Summary: No Absolute "Better," Only "More Suitable"

Compile-time and run-time polymorphism are not opposing theological propositions; they are two different knives in the toolbox. The embedded engineer's task is to select and mix them based on the target platform's constraints and the engineering workflow. My suggestions are:

- Start with the clearest, most understandable implementation (usually run-time polymorphism or simple functions) to thoroughly flesh out functionality, interfaces, and tests.
- When performance or resources become a bottleneck, identify hot spots and perform local optimization using compile-time polymorphism (templates/CRTP/concepts).
- Enable LTO (Link Time Optimization) and link-level deduplication to mitigate binary膨胀 caused by templates.
- Retain run-time polymorphism interfaces for cross-module or plug-in architectures to ensure ABI stability and replaceability.
- At the design level, clearly distinguish "variation points" from "stable points": push invariant logic to compile time and leave logic requiring flexible replacement for run time.
