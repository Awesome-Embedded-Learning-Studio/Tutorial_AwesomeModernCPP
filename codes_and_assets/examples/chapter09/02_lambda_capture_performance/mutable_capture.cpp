// Lambda Mutable Capture
// Demonstrates the mutable keyword and its effect on value-captured variables

#include <iostream>
#include <functional>

int main() {
    std::cout << "=== Mutable Lambda Demo ===" << std::endl;

    // Without mutable: captured variables are const by default
    int counter = 0;
    auto lambda1 = [counter]() {
        // counter++;  // ERROR: counter is const
        return counter;
    };

    std::cout << "lambda1() = " << lambda1() << std::endl;

    // With mutable: allows modification of captured copies
    auto lambda2 = [counter]() mutable {
        counter++;
        return counter;
    };

    std::cout << "lambda2() = " << lambda2() << std::endl;  // 1
    std::cout << "lambda2() = " << lambda2() << std::endl;  // 2
    std::cout << "External counter = " << counter << std::endl;  // Still 0

    // Comparison with reference capture
    std::cout << "\n=== Value vs Reference Capture ===" << std::endl;

    int value = 10;

    auto value_capture = [value]() mutable {
        value += 5;
        return value;
    };

    auto ref_capture = [&value]() {
        value += 5;
        return value;
    };

    std::cout << "Initial value: " << value << std::endl;
    std::cout << "value_capture() = " << value_capture() << std::endl;  // 15
    std::cout << "After value_capture, value = " << value << std::endl;  // 10

    std::cout << "ref_capture() = " << ref_capture() << std::endl;  // 15
    std::cout << "After ref_capture, value = " << value << std::endl;  // 15

    // Use case: Thread-local counter
    std::cout << "\n=== Thread-Local Counter ===" << std::endl;

    auto create_counter = [count = 0]() mutable {
        return [count]() mutable {  // Each lambda gets its own copy
            count++;
            return count;
        };
    };

    auto counter1 = create_counter();
    auto counter2 = create_counter();

    std::cout << "counter1: " << counter1() << ", " << counter1() << std::endl;
    std::cout << "counter2: " << counter2() << ", " << counter2() << std::endl;

    // Embedded scenario: Debounce counter in callback
    std::cout << "\n=== Embedded: Debounce Counter ===" << std::endl;

    class Button {
    public:
        auto get_handler() {
            return [this, debounce_count = 0]() mutable {
                debounce_count++;
                std::cout << "Button " << pin << " pressed, debounce count: "
                          << debounce_count << std::endl;
                return debounce_count;
            };
        }
    private:
        int pin = 5;
    };

    Button btn;
    auto handler = btn.get_handler();
    handler();
    handler();
    handler();

    // Accumulator with mutable
    std::cout << "\n=== Accumulator Example ===" << std::endl;

    auto make_accumulator = [sum = 0]() mutable {
        return [&sum](int x) {
            sum += x;
            return sum;
        };
    };

    auto acc = make_accumulator();
    std::cout << "acc(10) = " << acc(10) << std::endl;
    std::cout << "acc(20) = " << acc(20) << std::endl;
    std::cout << "acc(30) = " << acc(30) << std::endl;

    // Comparison with init capture
    std::cout << "\n=== Mutable vs Init Capture ===" << std::endl;

    // Traditional mutable
    int x = 0;
    auto trad_mutable = [x]() mutable {
        x++;
        return x;
    };

    // Init capture (more explicit)
    auto init_capture = [counter = 0]() mutable {
        counter++;
        return counter;
    };

    std::cout << "trad_mutable() = " << trad_mutable() << std::endl;
    std::cout << "init_capture() = " << init_capture() << std::endl;

    // Callable object equivalent (what the compiler generates)
    std::cout << "\n=== Compiler Generated Equivalent ===" << std::endl;

    struct LambdaEquivalent {
        int captured_x;

        LambdaEquivalent(int x) : captured_x(x) {}

        int operator()() {
            captured_x++;
            return captured_x;
        }
    };

    LambdaEquivalent equiv(0);
    std::cout << "equiv() = " << equiv() << std::endl;
    std::cout << "equiv() = " << equiv() << std::endl;

    return 0;
}
