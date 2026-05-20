// ARM freestanding: compiler options effect demonstration
// Uses only freestanding headers for ARM cross-compilation
// Compare assembly with: -O0, -Os, -O2, -O3
#include <cstddef>
#include <cstdint>

// ---- Simple function: inlining candidate ----
int add_simple(int a, int b) {
    return a + b;
}

// ---- Loop: shows loop optimization ----
int accumulate(const int* data, size_t n) {
    int sum = 0;
    for (size_t i = 0; i < n; ++i) {
        sum += data[i];
    }
    return sum;
}

// ---- Compile-time computation: should disappear at any -O ----
template <int N> constexpr int factorial() {
    int result = 1;
    for (int i = 2; i <= N; ++i)
        result *= i;
    return result;
}

constexpr int f10 = factorial<10>();
static_assert(f10 == 3628800);

// ---- Struct with ctor: shows ctor optimization ----
struct TimerConfig {
    uint32_t period;
    uint32_t prescaler;
    uint8_t enabled;

    constexpr TimerConfig(uint32_t p, uint32_t ps, bool e)
        : period(p), prescaler(ps), enabled(e ? 1u : 0u) {}
};

constexpr TimerConfig default_cfg{1000, 72, true};
static_assert(default_cfg.period == 1000);

// ---- Callable wrappers to observe code generation ----
int call_add() {
    return add_simple(3, 4);
}

int call_accumulate() {
    const int data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    return accumulate(data, 10);
}

int call_factorial() {
    return f10;
}

uint32_t call_config() {
    return default_cfg.period + default_cfg.prescaler;
}
