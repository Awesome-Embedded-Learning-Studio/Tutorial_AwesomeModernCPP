// Constructor optimization: initializer list vs assignment
#include <cstdint>
#include <iostream>

// ---- BAD: assignment in constructor body ----
class TimerBad {
  public:
    TimerBad(uint32_t period) {
        period_ = period; // default-init THEN assign
        enabled_ = false;
    }
    uint32_t period_;
    bool enabled_;
};

// ---- GOOD: initializer list ----
class TimerGood {
  public:
    TimerGood(uint32_t period)
        : period_(period) // direct initialization
          ,
          enabled_(false) {}
    uint32_t period_;
    bool enabled_;
};

// ---- Const member: ONLY works with init list ----
class Device {
  public:
    Device(uint32_t id) : id_(id) {}
    const uint32_t id_;
};

// ---- Reference member: ONLY works with init list ----
struct GPIO {};
class Driver {
  public:
    Driver(GPIO& gpio) : gpio_(gpio) {}
    GPIO& gpio_;
};

// ---- No default constructor: MUST use init list ----
class SpiBus {
  public:
    explicit SpiBus(uint32_t base_addr) : base_addr_(base_addr) {}
    uint32_t base_addr_;
};

class Sensor {
  public:
    Sensor() : spi_(SPI1_BASE) {}

  private:
    static constexpr uint32_t SPI1_BASE = 0x40013000;
    SpiBus spi_;
};

int main() {
    TimerBad tb(1000);
    TimerGood tg(1000);

    std::cout << "Bad timer:  period=" << tb.period_ << " enabled=" << tb.enabled_ << '\n';
    std::cout << "Good timer: period=" << tg.period_ << " enabled=" << tg.enabled_ << '\n';

    // sizeof should be identical
    static_assert(sizeof(TimerBad) == sizeof(TimerGood));
    std::cout << "sizeof(Timer) = " << sizeof(TimerGood) << '\n';

    Device dev(42);
    std::cout << "Device id = " << dev.id_ << '\n';

    GPIO gpio;
    Driver drv(gpio);
    std::cout << "Driver holds GPIO reference at " << static_cast<void*>(&drv.gpio_) << '\n';

    Sensor s;
    std::cout << "Sensor constructed with SpiBus\n";

    return 0;
}
