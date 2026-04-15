#include "device/button.hpp"
#include "device/button_event.hpp"
#include "device/led.hpp"
#include "system/clock.h"
extern "C" {
#include "stm32f1xx_hal.h"
}

int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();

    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
    device::Button<device::gpio::GpioPort::A, GPIO_PIN_0> button;

    while (1) {
        button.poll_events(
            [&](device::ButtonEvent event) {
                std::visit(
                    [&](auto&& e) {
                        using T = std::decay_t<decltype(e)>;
                        if constexpr (std::is_same_v<T, device::Pressed>) {
                            led.on();
                        } else {
                            led.off();
                        }
                    },
                    event);
            },
            HAL_GetTick());
    }
}
