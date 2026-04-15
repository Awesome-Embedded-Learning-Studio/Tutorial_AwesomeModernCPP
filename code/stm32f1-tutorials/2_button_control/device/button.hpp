#pragma once

#include "button_event.hpp"
#include "gpio/gpio.hpp"

#include <concepts>
#include <cstdint>

namespace device {

enum class ButtonActiveLevel { Low, High };

template <gpio::GpioPort PORT, uint16_t PIN, gpio::PullPush PULL = gpio::PullPush::PullUp,
          ButtonActiveLevel LEVEL = ButtonActiveLevel::Low>
class Button : public gpio::GPIO<PORT, PIN> {
    using Base = gpio::GPIO<PORT, PIN>;

    static_assert(PIN <= GPIO_PIN_15, "Pin number must be <= 15");

  public:
    Button() {
        Base::setup(Base::Mode::Input, PULL, Base::Speed::Low);
    }

    bool is_pressed() const {
        auto state = Base::read_pin_state();
        if constexpr (LEVEL == ButtonActiveLevel::Low) {
            return state == Base::State::UnSet;
        } else {
            return state == Base::State::Set;
        }
    }

    template <typename Callback>
        requires std::invocable<Callback, ButtonEvent>
    void poll_events(Callback&& cb, uint32_t now_ms, uint32_t debounce_ms = 20) {
        const bool sample = is_pressed();

        switch (state_) {
        case State::BootSync:
            raw_pressed_ = sample;
            stable_pressed_ = sample;
            debounce_start_ = now_ms;

            // If already pressed at startup, enter "boot lock":
            // no Pressed/Released events until released
            boot_locked_ = sample;
            state_ = sample ? State::BootPressed : State::Idle;
            return;

        case State::Idle:
            if (sample) {
                raw_pressed_ = true;
                debounce_start_ = now_ms;
                state_ = State::DebouncingPress;
            }
            return;

        case State::DebouncingPress:
            if (sample != raw_pressed_) {
                raw_pressed_ = sample;
                debounce_start_ = now_ms;
            }

            if (!sample) {
                state_ = State::Idle;
                return;
            }

            if ((now_ms - debounce_start_) < debounce_ms) {
                return;
            }

            // Confirmed press
            stable_pressed_ = true;
            state_ = State::Pressed;
            cb(Pressed{});
            return;

        case State::Pressed:
            if (sample != raw_pressed_) {
                raw_pressed_ = sample;
                debounce_start_ = now_ms;
                state_ = State::DebouncingRelease;
            }
            return;

        case State::DebouncingRelease: {
            if (sample != raw_pressed_) {
                raw_pressed_ = sample;
                debounce_start_ = now_ms;

                if (sample) {
                    state_ = State::Pressed;
                }
                return;
            }

            if (sample) {
                state_ = State::Pressed;
                return;
            }

            if ((now_ms - debounce_start_) < debounce_ms) {
                return;
            }

            // Confirmed release
            stable_pressed_ = false;
            state_ = State::Idle;

            if (boot_locked_) {
                boot_locked_ = false;
                return;
            }

            cb(Released{});
            return;
        }

        case State::BootPressed:
            if (sample != raw_pressed_) {
                raw_pressed_ = sample;
                debounce_start_ = now_ms;
                state_ = State::BootReleaseDebouncing;
            }
            return;

        case State::BootReleaseDebouncing:
            if (sample != raw_pressed_) {
                raw_pressed_ = sample;
                debounce_start_ = now_ms;

                if (sample) {
                    state_ = State::BootPressed;
                }
                return;
            }

            if (sample) {
                state_ = State::BootPressed;
                return;
            }

            if ((now_ms - debounce_start_) < debounce_ms) {
                return;
            }

            // Unlock only, no events
            boot_locked_ = false;
            stable_pressed_ = false;
            state_ = State::Idle;
            return;
        }
    }

  private:
    enum class State {
        BootSync,
        Idle,
        DebouncingPress,
        Pressed,
        DebouncingRelease,
        BootPressed,
        BootReleaseDebouncing,
    };

    State state_ = State::BootSync;

    bool raw_pressed_ = false;
    bool stable_pressed_ = false;
    bool boot_locked_ = false;
    uint32_t debounce_start_ = 0;
};

} // namespace device
