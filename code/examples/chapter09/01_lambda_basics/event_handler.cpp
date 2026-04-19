// Event Handler with Lambda
// Demonstrates lambda expressions for event handling in embedded systems

#include <iostream>
#include <functional>
#include <array>
#include <cstdint>

// Simple GPIO Manager with event handling
class GPIOManager {
public:
    using EventHandler = std::function<void(uint32_t timestamp)>;

    GPIOManager() : handlers{} {}

    void on_rising_edge(int pin, EventHandler handler) {
        if (pin >= 0 && pin < 16) {
            handlers[pin] = std::move(handler);
            std::cout << "Registered handler for pin " << pin << std::endl;
        }
    }

    void simulate_interrupt(int pin, uint32_t timestamp) {
        if (pin >= 0 && pin < 16 && handlers[pin]) {
            std::cout << "[IRQ " << timestamp << "ms] Pin " << pin << " triggered" << std::endl;
            handlers[pin](timestamp);
        }
    }

private:
    std::array<EventHandler, 16> handlers;
};

// LED Controller example
class LEDController {
public:
    LEDController(int p) : pin(p), state(false) {}

    std::function<void(uint32_t)> get_toggle_handler() {
        return [this](uint32_t timestamp) {
            (void)timestamp;
            state = !state;
            std::cout << "  -> LED on pin " << pin << " toggled to "
                      << (state ? "ON" : "OFF") << std::endl;
        };
    }

private:
    int pin;
    bool state;
};

// Button with debounce
class Button {
public:
    Button(int p) : pin(p), debounce_count(0), last_press_time(0) {}

    std::function<void(uint32_t)> get_debounced_handler() {
        return [this](uint32_t timestamp) {
            // 50ms debounce
            if (timestamp - last_press_time > 50) {
                debounce_count++;
                last_press_time = timestamp;
                std::cout << "  -> Button " << pin << " pressed (count: "
                          << debounce_count << ")" << std::endl;
                on_press();
            }
        };
    }

    void on_press() {
        std::cout << "  -> Executing button action..." << std::endl;
    }

    int get_count() const { return debounce_count; }

private:
    int pin;
    int debounce_count;
    uint32_t last_press_time;
};

int main() {
    std::cout << "=== Event Handler Demo ===" << std::endl;

    GPIOManager gpio;

    // Example 1: Simple toggle handler
    std::cout << "\n--- Example 1: LED Toggle ---" << std::endl;
    LEDController led(5);
    gpio.on_rising_edge(5, led.get_toggle_handler());
    gpio.simulate_interrupt(5, 100);
    gpio.simulate_interrupt(5, 200);
    gpio.simulate_interrupt(5, 300);

    // Example 2: Debounced button
    std::cout << "\n--- Example 2: Debounced Button ---" << std::endl;
    Button button(3);
    gpio.on_rising_edge(3, button.get_debounced_handler());

    // Simulate rapid button presses (some should be debounced)
    gpio.simulate_interrupt(3, 500);   // Will register
    gpio.simulate_interrupt(3, 510);   // Debounced (too soon)
    gpio.simulate_interrupt(3, 520);   // Debounced
    gpio.simulate_interrupt(3, 600);   // Will register
    gpio.simulate_interrupt(3, 610);   // Debounced

    std::cout << "Total button presses registered: " << button.get_count() << std::endl;

    // Example 3: Lambda with captured context
    std::cout << "\n--- Example 3: Context Capture ---" << std::endl;
    int event_counter = 0;
    gpio.on_rising_edge(7, [&event_counter](uint32_t timestamp) {
        event_counter++;
        std::cout << "  -> Event #" << event_counter << " at " << timestamp << "ms" << std::endl;
    });

    gpio.simulate_interrupt(7, 1000);
    gpio.simulate_interrupt(7, 2000);
    gpio.simulate_interrupt(7, 3000);

    return 0;
}
