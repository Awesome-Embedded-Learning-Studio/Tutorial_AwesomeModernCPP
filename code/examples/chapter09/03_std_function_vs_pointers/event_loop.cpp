// Event Loop with std::function
// Demonstrates using std::function for flexible callback management

#include <iostream>
#include <functional>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

// Event type
enum class EventType {
    TIMER,
    GPIO,
    UART,
    ADC,
    NETWORK
};

// Base event structure
struct Event {
    EventType type;
    uint32_t timestamp;
    int data;

    Event(EventType t, uint32_t ts, int d = 0)
        : type(t), timestamp(ts), data(d) {}
};

// Timer structure
struct Timer {
    int id;
    uint32_t interval_ms;
    uint32_t elapsed_ms;
    std::function<void()> callback;
    bool repeat;

    Timer(int i, uint32_t interval, std::function<void()> cb, bool r = false)
        : id(i), interval_ms(interval), elapsed_ms(0), callback(std::move(cb)), repeat(r) {}
};

// Event Loop
class EventLoop {
public:
    EventLoop() : running_(false), current_time_(0) {}

    // Timer management
    int add_timer(uint32_t interval_ms, std::function<void()> callback, bool repeat = false) {
        int id = next_timer_id_++;
        timers_.emplace_back(id, interval_ms, std::move(callback), repeat);
        std::cout << "Timer " << id << " added, interval: " << interval_ms << " ms" << std::endl;
        return id;
    }

    void remove_timer(int id) {
        auto it = std::remove_if(timers_.begin(), timers_.end(),
                                [id](const Timer& t) { return t.id == id; });
        if (it != timers_.end()) {
            timers_.erase(it, timers_.end());
            std::cout << "Timer " << id << " removed" << std::endl;
        }
    }

    // Event posting
    void post_event(Event event) {
        event_queue_.push_back(std::move(event));
    }

    // Run the event loop
    void run() {
        running_ = true;
        std::cout << "\n=== Event Loop Started ===" << std::endl;

        while (running_) {
            tick(10);  // 10ms tick
        }

        std::cout << "=== Event Loop Stopped ===" << std::endl;
    }

    void stop() {
        running_ = false;
    }

    // Single tick
    void tick(uint32_t delta_ms) {
        current_time_ += delta_ms;

        // Update timers
        for (auto& timer : timers_) {
            timer.elapsed_ms += delta_ms;
            if (timer.elapsed_ms >= timer.interval_ms) {
                if (timer.callback) {
                    std::cout << "[Timer " << timer.id << " fired] ";
                    timer.callback();
                }
                timer.elapsed_ms = 0;
                if (!timer.repeat) {
                    timer.interval_ms = 0;  // Mark for removal
                }
            }
        }

        // Remove one-shot timers
        timers_.erase(
            std::remove_if(timers_.begin(), timers_.end(),
                          [](const Timer& t) { return t.interval_ms == 0; }),
            timers_.end()
        );

        // Process events
        for (auto& event : event_queue_) {
            process_event(event);
        }
        event_queue_.clear();
    }

    uint32_t get_time() const { return current_time_; }

private:
    void process_event(const Event& event) {
        std::cout << "[Event @ " << event.timestamp << "ms] ";
        switch (event.type) {
            case EventType::TIMER:
                std::cout << "TIMER, data: " << event.data << std::endl;
                break;
            case EventType::GPIO:
                std::cout << "GPIO, pin: " << event.data << std::endl;
                break;
            case EventType::UART:
                std::cout << "UART, byte: " << std::hex << event.data << std::dec << std::endl;
                break;
            default:
                std::cout << "Unknown event" << std::endl;
        }
    }

    bool running_;
    uint32_t current_time_;
    int next_timer_id_ = 1;
    std::vector<Timer> timers_;
    std::vector<Event> event_queue_;
};

// Demo: LED blinker
class LEDBlinker {
public:
    LEDBlinker(EventLoop& loop, int pin) : loop_(loop), pin_(pin), state_(false) {
        timer_id_ = loop_.add_timer(500, [this]() {
            toggle();
        }, true);
    }

    void toggle() {
        state_ = !state_;
        std::cout << "  LED pin " << pin_ << " -> " << (state_ ? "ON" : "OFF") << std::endl;
    }

    void stop() {
        loop_.remove_timer(timer_id_);
    }

private:
    EventLoop& loop_;
    int pin_;
    bool state_;
    int timer_id_;
};

// Demo: Counter with lambda capturing
class Counter {
public:
    Counter(EventLoop& loop) : loop_(loop), count_(0) {}

    void start() {
        timer_id_ = loop_.add_timer(1000, [this]() {
            count_++;
            std::cout << "  Counter: " << count_ << std::endl;
            if (count_ >= 5) {
                std::cout << "  Limit reached, stopping..." << std::endl;
                loop_.stop();
            }
        }, true);
    }

private:
    EventLoop& loop_;
    int count_;
    int timer_id_;
};

// Demo: Button handler
class Button {
public:
    Button(EventLoop& loop, int pin) : loop_(loop), pin_(pin), press_count_(0) {}

    std::function<void()> get_handler() {
        return [this]() {
            press_count_++;
            std::cout << "Button pin " << pin_ << " pressed (count: " << press_count_ << ")" << std::endl;
        };
    }

private:
    EventLoop& loop_;
    int pin_;
    int press_count_;
};

int main() {
    std::cout << "=== Event Loop Demo ===" << std::endl;

    EventLoop loop;

    // LED blinker
    LEDBlinker led(loop, 5);

    // Counter
    Counter counter(loop);
    counter.start();

    // One-shot timer
    loop.add_timer(2500, []() {
        std::cout << "  One-shot timer fired!" << std::endl;
    }, false);

    // Run for limited time (counter will stop after 5 seconds)
    loop.run();

    std::cout << "\nTotal runtime: " << loop.get_time() << " ms" << std::endl;

    std::cout << "\n=== Key Features ===" << std::endl;
    std::cout << "- std::function allows capturing context" << std::endl;
    std::cout << "- Lambda can capture this pointer" << std::endl;
    std::cout << "- Flexible callback registration" << std::endl;
    std::cout << "- Type-safe event handling" << std::endl;

    return 0;
}
