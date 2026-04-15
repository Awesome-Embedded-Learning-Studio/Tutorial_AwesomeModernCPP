// 状态机示例：C 风格 switch-case vs C++ 编译时多态 (CRTP)

#include <cstdint>
#include <iostream>
#include <variant>

// ==================== C 风格（使用 switch-case）====================
enum CState { IDLE, RUNNING, STOPPED };
enum Event { START, STOP, RESET };

CState current_state = IDLE;

void process_event_c(int event) {
    switch(current_state) {
        case IDLE:
            if(event == START) current_state = RUNNING;
            break;
        case RUNNING:
            if(event == STOP) current_state = STOPPED;
            break;
        case STOPPED:
            if(event == RESET) current_state = IDLE;
            break;
    }
}

// ==================== C++ 零开销抽象（编译时多态 CRTP）====================
// CRTP 基类 - 编译时多态，无虚函数开销
template<typename StateImpl>
class State {
public:
    auto handle_event(int event) {
        return static_cast<StateImpl*>(this)->on_event(event);
    }
};

class IdleState : public State<IdleState> {
public:
    auto on_event(int event) {
        std::cout << "IdleState: 收到事件 " << event << std::endl;
        if(event == START) return "切换到 RUNNING";
        return "保持 IDLE";
    }
};

class RunningState : public State<RunningState> {
public:
    auto on_event(int event) {
        std::cout << "RunningState: 收到事件 " << event << std::endl;
        if(event == STOP) return "切换到 STOPPED";
        return "保持 RUNNING";
    }
};

class StoppedState : public State<StoppedState> {
public:
    auto on_event(int event) {
        std::cout << "StoppedState: 收到事件 " << event << std::endl;
        if(event == RESET) return "切换到 IDLE";
        return "保持 STOPPED";
    }
};

// 使用 std::variant 实现零开销状态切换
using StateMachine = std::variant<IdleState, RunningState, StoppedState>;

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== 状态机示例 ===" << std::endl;

    // C 风格演示
    std::cout << "\n--- C 风格 switch-case ---" << std::endl;
    process_event_c(START);
    std::cout << "状态: " << current_state << " (0=IDLE, 1=RUNNING, 2=STOPPED)" << std::endl;

    // C++ CRTP 演示
    std::cout << "\n--- C++ CRTP 编译时多态 ---" << std::endl;
    IdleState idle;
    auto result = idle.handle_event(START);
    std::cout << result << std::endl;

    // std::variant 演示
    std::cout << "\n--- std::variant 零开销状态切换 ---" << std::endl;
    StateMachine sm = IdleState{};
    std::cout << "当前状态索引: " << sm.index() << std::endl;

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. CRTP 使用编译时多态，无虚函数表查找开销" << std::endl;
    std::cout << "2. std::variant 本质上是联合体+标记，零开销" << std::endl;
    std::cout << "3. 编译器在编译期就知道具体类型，可以深度优化" << std::endl;

    return 0;
}
