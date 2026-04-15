// Event Manager with Ranges Pipelines
// Demonstrates event filtering and processing

#include <iostream>
#include <ranges>
#include <vector>
#include <cstdint>
#include <string>

enum class EventType { Timer, GPIO, UART, ADC };

struct Event {
    EventType type;
    uint32_t timestamp;
    uint32_t data;

    const char* type_name() const {
        switch (type) {
            case EventType::Timer: return "Timer";
            case EventType::GPIO: return "GPIO";
            case EventType::UART: return "UART";
            case EventType::ADC: return "ADC";
        }
        return "Unknown";
    }
};

class EventManager {
public:
    void add_event(const Event& e) {
        events_.push_back(e);
    }

    // Get events by type
    auto get_events_by_type(EventType type) const {
        return events_
            | std::views::filter([type](const Event& e) { return e.type == type; });
    }

    // Get recent events
    auto get_recent(size_t n) const {
        return events_
            | std::views::reverse
            | std::views::take(n)
            | std::views::reverse;  // Put back in chronological order
    }

    // Get events in time range
    auto get_time_range(uint32_t start, uint32_t end) const {
        return events_
            | std::views::filter([start, end](const Event& e) {
                return e.timestamp >= start && e.timestamp <= end;
            });
    }

    // Get event timestamps
    auto get_timestamps() const {
        return events_
            | std::views::transform([](const Event& e) { return e.timestamp; });
    }

private:
    std::vector<Event> events_;
};

void demo_filter_by_type() {
    std::cout << "=== Filter by Type ===" << std::endl;

    EventManager mgr;
    mgr.add_event({EventType::Timer, 1000, 0});
    mgr.add_event({EventType::GPIO, 1500, 0x20});
    mgr.add_event({EventType::ADC, 2000, 2048});
    mgr.add_event({EventType::Timer, 2500, 0});

    // Get only Timer events
    auto timer_events = mgr.get_events_by_type(EventType::Timer);

    std::cout << "Timer events:" << std::endl;
    for (const auto& e : timer_events) {
        std::cout << "  @" << e.timestamp << " ms" << std::endl;
    }
}

void demo_recent_events() {
    std::cout << "\n=== Recent Events ===" << std::endl;

    EventManager mgr;
    for (int i = 0; i < 10; ++i) {
        mgr.add_event({EventType::Timer, static_cast<uint32_t>(i * 1000), i});
    }

    // Get last 3 events
    auto recent = mgr.get_recent(3);

    std::cout << "Last 3 events:" << std::endl;
    for (const auto& e : recent) {
        std::cout << "  @" << e.timestamp << " ms, data=" << e.data << std::endl;
    }
}

void demo_time_range() {
    std::cout << "\n=== Time Range Filter ===" << std::endl;

    EventManager mgr;
    mgr.add_event({EventType::GPIO, 1000, 0x01});
    mgr.add_event({EventType::GPIO, 2000, 0x02});
    mgr.add_event({EventType::GPIO, 3000, 0x04});
    mgr.add_event({EventType::GPIO, 4000, 0x08});

    // Get events between 1500 and 3500
    auto in_range = mgr.get_time_range(1500, 3500);

    std::cout << "Events in range 1500-3500:" << std::endl;
    for (const auto& e : in_range) {
        std::cout << "  @" << e.timestamp << " ms, data=0x"
                  << std::hex << e.data << std::dec << std::endl;
    }
}

void demo_complex_pipeline() {
    std::cout << "\n=== Complex Pipeline ===" << std::endl;

    EventManager mgr;
    // Add mixed events
    for (int i = 0; i < 10; ++i) {
        EventType t = (i % 2 == 0) ? EventType::Timer : EventType::GPIO;
        mgr.add_event({t, static_cast<uint32_t>(i * 500), i});
    }

    // Filter Timer events, get last 3, extract data
    auto result = mgr.get_events_by_type(EventType::Timer)
        | std::views::reverse
        | std::views::take(3)
        | std::views::transform([](const Event& e) { return e.data; });

    std::cout << "Last 3 timer event data: ";
    for (uint32_t d : result) {
        std::cout << d << " ";
    }
    std::cout << std::endl;
}

void demo_statistics() {
    std::cout << "\n=== Event Statistics ===" << std::endl;

    EventManager mgr;
    mgr.add_event({EventType::Timer, 1000, 0});
    mgr.add_event({EventType::Timer, 2000, 0});
    mgr.add_event({EventType::GPIO, 1500, 1});
    mgr.add_event({EventType::GPIO, 2500, 2});
    mgr.add_event({EventType::ADC, 3000, 3});

    // Count events by type
    std::cout << "Event counts:" << std::endl;
    for (EventType t : {EventType::Timer, EventType::GPIO, EventType::ADC, EventType::UART}) {
        auto events = mgr.get_events_by_type(t);
        size_t count = std::distance(events.begin(), events.end());
        std::cout << "  " << static_cast<int>(t) << ": " << count << std::endl;
    }
}

int main() {
    demo_filter_by_type();
    demo_recent_events();
    demo_time_range();
    demo_complex_pipeline();
    demo_statistics();

    std::cout << "\n=== Benefits ===" << std::endl;
    std::cout << "- Declarative event filtering" << std::endl;
    std::cout << "- No temporary vectors for intermediate results" << std::endl;
    std::cout << "- Easy to compose complex queries" << std::endl;
    std::cout << "- Memory efficient for embedded systems" << std::endl;

    return 0;
}
