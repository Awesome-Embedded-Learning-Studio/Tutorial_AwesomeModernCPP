// Lambda Capture Basics
// Demonstrates how lambda captures external variables

#include <iostream>
#include <functional>
#include <cstdint>

int main() {
    std::cout << "=== Lambda Capture Basics Demo ===" << std::endl;

    // Value capture: copies the variable
    int threshold = 50;
    auto by_value = [threshold](int value) {
        return value > threshold;  // Uses the copied value
    };
    std::cout << "by_value(60) = " << by_value(60) << std::endl;
    threshold = 100;  // Modifying original doesn't affect lambda
    std::cout << "After threshold=100, by_value(60) = " << by_value(60) << std::endl;

    // Reference capture: references the original variable
    int count = 0;
    auto by_ref = [&count](int delta) {
        count += delta;  // Modifies the original
    };
    by_ref(10);
    by_ref(20);
    std::cout << "After by_ref(10) and by_ref(20), count = " << count << std::endl;

    // Capture all by value
    int x = 1, y = 2, z = 3;
    auto capture_all = [=]() {
        return x + y + z;  // All copied
    };
    std::cout << "capture_all() = " << capture_all() << std::endl;

    // Capture all by reference
    auto capture_all_ref = [&]() {
        x *= 2;
        y *= 2;
        z *= 2;
    };
    capture_all_ref();
    std::cout << "After capture_all_ref(), x=" << x << ", y=" << y << ", z=" << z << std::endl;

    // Mixed capture
    int multiplier = 2;
    int accumulator = 0;
    auto mixed = [multiplier, &accumulator](int value) {
        accumulator += value * multiplier;
    };
    mixed(5);
    mixed(10);
    std::cout << "After mixed(5) and mixed(10), accumulator = " << accumulator << std::endl;

    // Embedded example: PWM configuration with capture
    std::cout << "\n=== Embedded Example: PWM Config ===" << std::endl;

    uint32_t base_addr = 0x40000000;
    int frequency = 1000;

    auto set_duty = [base_addr, frequency](int percent) {
        uint32_t period = 1000000 / frequency;  // microseconds
        uint32_t duty = period * percent / 100;
        std::cout << "PWM: base=0x" << std::hex << base_addr << std::dec
                  << ", freq=" << frequency << "Hz, duty=" << duty << "us (" << percent << "%)" << std::endl;
    };

    set_duty(25);  // 25% duty cycle
    set_duty(50);  // 50% duty cycle
    set_duty(75);  // 75% duty cycle

    // Default capture warning
    std::cout << "\n=== Warning: Default Capture ===" << std::endl;
    int config = 100;
    int temp = 50;

    // Bad: captures everything by reference when only config is needed
    auto bad_capture = [&]() { return config > 50; };

    // Good: only capture what you need
    auto good_capture = [&config]() { return config > 50; };

    std::cout << "Use explicit capture to avoid unintended dependencies" << std::endl;

    return 0;
}
