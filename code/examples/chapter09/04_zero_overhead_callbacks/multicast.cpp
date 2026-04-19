// Multicast Callback System
// Demonstrates multiple listeners for a single event

#include <iostream>
#include <array>
#include <cstdint>
#include <functional>
#include <cstring>
#include <vector>

// Simple zero-overhead callback
template<typename Signature, size_t Size = 24>
class ZeroCallback;

template<size_t Size, typename R, typename... Args>
class ZeroCallback<R(Args...), Size> {
    struct VTable {
        void (*destroy)(void* obj);
        R (*invoke)(void* obj, Args...);
    };

    alignas(std::max_align_t) std::byte storage[Size];
    const VTable* vtable = nullptr;

    template<typename T>
    struct VTableFor {
        static void do_destroy(void* obj) {
            reinterpret_cast<T*>(obj)->~T();
        }
        static R do_invoke(void* obj, Args... args) {
            return (*reinterpret_cast<T*>(obj))(args...);
        }
        static constexpr VTable value = {do_destroy, do_invoke};
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

    ~ZeroCallback() {
        if (vtable) {
            vtable->destroy(storage);
        }
    }

    // Copy constructor
    ZeroCallback(const ZeroCallback& other) : vtable(other.vtable) {
        if (vtable) {
            std::memcpy(storage, other.storage, Size);
        }
    }

    // Copy assignment
    ZeroCallback& operator=(const ZeroCallback& other) {
        if (this != &other) {
            if (vtable) vtable->destroy(storage);
            vtable = other.vtable;
            if (vtable) std::memcpy(storage, other.storage, Size);
        }
        return *this;
    }

    R operator()(Args... args) const {
        return vtable->invoke(const_cast<std::byte*>(storage), args...);
    }

    bool empty() const { return vtable == nullptr; }
};

// Multicast callback container
template<typename Signature, size_t Size = 24, size_t MaxListeners = 4>
class MulticastCallback {
    using Handler = ZeroCallback<Signature, Size>;
    std::array<Handler, MaxListeners> handlers;
    size_t count = 0;

public:
    bool subscribe(Handler h) {
        if (count >= MaxListeners) {
            std::cout << "Failed: maximum listeners reached" << std::endl;
            return false;
        }
        handlers[count++] = std::move(h);
        return true;
    }

    template<typename... Args>
    void publish(Args&&... args) {
        for (size_t i = 0; i < count; ++i) {
            handlers[i](std::forward<Args>(args)...);
        }
    }

    size_t listener_count() const { return count; }
};

// Demo: ADC event with multiple listeners
void demo_adc_multicast() {
    std::cout << "=== ADC Multicast Demo ===" << std::endl;

    MulticastCallback<void(int), 24, 4> adc_event;

    // Listener 1: Log values
    adc_event.subscribe([](int value) {
        std::cout << "  [Logger] ADC value: " << value << std::endl;
    });

    // Listener 2: Update display
    adc_event.subscribe([](int value) {
        std::cout << "  [Display] Showing: " << value << " mV" << std::endl;
    });

    // Listener 3: Store data
    adc_event.subscribe([](int value) {
        std::cout << "  [Storage] Buffered: " << value << std::endl;
    });

    // Listener 4: Check thresholds
    adc_event.subscribe([](int value) {
        if (value > 3000) {
            std::cout << "  [Alert] High voltage detected!" << std::endl;
        }
    });

    std::cout << "Subscribed " << adc_event.listener_count() << " listeners" << std::endl;

    // Publish events
    std::cout << "\nPublishing ADC value 2048:" << std::endl;
    adc_event.publish(2048);

    std::cout << "\nPublishing ADC value 3500:" << std::endl;
    adc_event.publish(3500);
}

// Demo: GPIO event with context
void demo_gpio_multicast() {
    std::cout << "\n=== GPIO Multicast Demo ===" << std::endl;

    MulticastCallback<void(uint32_t), 32, 3> gpio_events;

    int press_counter = 0;
    int last_pin = 0;

    // Handler with capture
    gpio_events.subscribe([&press_counter](uint32_t pins) {
        if (pins & 0x01) {
            press_counter++;
            std::cout << "  Button 1 pressed (total: " << press_counter << ")" << std::endl;
        }
    });

    gpio_events.subscribe([&last_pin](uint32_t pins) {
        if (pins & 0x02) {
            last_pin = 2;
            std::cout << "  Button 2 pressed" << std::endl;
        }
    });

    gpio_events.subscribe([](uint32_t pins) {
        std::cout << "  GPIO state changed: 0x" << std::hex << pins << std::dec << std::endl;
    });

    // Trigger events
    std::cout << "\nTriggering GPIO events:" << std::endl;
    gpio_events.publish(0x01);  // Button 1
    gpio_events.publish(0x02);  // Button 2
    gpio_events.publish(0x01);  // Button 1 again
}

int main() {
    demo_adc_multicast();
    demo_gpio_multicast();

    std::cout << "\n=== Use Cases ===" << std::endl;
    std::cout << "- Sensor data processing (log + display + store)" << std::endl;
    std::cout << "- Event notification systems" << std::endl;
    std::cout << "- Observer pattern implementation" << std::endl;
    std::cout << "- Debug/monitoring hooks in production code" << std::endl;

    return 0;
}
