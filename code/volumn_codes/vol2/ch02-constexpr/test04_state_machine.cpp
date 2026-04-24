#include <array>
#include <cstdint>
#include <cstddef>

// 测试编译期状态机转移表校验

enum class State : std::uint8_t { Idle, Debouncing, Pressed, Count };
enum class Event : std::uint8_t { Press, Release, Timeout, Count };

// 状态转移条目
struct Transition {
    State from;
    Event trigger;
    State to;
};

// 编译期转移表
constexpr std::array<Transition, 5> kDebounceTable = {{
    {State::Idle,       Event::Press,   State::Debouncing},
    {State::Debouncing, Event::Timeout, State::Pressed},
    {State::Debouncing, Event::Release, State::Idle},
    {State::Pressed,    Event::Release, State::Idle},
    {State::Pressed,    Event::Timeout, State::Idle},
}};

// 检查是否有重复的 (state, event) 组合
template <std::size_t N>
constexpr bool has_duplicate_transitions(const std::array<Transition, N>& table)
{
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (table[i].from == table[j].from &&
                table[i].trigger == table[j].trigger) {
                return true;
            }
        }
    }
    return false;
}

// 检查所有状态是否都至少有一个出转移（排除 Count 哨兵值）
template <std::size_t N>
constexpr bool all_states_have_transitions(const std::array<Transition, N>& table)
{
    constexpr std::size_t kStateCount = static_cast<std::size_t>(State::Count);
    bool found[kStateCount] = {};
    for (std::size_t i = 0; i < N; ++i) {
        found[static_cast<std::size_t>(table[i].from)] = true;
    }
    for (std::size_t s = 0; s < kStateCount; ++s) {
        if (!found[s]) return false;
    }
    return true;
}

static_assert(!has_duplicate_transitions(kDebounceTable),
              "Duplicate (state, event) pairs found in transition table");
static_assert(all_states_have_transitions(kDebounceTable),
              "Some states have no outgoing transitions");

class DebounceFsm {
public:
    constexpr DebounceFsm() : state_(State::Idle) {}

    void handle(Event ev)
    {
        for (const auto& t : kDebounceTable) {
            if (t.from == state_ && t.trigger == ev) {
                state_ = t.to;
                return;
            }
        }
        // 未找到匹配的转移：忽略事件（或者触发断言）
    }

    constexpr State current_state() const { return state_; }

private:
    State state_;
};

int main()
{
    // 测试状态机运行时行为
    DebounceFsm fsm;
    // 注意：不能在 static_assert 中使用非 constexpr 变量
    // fsm 是运行时变量，所以不能用 static_assert

    fsm.handle(Event::Press);
    fsm.handle(Event::Timeout);
    fsm.handle(Event::Release);

    // 编译期验证构造函数是 constexpr
    constexpr DebounceFsm compile_time_fsm;
    static_assert(compile_time_fsm.current_state() == State::Idle,
                  "Initial state should be Idle");

    return 0;
}
