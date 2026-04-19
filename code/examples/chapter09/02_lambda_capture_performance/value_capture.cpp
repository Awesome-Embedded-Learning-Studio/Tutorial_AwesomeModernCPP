// Lambda Value Capture
// Demonstrates value capture semantics - the variable is copied into the lambda

#include <iostream>
#include <functional>
#include <string>
#include <cstdint>

int main() {
    std::cout << "=== Value Capture Demo ===" << std::endl;

    // Basic value capture
    int threshold = 100;

    auto is_high = [threshold](int value) {
        // threshold is a const copy inside the lambda
        return value > threshold;
    };

    std::cout << "is_high(150) = " << is_high(150) << std::endl;  // true
    std::cout << "is_high(50) = " << is_high(50) << std::endl;    // false

    // Modifying external variable doesn't affect the lambda
    threshold = 200;
    std::cout << "After threshold=200, is_high(150) = " << is_high(150) << std::endl;  // still true (uses original 100)

    // Multiple value captures
    int a = 10, b = 20, c = 30;
    auto sum = [a, b, c]() {
        return a + b + c;  // All are copies
    };
    std::cout << "sum() = " << sum() << std::endl;

    a = 100;
    std::cout << "After a=100, sum() = " << sum() << std::endl;  // Still 60

    // Value capture with complex types
    std::string message = "Hello, ";
    auto greet = [message]() {
        return message + "World!";  // message is copied
    };
    std::cout << "greet() = " << greet() << std::endl;

    message = "Goodbye, ";
    std::cout << "After message change, greet() = " << greet() << std::endl;  // Still "Hello, World!"

    // Embedded scenario: Configuration parameter capture
    std::cout << "\n=== Embedded: PWM Config ===" << std::endl;

    uint32_t base_addr = 0x40000000;
    int frequency = 1000;

    auto set_duty = [base_addr, frequency](int percent) {
        uint32_t period = 1000000 / frequency;  // Safe - frequency won't change
        uint32_t duty = period * percent / 100;
        std::cout << "PWM @ 0x" << std::hex << base_addr << std::dec
                  << ": period=" << period << "us, duty=" << duty << "us (" << percent << "%)" << std::endl;
    };

    set_duty(25);
    set_duty(50);
    set_duty(75);

    frequency = 2000;  // Doesn't affect the lambda
    std::cout << "After frequency=2000, set_duty still uses 1000Hz" << std::endl;
    set_duty(50);

    // Thread-safe counter (each lambda gets its own copy)
    std::cout << "\n=== Thread-Safe Counter ===" << std::endl;

    int counter = 0;
    auto get_lambda = [counter]() mutable {  // See mutable_capture.cpp for details
        counter++;
        return counter;
    };
    (void)get_lambda;

    // This creates a new lambda with a fresh copy each time
    auto lambda1 = [counter]() mutable {
        counter++;
        return counter;
    };
    counter = 100;
    auto lambda2 = [counter]() mutable {
        counter++;
        return counter;
    };

    std::cout << "lambda1() = " << lambda1() << std::endl;  // 1
    std::cout << "lambda2() = " << lambda2() << std::endl;  // 101

    return 0;
}
