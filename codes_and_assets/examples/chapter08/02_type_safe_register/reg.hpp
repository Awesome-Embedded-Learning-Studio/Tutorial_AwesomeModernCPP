// reg.hpp
// 类型安全的寄存器访问封装

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
        // 强制编译器不重排序访问
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

    // 取值
    static inline value_type read_raw() noexcept {
        return (reg_t::read() & mask) >> offset;
    }

    // 写入原始值
    static inline void write_raw(value_type value) noexcept {
        value = (value << offset) & mask;
        reg_t::modify([&](value_type v){ return (v & ~mask) | value; });
    }

    // 强类型枚举友好版
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
