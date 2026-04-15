// Universal Caller with std::invoke
// Demonstrates a generic callback system using std::invoke

#include <iostream>
#include <functional>
#include <array>
#include <cstdint>
#include <string>

// Universal callback manager
class CallbackManager {
public:
    // Register any callable with automatic type erasure
    template<typename Func>
    void register_callback(int id, Func&& callback) {
        // In a real implementation, you'd store this properly
        // For demo, we just show the registration
        std::cout << "Registered callback " << id << std::endl;
    }

    // Trigger callback using std::invoke
    template<typename... Args>
    void trigger(int id, Args&&... args) {
        // In a real implementation, you'd look up and invoke the stored callback
        std::cout << "Triggering callback " << id << std::endl;
    }
};

// Example classes
class LEDController {
    int pin;
    int flash_count = 0;

public:
    LEDController(int p) : pin(p) {}

    void on_timer(uint32_t timestamp) {
        flash_count = (flash_count + 1) % 10;
        std::cout << "LED on pin " << pin << " flash: " << flash_count << std::endl;
    }
};

// Global function
void global_timer_handler(uint32_t timestamp) {
    std::cout << "Global timer fired at " << timestamp << " ms" << std::endl;
}

// Demo
void demo_universal_caller() {
    std::cout << "=== Universal Caller Demo ===" << std::endl;

    CallbackManager manager;
    LEDController led(5);

    // All these work with the same interface
    auto lambda_handler = [](uint32_t ts) {
        std::cout << "Lambda callback at " << ts << " ms" << std::endl;
    };

    // Using std::invoke to call different types uniformly
    std::cout << "\nCalling different types:" << std::endl;

    std::invoke(global_timer_handler, 1000);

    std::invoke(lambda_handler, 2000);

    std::invoke(&LEDController::on_timer, led, 3000);

    // With pointer
    LEDController* led_ptr = &led;
    std::invoke(&LEDController::on_timer, led_ptr, 4000);
}

// Generic invoker template
template<typename Func, typename... Args>
auto universal_call(Func&& func, Args&&... args)
    -> decltype(std::invoke(std::forward<Func>(func), std::forward<Args>(args)...))
{
    return std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
}

void demo_generic_invoker() {
    std::cout << "\n=== Generic Invoker ===" << std::endl;

    struct Calculator {
        int add(int a, int b) const { return a + b; }
    };

    Calculator calc;

    auto result = universal_call(&Calculator::add, calc, 10, 20);
    std::cout << "10 + 20 = " << result << std::endl;

    auto lambda_result = universal_call([](int x) { return x * 2; }, 21);
    std::cout << "21 * 2 = " << lambda_result << std::endl;
}

// Embedded callback system
class EmbeddedCallbackSystem {
public:
    template<typename Func>
    void register_gpio_handler(int pin, Func&& handler) {
        // Would store handler for later invocation
        std::cout << "Registered GPIO handler for pin " << pin << std::endl;
    }

    template<typename Func>
    void register_timer_handler(int timer_id, Func&& handler) {
        std::cout << "Registered timer handler for timer " << timer_id << std::endl;
    }

    // This would be called from interrupt context
    void trigger_gpio(int pin, uint32_t timestamp) {
        std::cout << "[GPIO IRQ] Pin " << pin << " at " << timestamp << " ms" << std::endl;
    }
};

class Device {
    int device_id;
public:
    Device(int id) : device_id(id) {}

    void on_gpio(uint32_t timestamp) {
        std::cout << "Device " << device_id << " GPIO event at " << timestamp << std::endl;
    }
};

void demo_embedded_system() {
    std::cout << "\n=== Embedded Callback System ===" << std::endl;

    EmbeddedCallbackSystem system;
    Device dev1(1);
    Device dev2(2);

    // Register member functions as callbacks
    system.register_gpio_handler(5, [&dev1](uint32_t ts) {
        std::invoke(&Device::on_gpio, dev1, ts);
    });

    system.register_gpio_handler(6, [&dev2](uint32_t ts) {
        std::invoke(&Device::on_gpio, dev2, ts);
    });

    // Simulate interrupts
    system.trigger_gpio(5, 1000);
    system.trigger_gpio(6, 2000);
}

int main() {
    demo_universal_caller();
    demo_generic_invoker();
    demo_embedded_system();

    return 0;
}
