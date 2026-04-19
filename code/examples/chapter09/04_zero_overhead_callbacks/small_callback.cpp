// Small Callback: Fixed-Size Type Erasure
// A practical implementation of zero-overhead callbacks for embedded systems

#include <iostream>
#include <functional>
#include <array>
#include <cstdint>

template<typename Signature, size_t StorageSize = 32>
class SmallCallback;

template<size_t StorageSize, typename R, typename... Args>
class SmallCallback<R(Args...), StorageSize> {
    // Concept interface (with virtual functions - simpler implementation)
    struct Concept {
        virtual ~Concept() = default;
        virtual R invoke(Args...) = 0;
        virtual void move_to(void* dest) = 0;
    };

    // Model for specific callable types
    template<typename T>
    struct Model : Concept {
        T callable;

        Model(T&& c) : callable(std::forward<T>(c)) {}

        R invoke(Args... args) override {
            return callable(args...);
        }

        void move_to(void* dest) override {
            new(dest) Model(std::move(callable));
        }
    };

    // In-place storage
    alignas(std::max_align_t) std::byte storage[StorageSize];
    Concept* object = nullptr;

public:
    SmallCallback() = default;

    template<typename T>
    SmallCallback(T&& callable) {
        static_assert(sizeof(T) <= StorageSize, "Callable too large");
        object = new(storage) Model<T>(std::forward<T>(callable));
    }

    ~SmallCallback() {
        if (object) {
            object->~Concept();
        }
    }

    SmallCallback(const SmallCallback&) = delete;
    SmallCallback& operator=(const SmallCallback&) = delete;

    SmallCallback(SmallCallback&& other) noexcept {
        if (other.object) {
            other.object->move_to(storage);
            object = reinterpret_cast<Concept*>(storage);
            other.object = nullptr;
        }
    }

    SmallCallback& operator=(SmallCallback&& other) noexcept {
        if (this != &other) {
            if (object) {
                object->~Concept();
                object = nullptr;
            }
            if (other.object) {
                other.object->move_to(storage);
                object = reinterpret_cast<Concept*>(storage);
                other.object = nullptr;
            }
        }
        return *this;
    }

    R operator()(Args... args) {
        return object->invoke(args...);
    }

    bool empty() const { return object == nullptr; }
};

// Event type
enum class EventType { GPIO, ADC, TIMER, UART };

// Event system using SmallCallback
class EmbeddedEventSystem {
public:
    using Handler = SmallCallback<void(uint32_t), 24>;

    struct Slot {
        Handler callback;
        uint32_t user_data = 0;

        Slot() = default;
        Slot(Handler cb, uint32_t data = 0)
            : callback(std::move(cb)), user_data(data) {}

        Slot& operator=(Slot&& other) noexcept {
            if (this != &other) {
                callback = std::move(other.callback);
                user_data = other.user_data;
            }
            return *this;
        }
    };

    bool register_handler(EventType evt, Handler cb, uint32_t data = 0) {
        auto idx = static_cast<size_t>(evt);
        if (idx < handlers.size() && handlers[idx].callback.empty()) {
            handlers[idx] = {std::move(cb), data};
            return true;
        }
        return false;
    }

    void trigger(EventType evt, uint32_t param = 0) {
        auto idx = static_cast<size_t>(evt);
        if (idx < handlers.size() && !handlers[idx].callback.empty()) {
            handlers[idx].callback(param | handlers[idx].user_data);
        }
    }

private:
    std::array<Slot, 4> handlers{};
};

// Demo
int main() {
    std::cout << "=== Small Callback Demo ===" << std::endl;

    EmbeddedEventSystem events;

    // Register handlers with captures
    int button_count = 0;
    events.register_handler(EventType::GPIO,
        [&button_count](uint32_t pins) {
            if (pins & 0x01) {
                button_count++;
                std::cout << "Button pressed! Count: " << button_count << std::endl;
            }
        }
    );

    // Register ADC handler
    events.register_handler(EventType::ADC,
        [](uint32_t channel) {
            std::cout << "ADC conversion complete on channel " << channel << std::endl;
        }
    );

    // Trigger events
    events.trigger(EventType::GPIO, 0x01);  // Button press
    events.trigger(EventType::GPIO, 0x01);  // Another press
    events.trigger(EventType::ADC, 0);       // ADC complete

    std::cout << "\n=== Key Features ===" << std::endl;
    std::cout << "- Fixed size storage (no heap)" << std::endl;
    std::cout << "- Can capture context (unlike function pointers)" << std::endl;
    std::cout << "- Uses virtual functions (small overhead)" << std::endl;
    std::cout << "- Suitable for interrupt-safe callbacks" << std::endl;

    return 0;
}
