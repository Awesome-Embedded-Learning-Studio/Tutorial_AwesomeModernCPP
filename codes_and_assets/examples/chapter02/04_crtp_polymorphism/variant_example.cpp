// std::variant 和 std::visit 示例

#include <iostream>
#include <variant>
#include <string>
#include <vector>

// 定义不同的消息类型
struct StartEvent {
    int priority;
};

struct StopEvent {
    int reason_code;
};

struct ConfigEvent {
    std::string key;
    int value;
};

// 使用 std::variant 存储不同类型的事件
using Event = std::variant<StartEvent, StopEvent, ConfigEvent>;

// 访问者：处理不同类型的事件
struct EventVisitor {
    void operator()(const StartEvent& e) {
        std::cout << "启动事件，优先级: " << e.priority << std::endl;
    }

    void operator()(const StopEvent& e) {
        std::cout << "停止事件，原因码: " << e.reason_code << std::endl;
    }

    void operator()(const ConfigEvent& e) {
        std::cout << "配置事件: " << e.key << " = " << e.value << std::endl;
    }
};

// 通用的处理函数
void process_event(const Event& e) {
    std::visit(EventVisitor{}, e);
}

// 使用 lambda 的版本（C++20 的更简洁写法）
void process_event_modern(const Event& e) {
    std::visit([](const auto& event) {
        using T = std::decay_t<decltype(event)>;
        if constexpr (std::is_same_v<T, StartEvent>) {
            std::cout << "[modern] 启动事件，优先级: " << event.priority << std::endl;
        } else if constexpr (std::is_same_v<T, StopEvent>) {
            std::cout << "[modern] 停止事件，原因码: " << event.reason_code << std::endl;
        } else if constexpr (std::is_same_v<T, ConfigEvent>) {
            std::cout << "[modern] 配置事件: " << event.key << " = " << event.value << std::endl;
        }
    }, e);
}

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== std::variant 和 std::visit 示例 ===" << std::endl;

    // 创建不同类型的事件
    Event e1 = StartEvent{10};
    Event e2 = StopEvent{5};
    Event e3 = ConfigEvent{"timeout", 1000};

    std::cout << "\n--- 使用访问者模式 ---" << std::endl;
    process_event(e1);
    process_event(e2);
    process_event(e3);

    std::cout << "\n--- 使用 modern lambda ---" << std::endl;
    process_event_modern(e1);
    process_event_modern(e2);
    process_event_modern(e3);

    // 存储在容器中
    std::cout << "\n--- 存储在容器中 ---" << std::endl;
    std::vector<Event> events = {
        StartEvent{1},
        ConfigEvent{"mode", 2},
        StopEvent{0}
    };

    for (const auto& e : events) {
        process_event(e);
    }

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. std::variant 可以存储不同类型的值" << std::endl;
    std::cout << "2. std::visit 在编译期为所有类型生成分发逻辑" << std::endl;
    std::cout << "3. 类型安全，编译期检查所有变体都被处理" << std::endl;
    std::cout << "4. 无 vtable 开销，内联优化友好" << std::endl;

    return 0;
}
