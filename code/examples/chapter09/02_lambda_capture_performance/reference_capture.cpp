// Lambda Reference Capture
// Demonstrates reference capture and lifetime considerations

#include <iostream>
#include <functional>
#include <vector>

int main() {
    std::cout << "=== Reference Capture Demo ===" << std::endl;

    // Basic reference capture
    int sum = 0;

    auto accumulator = [&sum](int value) {
        sum += value;  // Modifies the original sum variable
    };

    accumulator(10);
    accumulator(20);
    accumulator(30);
    std::cout << "After accumulations, sum = " << sum << std::endl;  // 60

    // Reference capture reflects external changes
    int threshold = 100;
    auto is_high = [&threshold](int value) {
        return value > threshold;
    };

    std::cout << "is_high(150) = " << is_high(150) << std::endl;  // true

    threshold = 200;
    std::cout << "After threshold=200, is_high(150) = " << is_high(150) << std::endl;  // false

    // Full reference capture
    int x = 1, y = 2, z = 3;
    auto modify_all = [&]() {
        x *= 2;
        y *= 2;
        z *= 2;
    };
    modify_all();
    std::cout << "After modify_all: x=" << x << ", y=" << y << ", z=" << z << std::endl;

    // Mixed capture: some by value, some by reference
    int multiplier = 2;
    int total = 0;
    auto mixed = [multiplier, &total](int value) {
        total += value * multiplier;  // multiplier is copied, total is referenced
    };
    mixed(5);
    mixed(10);
    std::cout << "After mixed(5, 10), total = " << total << std::endl;  // 30

    // WARNING: Lifetime issues
    std::cout << "\n=== Lifetime Warning ===" << std::endl;

    // BAD: Returning lambda with reference to local variable
    /*
    auto bad_counter() {
        int count = 0;
        return [&count]() { return ++count; };  // Dangling reference!
    }
    */

    // GOOD: Return by value or use safe lifetime
    auto safe_counter() {
        int count = 0;
        return [count]() mutable {  // Copy the value
            return ++count;
        };
    }

    auto counter1 = safe_counter();
    std::cout << "safe_counter() = " << counter1() << std::endl;  // 1
    std::cout << "safe_counter() = " << counter1() << std::endl;  // 2

    // Using class to manage lifetime
    class SafeCounter {
    public:
        std::function<int()> get_lambda() {
            return [this]() {
                return ++count;
            };
        }
    private:
        int count = 0;
    };

    SafeCounter sc;
    auto counter2 = sc.get_lambda();
    std::cout << "SafeCounter lambda = " << counter2() << std::endl;  // 1

    // Embedded scenario: Using reference capture for status
    std::cout << "\n=== Embedded: Status Update ===" << std::endl;

    struct Status {
        bool ready = false;
        int error_code = 0;
    } status;

    auto set_ready = [&status]() {
        status.ready = true;
        status.error_code = 0;
    };

    auto set_error = [&status](int code) {
        status.ready = false;
        status.error_code = code;
    };

    set_ready();
    std::cout << "Status: ready=" << status.ready << ", error=" << status.error_code << std::endl;

    set_error(42);
    std::cout << "Status: ready=" << status.ready << ", error=" << status.error_code << std::endl;

    // Caution with loop variables
    std::cout << "\n=== Loop Capture Caution ===" << std::endl;

    std::vector<std::function<void()>> handlers;

    // BAD: All lambdas reference the same i
    /*
    for (int i = 0; i < 5; ++i) {
        handlers.push_back([&i]() { std::cout << i << " "; });
    }
    // All output 5 (or undefined)
    */

    // GOOD: Capture by value
    for (int i = 0; i < 5; ++i) {
        handlers.push_back([i]() { std::cout << i << " "; });
    }

    std::cout << "Handlers output: ";
    for (auto& h : handlers) {
        h();
    }
    std::cout << std::endl;

    return 0;
}
