#include <cstdint>

#define GPIO_PORT_A_C ((volatile std::uint32_t*)0x40020000u)
#define PIN_5_C (1u << 5)

void set_pin_c() {
    *GPIO_PORT_A_C |= PIN_5_C;
}

template <std::uint32_t Address> class GPIO_Port {
    static volatile std::uint32_t& reg() {
        return *reinterpret_cast<volatile std::uint32_t*>(Address);
    }

  public:
    static void set_pin(std::uint8_t pin) { reg() |= (1u << pin); }
};

using GPIOA = GPIO_Port<0x40020000u>;

void set_pin_cpp() {
    GPIOA::set_pin(5);
}
