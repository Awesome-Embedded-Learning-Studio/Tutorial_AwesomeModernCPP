// State Machine with Function Pointers
// Table-driven state machine using function pointers (embedded-friendly)

#include <iostream>
#include <cstdint>
#include <cstring>

// States
enum class State {
    IDLE,
    CONNECTING,
    CONNECTED,
    DISCONNECTING,
    ERROR,
    MAX_STATES
};

// Events
enum class Event {
    START,
    CONNECT_OK,
    CONNECT_FAIL,
    DISCONNECT,
    DATA_RX,
    TIMEOUT,
    MAX_EVENTS
};

// Context data
struct Context {
    int retry_count = 0;
    int bytes_received = 0;
    const char* error_msg = nullptr;
};

// State handler functions
State handle_idle(Event event, Context& ctx) {
    switch (event) {
        case Event::START:
            std::cout << "[IDLE] Starting connection..." << std::endl;
            ctx.retry_count = 0;
            return State::CONNECTING;
        default:
            return State::IDLE;
    }
}

State handle_connecting(Event event, Context& ctx) {
    switch (event) {
        case Event::CONNECT_OK:
            std::cout << "[CONNECTING] Connected!" << std::endl;
            ctx.bytes_received = 0;
            return State::CONNECTED;
        case Event::CONNECT_FAIL:
            std::cout << "[CONNECTING] Connection failed" << std::endl;
            if (ctx.retry_count++ < 3) {
                std::cout << "  Retrying... (" << ctx.retry_count << "/3)" << std::endl;
                return State::CONNECTING;
            }
            ctx.error_msg = "Max retries exceeded";
            return State::ERROR;
        case Event::TIMEOUT:
            std::cout << "[CONNECTING] Timeout" << std::endl;
            return State::ERROR;
        default:
            return State::CONNECTING;
    }
}

State handle_connected(Event event, Context& ctx) {
    switch (event) {
        case Event::DATA_RX:
            ctx.bytes_received += 100;
            std::cout << "[CONNECTED] Data received, total: " << ctx.bytes_received << std::endl;
            return State::CONNECTED;
        case Event::DISCONNECT:
            std::cout << "[CONNECTED] Disconnecting..." << std::endl;
            return State::DISCONNECTING;
        case Event::TIMEOUT:
            std::cout << "[CONNECTED] Timeout, disconnecting..." << std::endl;
            return State::DISCONNECTING;
        default:
            return State::CONNECTED;
    }
}

State handle_disconnecting(Event event, Context& /*ctx*/) {
    switch (event) {
        case Event::START:
            std::cout << "[DISCONNECTING] Reconnecting..." << std::endl;
            return State::CONNECTING;
        default:
            std::cout << "[DISCONNECTING] Disconnected" << std::endl;
            return State::IDLE;
    }
}

State handle_error(Event event, Context& ctx) {
    switch (event) {
        case Event::START:
            std::cout << "[ERROR] Clearing error, restarting..." << std::endl;
            ctx.error_msg = nullptr;
            ctx.retry_count = 0;
            return State::CONNECTING;
        default:
            if (ctx.error_msg) {
                std::cout << "[ERROR] " << ctx.error_msg << std::endl;
            } else {
                std::cout << "[ERROR] Unknown error" << std::endl;
            }
            return State::ERROR;
    }
}

// State transition table
struct StateEntry {
    State state;
    const char* name;
    State (*handler)(Event, Context&);
};

constexpr StateEntry state_table[] = {
    {State::IDLE,         "IDLE",         handle_idle},
    {State::CONNECTING,   "CONNECTING",   handle_connecting},
    {State::CONNECTED,    "CONNECTED",    handle_connected},
    {State::DISCONNECTING, "DISCONNECTING", handle_disconnecting},
    {State::ERROR,        "ERROR",        handle_error},
};

// State machine class
class StateMachine {
public:
    StateMachine() : current_state(State::IDLE) {}

    void process_event(Event event) {
        auto idx = static_cast<size_t>(current_state);
        if (idx < sizeof(state_table) / sizeof(state_table[0])) {
            current_state = state_table[idx].handler(event, context);
        }
    }

    State get_state() const { return current_state; }
    const char* get_state_name() const {
        return state_table[static_cast<size_t>(current_state)].name;
    }

private:
    State current_state;
    Context context;
};

// Demo scenario
void run_demo() {
    std::cout << "=== State Machine Demo ===" << std::endl;

    StateMachine sm;

    // Sequence of events
    Event events[] = {
        Event::START,           // IDLE -> CONNECTING
        Event::CONNECT_FAIL,    // CONNECTING -> CONNECTING (retry 1)
        Event::CONNECT_FAIL,    // CONNECTING -> CONNECTING (retry 2)
        Event::CONNECT_OK,      // CONNECTING -> CONNECTED
        Event::DATA_RX,         // CONNECTED -> CONNECTED
        Event::DATA_RX,         // CONNECTED -> CONNECTED
        Event::DISCONNECT,      // CONNECTED -> DISCONNECTING
    };

    for (Event event : events) {
        std::cout << "Event: ";
        switch (event) {
            case Event::START:         std::cout << "START"; break;
            case Event::CONNECT_OK:    std::cout << "CONNECT_OK"; break;
            case Event::CONNECT_FAIL:  std::cout << "CONNECT_FAIL"; break;
            case Event::DISCONNECT:    std::cout << "DISCONNECT"; break;
            case Event::DATA_RX:       std::cout << "DATA_RX"; break;
            case Event::TIMEOUT:       std::cout << "TIMEOUT"; break;
            default:                   std::cout << "?"; break;
        }
        std::cout << " -> State: " << sm.get_state_name();

        State old_state = sm.get_state();
        sm.process_event(event);

        if (sm.get_state() != old_state) {
            std::cout << " -> " << sm.get_state_name();
        }
        std::cout << std::endl;
    }

    std::cout << "\nFinal state: " << sm.get_state_name() << std::endl;
}

int main() {
    run_demo();

    std::cout << "\n=== Advantages ===" << std::endl;
    std::cout << "- Table driven: easy to modify" << std::endl;
    std::cout << "- Function pointers: can be placed in ROM" << std::endl;
    std::cout << "- No heap allocation" << std::endl;
    std::cout << "- Fast state transitions" << std::endl;

    return 0;
}
