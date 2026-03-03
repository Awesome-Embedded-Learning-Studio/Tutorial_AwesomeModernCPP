// Lambda Full Capture
// Demonstrates full capture with [=] and [&] default capture modes

#include <iostream>
#include <vector>
#include <string>

int main() {
    std::cout << "=== Full Capture Demo ===" << std::endl;

    // [=] - Capture all by value (copy)
    int a = 1, b = 2, c = 3, d = 4;

    auto lambda1 = [=]() {
        // a, b, c, d are all copied
        return a + b + c + d;
    };

    std::cout << "lambda1() = " << lambda1() << std::endl;  // 10

    a = 10;
    std::cout << "After a=10, lambda1() = " << lambda1() << std::endl;  // Still 10

    // [&] - Capture all by reference
    int x = 1, y = 2, z = 3;

    auto lambda2 = [&]() {
        x++;
        y++;
        z++;
        return x + y + z;
    };

    std::cout << "lambda2() = " << lambda2() << std::endl;  // 9 (2+3+4)
    std::cout << "x=" << x << ", y=" << y << ", z=" << z << std::endl;  // All modified

    // Mixed: Default capture with exceptions
    int threshold = 100;
    int count = 0;
    double factor = 1.5;

    // [=] by default, but capture 'count' by reference
    auto process1 = [=, &count](int value) {
        if (value > threshold) {  // threshold is copied
            count++;              // count is referenced
            return static_cast<int>(value * factor);  // factor is copied
        }
        return value;
    };

    int result1 = process1(150);
    std::cout << "process1(150) = " << result1 << ", count = " << count << std::endl;

    // [&] by default, but capture 'threshold' and 'factor' by value
    auto process2 = [&, threshold, factor](int value) {
        if (value > threshold) {  // threshold is copied
            count++;              // count is referenced (default)
            return static_cast<int>(value * factor);  // factor is copied
        }
        return value;
    };

    int result2 = process2(200);
    std::cout << "process2(200) = " << result2 << ", count = " << count << std::endl;

    // Avoid default capture warning
    std::cout << "\n=== Capture Best Practices ===" << std::endl;

    int config = 100;
    int temp = 50;

    // NOT RECOMMENDED: [&] captures everything, even unused variables
    auto bad1 = [&]() {
        return config > 50;
    };
    // temp is also captured even though we don't use it!

    // RECOMMENDED: Explicitly list what you need
    auto good1 = [&config]() {
        return config > 50;
    };

    // Or if it's just a value, capture by value
    auto good2 = [config]() {
        return config > 50;
    };

    // Example with multiple variables
    int base = 10;
    int offset = 5;
    int multiplier = 2;

    // Clear intention: capture these specific values
    auto calc = [base, offset, multiplier](int input) {
        return (input + offset) * multiplier + base;
    };

    std::cout << "calc(3) = " << calc(3) << std::endl;  // (3+5)*2+10 = 26

    // Embedded scenario: Device configuration
    std::cout << "\n=== Embedded: Device Config ===" << std::endl;

    uint32_t base_addr = 0x40000000;
    int frequency = 1000;
    int duty_cycle = 50;

    // Capture all needed config by value for thread safety
    auto configure_pwm = [=]() {
        std::cout << "Configuring PWM @ 0x" << std::hex << base_addr << std::dec
                  << " freq=" << frequency << "Hz duty=" << duty_cycle << "%" << std::endl;
        // Configuration is frozen at lambda creation time
    };

    configure_pwm();

    frequency = 2000;
    duty_cycle = 75;
    std::cout << "Changed frequency to " << frequency << "Hz and duty to " << duty_cycle << "%" << std::endl;
    configure_pwm();  // Still uses original values (1000Hz, 50%)

    return 0;
}
