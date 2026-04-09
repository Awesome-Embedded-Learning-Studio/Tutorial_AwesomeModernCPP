#pragma once

extern "C" {
// IWYU pragma: begin_keep
#include "stm32f1xx_hal.h"
// IWYU pragma: end_keep
}

#include <cstdint>

namespace device::gpio {

enum class GpioPort : uintptr_t {
    A = GPIOA_BASE,
    B = GPIOB_BASE,
    C = GPIOC_BASE,
    D = GPIOD_BASE,
    E = GPIOE_BASE,
};

enum class PullPush : uint32_t {
    NoPull = GPIO_NOPULL,
    PullUp = GPIO_PULLUP,
    PullDown = GPIO_PULLDOWN,
};

template <GpioPort PORT, uint16_t PIN> class GPIO {
  public:
    enum class Mode : uint32_t {
        Input = GPIO_MODE_INPUT,
        OutputPP = GPIO_MODE_OUTPUT_PP,
        OutputOD = GPIO_MODE_OUTPUT_OD,
        AfPP = GPIO_MODE_AF_PP,
        AfOD = GPIO_MODE_AF_OD,
        AfInput = GPIO_MODE_AF_INPUT,
        Analog = GPIO_MODE_ANALOG,
        ItRising = GPIO_MODE_IT_RISING,
        ItFalling = GPIO_MODE_IT_FALLING,
        ItRisingFalling = GPIO_MODE_IT_RISING_FALLING,
        EvtRising = GPIO_MODE_EVT_RISING,
        EvtFalling = GPIO_MODE_EVT_FALLING,
        EvtRisingFalling = GPIO_MODE_EVT_RISING_FALLING,
    };

    enum class Speed : uint32_t {
        Low = GPIO_SPEED_FREQ_LOW,
        Medium = GPIO_SPEED_FREQ_MEDIUM,
        High = GPIO_SPEED_FREQ_HIGH,
    };

    static constexpr GPIO_TypeDef* native_port() noexcept {
        return reinterpret_cast<GPIO_TypeDef*>(static_cast<uintptr_t>(PORT));
    }

    void setup(Mode gpio_mode, PullPush pull_push = PullPush::NoPull, Speed speed = Speed::High) {
        GPIOClock::enable_target_clock();
        GPIO_InitTypeDef init_types{};
        init_types.Pin = PIN;
        init_types.Mode = static_cast<uint32_t>(gpio_mode);
        init_types.Pull = static_cast<uint32_t>(pull_push);
        init_types.Speed = static_cast<uint32_t>(speed);
        HAL_GPIO_Init(native_port(), &init_types);
    }

    enum class State { Set = GPIO_PIN_SET, UnSet = GPIO_PIN_RESET };
    void set_gpio_pin_state(State s) const {
        HAL_GPIO_WritePin(native_port(), PIN, static_cast<GPIO_PinState>(s));
    }

    void toggle_pin_state() const { HAL_GPIO_TogglePin(native_port(), PIN); }

    [[nodiscard]] State read_pin_state() const {
        return static_cast<State>(HAL_GPIO_ReadPin(native_port(), PIN));
    }

  private:
    class GPIOClock {
      public:
        static inline void enable_target_clock() {
            if constexpr (PORT == GpioPort::A) {
                __HAL_RCC_GPIOA_CLK_ENABLE();
            } else if constexpr (PORT == GpioPort::B) {
                __HAL_RCC_GPIOB_CLK_ENABLE();
            } else if constexpr (PORT == GpioPort::C) {
                __HAL_RCC_GPIOC_CLK_ENABLE();
            } else if constexpr (PORT == GpioPort::D) {
                __HAL_RCC_GPIOD_CLK_ENABLE();
            } else if constexpr (PORT == GpioPort::E) {
                __HAL_RCC_GPIOE_CLK_ENABLE();
            }
        }
    };
};
}; // namespace device::gpio
