// Compiler options: demonstrating the effect of optimization flags
// This source is designed to show differences between -O0, -Os, -O2
#include <cstdint>
#include <iostream>

// ---- Simple function to show inlining effect ----
int add_simple(int a, int b) {
    return a + b;
}

// ---- Function with side effects to prevent full optimization ----
int accumulate(int* data, size_t n) {
    int sum = 0;
    for (size_t i = 0; i < n; ++i) {
        sum += data[i];
    }
    return sum;
}

// ---- Template function: compiler can fully optimize ----
template <int N> constexpr int factorial() {
    int result = 1;
    for (int i = 2; i <= N; ++i)
        result *= i;
    return result;
}

// ---- Class with constructor: shows ctor optimization ----
struct TimerConfig {
    uint32_t period;
    uint32_t prescaler;
    bool enabled;

    constexpr TimerConfig(uint32_t p, uint32_t ps, bool e) : period(p), prescaler(ps), enabled(e) {}
};

int main() {
    // Simple operations
    int x = add_simple(3, 4);
    std::cout << "add_simple(3,4) = " << x << "\n";

    // Array accumulation
    int data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    int sum = accumulate(data, 10);
    std::cout << "accumulate = " << sum << "\n";

    // Compile-time factorial
    constexpr auto f10 = factorial<10>();
    static_assert(f10 == 3628800);
    std::cout << "factorial<10>() = " << f10 << "\n";

    // Timer config with constexpr constructor
    constexpr TimerConfig cfg(1000, 72, true);
    static_assert(cfg.period == 1000);
    std::cout << "Timer: period=" << cfg.period << " prescaler=" << cfg.prescaler
              << " enabled=" << cfg.enabled << "\n";

    std::cout << "\nTip: compare assembly with -O0 vs -Os vs -O2\n"
              << "to see how each flag affects code generation.\n";

    return 0;
}
