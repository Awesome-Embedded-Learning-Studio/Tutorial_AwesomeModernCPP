// Inline and compiler optimization: demonstrating zero-overhead abstraction
#include <cstdint>
#include <iostream>

// ---- C-style direct register access ----
#define GPIO_BASE 0x40020000
#define GPIO_BSRR (*(volatile uint32_t*)(GPIO_BASE + 0x18))

void c_set_pin(uint8_t pin) {
    GPIO_BSRR = (1U << pin);
}
void c_reset_pin(uint8_t pin) {
    GPIO_BSRR = (1U << (pin + 16));
}

// ---- C++ template-based zero-overhead abstraction ----
template <uint32_t BaseAddr> class GpioPort {
    static volatile uint32_t& reg(uint32_t offset) {
        return *reinterpret_cast<volatile uint32_t*>(BaseAddr + offset);
    }

  public:
    static void set_pin(uint8_t pin) { reg(0x18) = (1U << pin); }
    static void reset_pin(uint8_t pin) { reg(0x18) = (1U << (pin + 16)); }
};

using GPIOA = GpioPort<0x40020000>;

// ---- Inline wrapper that adds value ----
inline int add_inline(int a, int b) {
    return a + b;
}

// Regular function (compiler decides)
int add_regular(int a, int b) {
    return a + b;
}

// ---- Demonstrate constexpr compile-time computation ----
constexpr uint32_t calc_baud_divisor(uint32_t cpu_freq, uint32_t baud) {
    return cpu_freq / (16u * baud);
}

constexpr uint32_t baud_div = calc_baud_divisor(72000000u, 115200u);
static_assert(baud_div == 39u);

int main() {
    // Show that C++ template GPIO compiles to same code as C
    GPIOA::set_pin(5);
    GPIOA::reset_pin(5);

    c_set_pin(5);
    c_reset_pin(5);

    // Inline vs regular: compiler will inline both at -O2
    std::cout << "inline add: " << add_inline(3, 4) << "\n";
    std::cout << "regular add: " << add_regular(3, 4) << "\n";

    // Constexpr baud rate computed at compile time
    std::cout << "Baud divisor (compile-time): " << baud_div << "\n";

    // Runtime computation for comparison
    uint32_t rt_div = calc_baud_divisor(72000000u, 115200u);
    std::cout << "Baud divisor (runtime): " << rt_div << "\n";

    std::cout << "\nKey insight: inline keyword is a hint, not a command.\n"
              << "At -O2, the compiler inlines both functions anyway.\n"
              << "The real value of inline is allowing multi-TU definitions (ODR).\n";

    return 0;
}
