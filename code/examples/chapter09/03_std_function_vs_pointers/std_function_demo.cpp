// std::function Demo
// Demonstrates std::function usage and its capabilities

#include <iostream>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

// Basic functions
int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}

// Functor
struct Multiplier {
    int factor;
    Multiplier(int f) : factor(f) {}
    int operator()(int x) const { return x * factor; }
};

void demo_basics() {
    std::cout << "=== std::function Basics ===" << std::endl;

    std::function<int(int, int)> func;

    // Store a function pointer
    func = add;
    std::cout << "func(3, 4) = " << func(3, 4) << std::endl;

    // Store a lambda
    func = [](int a, int b) { return a * b; };
    std::cout << "func(3, 4) = " << func(3, 4) << std::endl;

    // Store a functor (wrapped to match int(int,int) signature)
    func = [](int a, int b) { return a * b; };
    std::cout << "func(10) with Multiplier(5) = " << func(10, 0) << std::endl;
}

// Capturing context - the key advantage
class Button {
    int pin;
    int debounce_count = 0;

public:
    Button(int p) : pin(p) {}

    std::function<void()> get_handler() {
        return [this]() {
            debounce_count++;
            std::cout << "Button on pin " << pin
                      << " pressed, count: " << debounce_count << std::endl;
        };
    }
};

void demo_context_capture() {
    std::cout << "\n=== Context Capture ===" << std::endl;

    Button btn(5);
    auto handler = btn.get_handler();
    handler();
    handler();
}

// Event loop using std::function
class EventLoop {
public:
    using Callback = std::function<void()>;

    void register_timer(int id, uint32_t interval_ms, Callback cb) {
        timers.push_back({id, interval_ms, 0, std::move(cb)});
    }

    void tick(uint32_t delta_ms) {
        for (auto& timer : timers) {
            timer.elapsed += delta_ms;
            if (timer.elapsed >= timer.interval && timer.callback) {
                timer.callback();
                timer.elapsed = 0;
            }
        }
    }

private:
    struct Timer {
        int id;
        uint32_t interval;
        uint32_t elapsed;
        Callback callback;
    };
    std::vector<Timer> timers;
};

void demo_event_loop() {
    std::cout << "\n=== Event Loop ===" << std::endl;

    EventLoop loop;
    int counter = 0;

    // Capture counter by reference
    loop.register_timer(0, 1000, [&counter]() {
        counter++;
        std::cout << "Timer fired! Counter: " << counter << std::endl;
    });

    // Simulate 3 ticks
    for (int i = 0; i < 3; ++i) {
        loop.tick(1000);
    }
}

// Storing different callable types
void demo_polymorphism() {
    std::cout << "\n=== Type Erasure ===" << std::endl;

    std::vector<std::function<void()>> tasks;

    // Add different types of callables
    tasks.push_back([]() {
        std::cout << "Lambda task" << std::endl;
    });

    tasks.push_back([]() { add(1, 2); });
    tasks.push_back([]() { Multiplier(3)(10); });

    for (auto& task : tasks) {
        task();
    }
}

// std::function with return types
void demo_return_types() {
    std::cout << "\n=== Return Types ===" << std::endl;

    std::function<int(int)> factorial = [&](int n) -> int {
        return n <= 1 ? 1 : n * factorial(n - 1);
    };

    std::cout << "factorial(5) = " << factorial(5) << std::endl;

    // Recursive lambda with std::function
    std::function<int(int)> fib = [&](int n) -> int {
        return n <= 1 ? n : fib(n - 1) + fib(n - 2);
    };

    std::cout << "fib(10) = " << fib(10) << std::endl;
}

// Memory overhead discussion
void demo_memory_overhead() {
    std::cout << "\n=== Memory Overhead ===" << std::endl;

    std::cout << "sizeof(std::function<void()>) = "
              << sizeof(std::function<void()>) << " bytes" << std::endl;
    std::cout << "sizeof(void(*)()) = "
              << sizeof(void(*)()) << " bytes" << std::endl;

    // Small Object Optimization (SOO)
    std::function<void()> small = [x = 42]() { std::cout << x << std::endl; };
    std::cout << "Small lambda in std::function: no heap allocation" << std::endl;

    // Large lambda may cause heap allocation
    std::function<void()> large = [big = std::array<int, 1000>()]() {
        std::cout << "Large lambda" << std::endl;
    };
    std::cout << "Large lambda: may use heap allocation" << std::endl;
}

// Checking if std::function is empty
void demo_empty_check() {
    std::cout << "\n=== Empty Check ===" << std::endl;

    std::function<void()> func;

    if (!func) {
        std::cout << "Function is empty" << std::endl;
    }

    func = []() { std::cout << "Now it has a target" << std::endl; };

    if (func) {
        std::cout << "Function has a target" << std::endl;
        func();
    }

    func = nullptr;

    if (!func) {
        std::cout << "Cleared back to empty" << std::endl;
    }
}

int main() {
    std::cout << "=== std::function Demo ===" << std::endl;

    demo_basics();
    demo_context_capture();
    demo_event_loop();
    demo_polymorphism();
    demo_return_types();
    demo_memory_overhead();
    demo_empty_check();

    return 0;
}
