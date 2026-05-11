---
chapter: 8
cpp_standard:
- 11
- 14
- 17
- 20
description: Type-safe register wrapper
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 7: 容器与数据结构'
reading_time_minutes: 7
tags:
- cpp-modern
- host
- intermediate
title: Type-safe register access
---
# Embedded C++ Tutorial — Type-Safe Register Access

When writing register operations, our common appetizer is a one-line tragedy like this:

```cpp
*(volatile uint32_t*)0x40001000 |= (1 << 3);

```

Its advantage is being short and concise; the downside is that you won't understand it tomorrow, the compiler understands it but isn't fully satisfied, and you might also step on a landmine of undefined behavior (UB).

We use **compile-time constants + templates + strongly-typed enums** to encapsulate register addresses, bit fields, and operations; meanwhile, we use **constexpr mask / static_assert** to catch errors at compile time. We must preserve `volatile` (telling the compiler not to optimize away hardware accesses) and use memory barriers when necessary to guarantee visibility and ordering.

------

## A Concise Type-Safe Register Wrapper

Below is a small yet complete implementation template that can both read and write registers, safely read and write fields, and support user-defined strongly-typed enum types.

```cpp
// reg.hpp
#pragma once
#include <cstdint>
#include <type_traits>

template<typename RegT, std::uintptr_t addr>
struct mmio_reg {
    static_assert(std::is_integral_v<RegT>, "RegT must be integral");
    using value_type = RegT;
    static constexpr std::uintptr_t address = addr;

    // 直接读取
    static inline RegT read() noexcept {
        volatile RegT* p = reinterpret_cast<volatile RegT*>(address);
        RegT v = *p;
        compiler_barrier();
        return v;
    }

    // 直接写入
    static inline void write(RegT v) noexcept {
        volatile RegT* p = reinterpret_cast<volatile RegT*>(address);
        *p = v;
        compiler_barrier();
    }

    // 按位设置（OR）
    static inline void set_bits(RegT mask) noexcept {
        write(read() | mask);
    }

    // 按位清除（AND ~mask）
    static inline void clear_bits(RegT mask) noexcept {
        write(read() & ~mask);
    }

    // 通用修改器：读取 -> 修改 -> 写回，lambda 接受并返回 RegT
    template<typename F>
    static inline void modify(F f) noexcept {
        RegT val = read();
        val = f(val);
        write(val);
    }

private:
    static inline void compiler_barrier() noexcept {
        // 强制编译器不重排序访问（实现可按目标平台替换为更强的指令）
        asm volatile ("" ::: "memory");
    }
};

// 字段访问（Offset: 起始位，Width: 位宽）
template<typename Reg, unsigned Offset, unsigned Width>
struct reg_field {
    static_assert(Width > 0 && Width <= (8 * sizeof(typename Reg::value_type)), "bad width");
    using reg_t = Reg;
    using value_type = typename Reg::value_type;
    static constexpr unsigned offset = Offset;
    static constexpr unsigned width  = Width;
    static constexpr value_type mask = ((static_cast<value_type>(1) << width) - 1) << offset;

    // 取值（未右移）
    static inline value_type read_raw() noexcept {
        return (reg_t::read() & mask) >> offset;
    }

    // 写入原始值（value 必须在域范围内）
    static inline void write_raw(value_type value) noexcept {
        value = (value << offset) & mask;
        reg_t::modify([&](value_type v){ return (v & ~mask) | value; });
    }

    // 强类型枚举友好版：若传入枚举则会静态检查与转换
    template<typename E>
    static inline void write(E e) noexcept {
        static_assert(std::is_enum_v<E>, "E must be enum");
        write_raw(static_cast<value_type>(e));
    }

    template<typename E = value_type>
    static inline E read_as() noexcept {
        return static_cast<E>(read_raw());
    }
};

```

> Note: The `compiler_barrier()` in `mmio_reg` above uses `asm volatile("" ::: "memory")`, which is the lightest compiler barrier; on ARM Cortex-M, if you need to ensure bus ordering or cache coherency, you should use `__DSB()` / `__ISB()` or equivalent functions provided by the platform SDK at critical locations.

------

## Usage Example

Suppose we have a 32-bit UART control register `UART_CR` at address `0x40001000`, defined as:

- `EN` bit 0 (enable),
- `MODE` bits 1–2 (2-bit mode),
- `BAUDDIV` bits 8–15 (8-bit baud rate divider).

```cpp
// uart_regs.hpp
#include "reg.hpp"

using uart_cr_t = mmio_reg<uint32_t, 0x40001000u>;

// 强类型枚举：MODE 的可能值
enum class uart_mode : uint32_t {
    Idle = 0,
    TxRx = 1,
    TxOnly = 2,
    Reserved = 3
};

// 字段定义
using uart_en      = reg_field<uart_cr_t, 0, 1>;
using uart_mode_f  = reg_field<uart_cr_t, 1, 2>;
using uart_baud    = reg_field<uart_cr_t, 8, 8>;

// 使用
void uart_init() {
    // 设波特率分频
    uart_baud::write_raw(16);            // 直接写数值
    // 设置模式
    uart_mode_f::write(uart_mode::TxRx); // 强类型枚举
    // 使能 UART
    uart_en::write_raw(1);
}

```

The advantages are immediately visible: field positions, widths, and valid values are all encoded in the type system, making the code read like documentation rather than magical bit manipulation.

------

## Preventing Common Errors

1. **Ensure consistent type widths**: The `uint32_t` of `mmio_reg<uint32_t, ...>` must match the actual width of the hardware register; `static_assert` can help you catch errors at compile time.
2. **Avoid raw `|=`/`&=` on the same register, which can cause read-modify-write timing issues**: If a register is specifically designed as "write-1-to-clear" or "write-1-to-set", use explicitly wrapped `set_bits()` / `clear_bits()` or dedicated functions to prevent misuse.
3. **Consider concurrency and interrupts**: Read-modify-write operations may not be atomic in interrupt or multi-core environments. For register modifications that must be atomic, disable interrupts in a critical section or use hardware-provided atomic accesses.
4. **Memory barriers**: After initializing a peripheral or swapping control registers, if you need to guarantee that subsequent reads/writes take effect on the hardware immediately, please use appropriate DSB/ISB or `atomic_thread_fence`.
5. **Don't pass registers around like global variables**: Try to keep register wrappers as `constexpr` types/aliases to facilitate static auditing and automatic documentation generation.
