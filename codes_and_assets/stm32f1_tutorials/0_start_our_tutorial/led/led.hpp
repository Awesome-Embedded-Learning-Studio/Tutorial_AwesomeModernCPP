#pragma once

#include "stm32f1xx_hal.h"
#include <type_traits>

namespace hal {

// ====================================================================
// LED模板类 - 编译期端口和引脚配置
// 使用地址值作为非类型模板参数（constexpr兼容）
// ====================================================================
template <uintptr_t PortAddr, uint16_t PinNum> class Led {
  public:
    // 编译期将地址转换为指针
    static constexpr GPIO_TypeDef* port() { return reinterpret_cast<GPIO_TypeDef*>(PortAddr); }

    static constexpr uint16_t pin = PinNum;

    // 初始化GPIO
    void init() const {
        enable_clock();
        GPIO_InitTypeDef cfg{};
        cfg.Pin = pin;
        cfg.Mode = GPIO_MODE_OUTPUT_PP;
        cfg.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(port(), &cfg);
        off();
    }

    // LED开（低电平点亮）
    void on() const { HAL_GPIO_WritePin(port(), pin, GPIO_PIN_RESET); }

    // LED关（高电平熄灭）
    void off() const { HAL_GPIO_WritePin(port(), pin, GPIO_PIN_SET); }

    // 翻转状态
    void toggle() const { HAL_GPIO_TogglePin(port(), pin); }

    // 闪烁指定次数
    void blink_times(uint32_t count, uint32_t ms = 500) const {
        for (uint32_t i = 0; i < count; ++i) {
            toggle();
            HAL_Delay(ms);
        }
    }

  private:
    static void enable_clock() {
        if constexpr (PortAddr == GPIOA_BASE) {
            __HAL_RCC_GPIOA_CLK_ENABLE();
        } else if constexpr (PortAddr == GPIOB_BASE) {
            __HAL_RCC_GPIOB_CLK_ENABLE();
        } else if constexpr (PortAddr == GPIOC_BASE) {
            __HAL_RCC_GPIOC_CLK_ENABLE();
        } else if constexpr (PortAddr == GPIOD_BASE) {
            __HAL_RCC_GPIOD_CLK_ENABLE();
        } else if constexpr (PortAddr == GPIOE_BASE) {
            __HAL_RCC_GPIOE_CLK_ENABLE();
        }
    }
};

} // namespace hal
