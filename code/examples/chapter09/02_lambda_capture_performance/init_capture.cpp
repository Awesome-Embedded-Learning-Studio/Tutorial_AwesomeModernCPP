// Lambda Init Capture (C++14)
// Demonstrates generalized lambda capture with initialization expressions

#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main() {
    std::cout << "=== Init Capture Demo (C++14) ===" << std::endl;

    // Basic init capture: capture result of expression
    int base = 10;

    auto lambda = [value = base + 5]() {
        return value * 2;  // value is 15
    };

    std::cout << "lambda() = " << lambda() << std::endl;  // 30

    // Capturing computed value
    auto timer_config = [prescaler = 72000000 / 10000 - 1]() {
        std::cout << "Timer prescaler: " << prescaler << std::endl;
        return prescaler;
    };
    timer_config();

    // Move capture with unique_ptr
    std::cout << "\n=== Move Capture ===" << std::endl;

    auto ptr = std::make_unique<int>(42);
    std::cout << "Original ptr value: " << *ptr << std::endl;

    auto lambda_with_ptr = [p = std::move(ptr)]() {
        return *p;
    };
    std::cout << "lambda_with_ptr() = " << lambda_with_ptr() << std::endl;
    // ptr is now nullptr (moved into lambda)

    // More practical example: move a vector
    auto large_data = std::make_unique<std::vector<int>>(1000, 42);

    auto process_data = [data = std::move(large_data)]() {
        std::cout << "Processing " << data->size() << " elements" << std::endl;
        return data->front();
    };
    std::cout << "process_data() = " << process_data() << std::endl;

    // Init capture vs mutable
    std::cout << "\n=== Init Capture vs Mutable ===" << std::endl;

    // C++11 style with mutable
    int x = 0;
    auto lambda_mutable = [x]() mutable {
        x += 1;
        return x;
    };
    std::cout << "lambda_mutable() = " << lambda_mutable() << std::endl;  // 1
    std::cout << "lambda_mutable() = " << lambda_mutable() << std::endl;  // 2

    // C++14 style with init capture - clearer intent
    auto counter = [count = 0]() mutable {
        count += 1;
        return count;
    };
    std::cout << "counter() = " << counter() << std::endl;  // 1
    std::cout << "counter() = " << counter() << std::endl;  // 2

    // Complex init capture
    std::cout << "\n=== Complex Init Capture ===" << std::endl;

    std::string name = "sensor";
    [id = 1, label = std::move(name)]() {
        std::cout << "ID: " << id << ", Label: " << label << std::endl;
    }();

    // Capturing by reference with alias
    int value = 100;
    auto ref_alias = [ref = value]() {
        std::cout << "ref = " << ref << std::endl;
    };
    (void)ref_alias;

    // Embedded: Capture computed register values
    std::cout << "\n=== Embedded: Register Configuration ===" << std::endl;

    int system_clock = 72000000;
    int target_frequency = 1000;

    auto setup_timer = [psc = system_clock / (target_frequency * 1000) - 1,
                        arr = 10000 - 1]() {
        std::cout << "PSC = " << psc << ", ARR = " << arr << std::endl;
        std::cout << "Resulting frequency = "
                  << 72000000 / ((psc + 1) * (arr + 1)) << " Hz" << std::endl;
    };
    setup_timer();

    // Init capture for state machine
    std::cout << "\n=== State Machine Example ===" << std::endl;

    enum State { IDLE, RUNNING, DONE };

    auto state_machine = [state = IDLE, count = 0]() mutable {
        switch (state) {
            case IDLE:
                count = 0;
                state = RUNNING;
                return "Started";
            case RUNNING:
                count++;
                if (count > 2) state = DONE;
                return "Running";
            case DONE:
                return "Done";
        }
        return "Unknown";
    };

    std::cout << state_machine() << std::endl;
    std::cout << state_machine() << std::endl;
    std::cout << state_machine() << std::endl;
    std::cout << state_machine() << std::endl;

    // Capturing const references
    std::cout << "\n=== Const Reference Capture ===" << std::endl;

    const std::string config = "some_config";

    // Capture by const reference (no copy)
    auto use_config = [&cfg = config]() {
        std::cout << "Using config: " << cfg << std::endl;
    };
    use_config();

    // Capture with decltype(auto) for perfect forwarding (C++17/20)
    auto forward_capture = [x = std::move(value)]() {
        std::cout << "Captured value: " << x << std::endl;
    };
    forward_capture();

    return 0;
}
