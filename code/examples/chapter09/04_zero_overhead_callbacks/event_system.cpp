// Zero Overhead Event System
// A complete event handling system for embedded systems

#include <iostream>
#include <array>
#include <cstdint>
#include <functional>

// ZeroCallback implementation
template<typename Signature, size_t Size = 24>
class ZeroCallback;

template<size_t Size, typename R, typename... Args>
class ZeroCallback<R(Args...), Size> {
    struct VTable {
        void (*move)(void* dest, void* src);
        void (*destroy)(void* obj);
        R (*invoke)(void* obj, Args...);
    };

    alignas(std::max_align_t) std::byte storage[Size];
    const VTable* vtable = nullptr;

    template<typename T>
    struct VTableFor {
        static void do_move(void* dest, void* src) {
            new(dest) T(std::move(*reinterpret_cast<T*>(src)));
        }
        static void do_destroy(void* obj) {
            reinterpret_cast<T*>(obj)->~T();
        }
        static R do_invoke(void* obj, Args... args) {
            return (*reinterpret_cast<T*>(obj))(args...);
        }
        static constexpr VTable value = {do_move, do_destroy, do_invoke};
    };

public:
    ZeroCallback() = default;

    template<typename T>
    ZeroCallback(T&& callable) {
        using TDecay = std::decay_t<T>;
        static_assert(sizeof(TDecay) <= Size, "Callable too large");
        new(storage) TDecay(std::forward<T>(callable));
        vtable = &VTableFor<TDecay>::value;
    }

    ZeroCallback(ZeroCallback&& other) noexcept : vtable(other.vtable) {
        if (vtable) {
            vtable->move(storage, other.storage);
            other.vtable = nullptr;
        }
    }

    ~ZeroCallback() {
        if (vtable) {
            vtable->destroy(storage);
        }
    }

    ZeroCallback(const ZeroCallback&) = delete;
    ZeroCallback& operator=(const ZeroCallback&) = delete;
    ZeroCallback& operator=(ZeroCallback&& other) noexcept {
        if (this != &other) {
            if (vtable) {
                vtable->destroy(storage);
            }
            vtable = other.vtable;
            if (vtable) {
                vtable->move(storage, other.storage);
                other.vtable = nullptr;
            }
        }
        return *this;
    }

    R operator()(Args... args) const {
        return vtable->invoke(const_cast<std::byte*>(storage), args...);
    }

    bool empty() const { return vtable == nullptr; }
};

// Event types
enum class EventType : uint8_t {
    GPIO_CHANGED,
    ADC_COMPLETE,
    TIMER_EXPIRED,
    UART_RX,
    MAX_EVENTS
};

// Event system
class EventSystem {
public:
    using Handler = ZeroCallback<void(uint32_t), 24>;

    struct Slot {
        Handler callback;
        uint32_t user_data;

        Slot() = default;
        Slot(Handler cb, uint32_t data = 0)
            : callback(std::move(cb)), user_data(data) {}
        Slot& operator=(Slot&&) = default;
    };

    bool register_handler(EventType evt, Handler cb, uint32_t data = 0) {
        auto idx = static_cast<size_t>(evt);
        if (!handlers[idx].callback.empty()) {
            return false;  // Slot already occupied
        }
        handlers[idx] = {std::move(cb), data};
        return true;
    }

    void trigger(EventType evt, uint32_t param = 0) {
        auto idx = static_cast<size_t>(evt);
        if (!handlers[idx].callback.empty()) {
            handlers[idx].callback(param | handlers[idx].user_data);
        }
    }

private:
    std::array<Slot, static_cast<size_t>(EventType::MAX_EVENTS)> handlers{};
};

// Button driver
class ButtonDriver {
    int pin;
    int press_count = 0;

public:
    ButtonDriver(int p) : pin(p) {}

    void init(EventSystem& events) {
        events.register_handler(EventType::GPIO_CHANGED,
            [this](uint32_t pins) {
                if (pins & (1 << pin)) {
                    press_count++;
                    on_press();
                }
            });
    }

    void on_press() {
        std::cout << "Button on pin " << pin << " pressed (count: "
                  << press_count << ")" << std::endl;
    }
};

// LED controller
class LEDController {
    int pin;
    bool state = false;

public:
    LEDController(int p) : pin(p) {}

    void init(EventSystem& events) {
        events.register_handler(EventType::TIMER_EXPIRED,
            [this](uint32_t) {
                state = !state;
                std::cout << "LED pin " << pin << " -> "
                          << (state ? "ON" : "OFF") << std::endl;
            });
    }
};

// ADC handler
void setup_adc(EventSystem& events) {
    events.register_handler(EventType::ADC_COMPLETE,
        [](uint32_t channel) {
            std::cout << "ADC conversion complete, channel: "
                      << channel << ", value: " << (2048 + channel * 100) << std::endl;
        });
}

int main() {
    std::cout << "=== Zero Overhead Event System Demo ===" << std::endl;

    EventSystem g_events;

    // Initialize devices
    ButtonDriver btn(5);
    btn.init(g_events);

    LEDController led(10);
    led.init(g_events);

    setup_adc(g_events);

    // Simulate interrupts
    std::cout << "\n--- Simulating Events ---" << std::endl;

    g_events.trigger(EventType::GPIO_CHANGED, 0x20);  // Button press
    g_events.trigger(EventType::TIMER_EXPIRED, 0);    // LED toggle
    g_events.trigger(EventType::ADC_COMPLETE, 0);     // ADC done
    g_events.trigger(EventType::GPIO_CHANGED, 0x20);  // Another button press
    g_events.trigger(EventType::TIMER_EXPIRED, 0);    // LED toggle

    std::cout << "\n=== Performance Characteristics ===" << std::endl;
    std::cout << "- No heap allocation (stack storage only)" << std::endl;
    std::cout << "- Single indirection through function pointer" << std::endl;
    std::cout << "- Compiler can inline in many cases" << std::endl;
    std::cout << "- Suitable for interrupt handlers" << std::endl;
    std::cout << "- Fixed overhead per callback (~24 bytes)" << std::endl;

    return 0;
}
