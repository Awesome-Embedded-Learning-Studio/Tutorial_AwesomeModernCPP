// uart_regs.hpp
// UART 寄存器定义示例

#pragma once
#include "reg.hpp"
#include <cstdint>

// UART 控制寄存器，地址 0x40001000
using uart_cr_t = mmio_reg<uint32_t, 0x40001000u>;

// 强类型枚举：MODE 的可能值
enum class uart_mode : uint32_t {
    Idle = 0,
    TxRx = 1,
    TxOnly = 2,
    Reserved = 3
};

// 字段定义
// EN: 位 0（使能）
using uart_en      = reg_field<uart_cr_t, 0, 1>;
// MODE: 位 1~2（2 bit 模式）
using uart_mode_f  = reg_field<uart_cr_t, 1, 2>;
// BAUDDIV: 位 8~15（8 bit 波特率分频器）
using uart_baud    = reg_field<uart_cr_t, 8, 8>;
