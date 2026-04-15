#pragma once
#include "gpio/gpio.hpp"

namespace device {

enum class ActiveLevel { Low, High };

template <gpio::GpioPort PORT, uint16_t PIN, ActiveLevel LEVEL = ActiveLevel::Low> class LED
    : public gpio::GPIO<PORT, PIN> {
    using Base = gpio::GPIO<PORT, PIN>;

  public:
    LED() { Base::setup(Base::Mode::OutputPP, gpio::PullPush::NoPull, Base::Speed::Low); }

    void on() const {
        Base::set_gpio_pin_state(LEVEL == ActiveLevel::Low ? Base::State::UnSet : Base::State::Set);
    }

    void off() const {
        Base::set_gpio_pin_state(LEVEL == ActiveLevel::Low ? Base::State::Set : Base::State::UnSet);
    }

    void toggle() const { Base::toggle_pin_state(); }
};

} // namespace device
