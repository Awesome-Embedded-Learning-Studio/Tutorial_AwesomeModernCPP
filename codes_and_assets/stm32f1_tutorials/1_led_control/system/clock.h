#pragma once
#include "base/simple_singleton.hpp"
#include <cstdint>

namespace clock {
class ClockConfig : public base::SimpleSingleton<ClockConfig> {
  public:
    /* Setup the System clocks */
    void setup_system_clock();

    [[nodiscard("You should accept the clock frequency, it's what you request!")]] uint64_t
    clock_freq() const noexcept;
};
} // namespace clock
