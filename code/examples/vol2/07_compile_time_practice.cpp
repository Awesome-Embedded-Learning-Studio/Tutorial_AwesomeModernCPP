// 07_compile_time_practice.cpp
// 编译期实战：CRC-32 查找表生成与编译期状态机校验

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

constexpr std::array<std::uint32_t, 256> make_crc32_table() {
    std::array<std::uint32_t, 256> table{};
    constexpr std::uint32_t kPolynomial = 0xEDB88320u;
    for (std::size_t i = 0; i < 256; ++i) {
        std::uint32_t crc = static_cast<std::uint32_t>(i);
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 1) ? ((crc >> 1) ^ kPolynomial) : (crc >> 1);
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto kCrc32Table = make_crc32_table();
static_assert(kCrc32Table[0] == 0x00000000u);

enum class State : std::uint8_t { Idle, Debouncing, Pressed, Count };
enum class Event : std::uint8_t { Press, Release, Timeout, Count };

struct Transition {
    State from;
    Event trigger;
    State to;
};

constexpr std::array<Transition, 5> kDebounceTable = {{
    {State::Idle, Event::Press, State::Debouncing},
    {State::Debouncing, Event::Timeout, State::Pressed},
    {State::Debouncing, Event::Release, State::Idle},
    {State::Pressed, Event::Release, State::Idle},
    {State::Pressed, Event::Timeout, State::Idle},
}};

template <std::size_t N>
constexpr bool has_duplicate_transitions(const std::array<Transition, N>& table) {
    for (std::size_t i = 0; i < N; ++i)
        for (std::size_t j = i + 1; j < N; ++j)
            if (table[i].from == table[j].from && table[i].trigger == table[j].trigger)
                return true;
    return false;
}

static_assert(!has_duplicate_transitions(kDebounceTable));

const char* state_name(State s) {
    switch (s) {
        case State::Idle:
            return "Idle";
        case State::Debouncing:
            return "Debouncing";
        case State::Pressed:
            return "Pressed";
        default:
            return "Unknown";
    }
}

int main() {
    std::cout << "=== 编译期 CRC-32 查找表 ===\n";
    std::cout << "  table[0]   = 0x" << std::hex << kCrc32Table[0] << "\n";
    std::cout << "  table[1]   = 0x" << kCrc32Table[1] << "\n";
    std::cout << "  table[255] = 0x" << kCrc32Table[255] << std::dec << "\n\n";

    std::cout << "=== 编译期状态机 ===\n";
    State state = State::Idle;
    std::cout << "  初始: " << state_name(state) << "\n";
    for (const auto& t : kDebounceTable) {
        if (t.from == state && t.trigger == Event::Press) {
            state = t.to;
            break;
        }
    }
    std::cout << "  Press -> " << state_name(state) << "\n";
    for (const auto& t : kDebounceTable) {
        if (t.from == state && t.trigger == Event::Timeout) {
            state = t.to;
            break;
        }
    }
    std::cout << "  Timeout -> " << state_name(state) << "\n";

    return 0;
}
