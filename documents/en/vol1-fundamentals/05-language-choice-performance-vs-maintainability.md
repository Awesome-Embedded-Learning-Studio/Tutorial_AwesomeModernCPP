# Language Selection Principles: The Real Trade-offs Between Performance and Maintainability

Allow me a brief rant—I've noticed that many developers lack rational thinking when choosing tools, often evaluating them based on labels rather than actual performance. As a result, language selection is frequently framed as a moral judgment: "C is pure, close-to-the-hardware real engineering; C++/Rust are high-level, lazy productivity tools." This rhetoric is emotionally charged but utterly useless for engineering decisions. The real engineering question is never "which language is more advanced," but rather "given this machine, these timing constraints, this team, and this product roadmap, which approach lets us build things correctly and completely at a reasonable cost?" Performance and maintainability are not inherently opposing magical forces; they introduce different risks and overheads across different dimensions. Treating a language as a master key or a silver bullet, rather than as part of an engineering toolchain, is the biggest mistake we can make.

## What Performance Actually Means in Embedded

First, we need to clarify what "performance" means in this context. Performance isn't just a raw CPU benchmark score; it encompasses flash usage, RAM usage, startup time, real-time interrupt latency, power consumption, and timing predictability. "Performance" means something entirely different when we are running an interrupt service routine (ISR) that must complete within 5µs on a Cortex-M0, compared to running a UI thread on a Raspberry Pi. Similarly, maintainability isn't an abstract notion of "pretty code." It refers to whether a team can reliably reuse, fix, and extend the codebase over the next six months to three years: are unit tests easy to write, can bugs be reproduced in CI, and can a new team member understand the module boundaries within a week?

## Trade-offs in Real-World Scenarios

Discussing performance and maintainability only makes sense within real-world scenarios. When memory and determinism are hard constraints, we need a language and style that puts all overhead explicitly in the engineer's hands. Below is a common C implementation of an interrupt handler. It lays the overhead and boundaries right in front of you—you can clearly see what each step does and how much time it takes:

```c
// IRQ handler — 必须尽快返回
void TIM2_IRQHandler(void) {
    if (TIM2->SR & TIM_SR_UIF) {
        TIM2->SR &= ~TIM_SR_UIF;
        // 尽可能少做事情：设置标志，或写环形缓冲一字节
        rx_ring_put(&uart_rx_ring, TIM2->CNT);
    }
}
```

If you try to do memory allocation, throw exceptions, or call complex virtual functions inside an interrupt, you need to be extremely careful. It's not that the language is inherently flawed, but rather that improper usage introduces unpredictable latency. This kind of latency doesn't matter in desktop applications, but in an ISR, it might mean missing a deadline.

However, modern C++ itself is not inherently at odds with high performance. Its zero-overhead abstraction is achievable, provided there is a set of constraints agreed upon by the team and enforceable by automated tools: disabling exceptions and RTTI (by passing `-fno-rtti -fno-exceptions` to the compiler), restricting heap allocation, and using `constexpr` and `std::array` instead of `new`.

A highly practical pattern is to use C++ abstractions at the high level for maintainability, while using restricted, deterministic implementations at the low level (especially in ISRs) to guarantee predictability. The following example demonstrates using RAII (Resource Acquisition Is Initialization) for resource cleanup without relying on heap allocation:

```cpp
struct ScopedIrqLock {
    ScopedIrqLock() { __disable_irq(); }
    ~ScopedIrqLock() { __enable_irq(); }
};

void append_log(const char* msg) {
    ScopedIrqLock lk;  // 在栈上完成可预测的加锁/解锁
    ring_buffer_write(&log_buf, msg, strlen(msg));
}
```

`ScopedIrqLock` disables interrupts upon construction and automatically restores them upon destruction. The entire process happens on the stack, involves no heap allocation, and is completely deterministic in terms of timing. This way, we maintain high-level maintainability while guaranteeing determinism on the critical path.

## Safety-Critical Scenarios

If a project requires strict certification and memory safety is paramount (such as in medical devices, automotive ECUs, etc.), language selection must also factor in certification costs and the price of errors. In these scenarios, a language's ability to prevent certain classes of errors at compile time is valuable in itself. Rust's borrow checker can avoid data races and dangling pointers in many situations, which directly reduces the workload for later verification and bug fixing.

However, introducing a new language also means thoroughly evaluating the toolchain, compiler support, third-party library maturity, and the team's learning curve. No language automatically handles compliance work for you—a language simply moves certain errors forward to compile time, reducing runtime risks. Just make sure to do the math when selecting a language.

## Long-Term Cost of Ownership

Finally, we need to discuss long-term maintenance costs. Debugging time, defect resolution, team handovers, and the difficulty of future extensions—these are the areas that truly consume budgets. Often, good testing, reproducible build pipelines, clear module boundaries, and thorough documentation have a far greater impact on total cost than saving a few milliseconds on some micro-benchmark. Choosing a language that allows a team to produce stable output, facilitates code reviews, and enables automated testing is often far more meaningful than fighting over that tiny bit of single-core peak performance.

## Wrapping Up

Language selection is not a black-and-white sacrifice between performance and maintainability. It is the result of weighing a project's constraints, the cost of errors, team capabilities, and long-term maintenance costs together. Use data to prove where the bottleneck lies, use different tools for different module layers, and define and enforce a controlled language subset through automated means. Our ultimate goal boils down to one thing: minimizing "predictable costs."

Just remember this: use the right tool for the right job, and don't treat tools as beliefs.
