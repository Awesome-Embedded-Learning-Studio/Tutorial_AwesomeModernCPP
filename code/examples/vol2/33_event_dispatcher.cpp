#include <array>
#include <cstdint>
#include <functional>
#include <iostream>

class EventDispatcher {
  public:
    using Handler = std::function<void(uint32_t)>;

    void on_event(int id, Handler handler) {
        if (id >= 0 && id < static_cast<int>(handlers_.size())) {
            handlers_[id] = std::move(handler);
        }
    }

    void trigger(int id, uint32_t timestamp) {
        if (id >= 0 && id < static_cast<int>(handlers_.size()) && handlers_[id]) {
            handlers_[id](timestamp);
        }
    }

  private:
    std::array<Handler, 8> handlers_;
};

// 使用示例
void setup_system() {
    EventDispatcher dispatcher;
    int press_count = 0;
    uint32_t last_press_time = 0;

    // 注册按键回调：引用捕获 press_count 和 last_press_time
    dispatcher.on_event(0, [&](uint32_t timestamp) {
        if (timestamp - last_press_time > 50) { // 50ms 防抖
            press_count++;
            last_press_time = timestamp;
            std::cout << "Press #" << press_count << " at " << timestamp << "ms\n";
        }
    });

    // 注册超时回调：值捕获 threshold
    uint32_t threshold = 1000;
    dispatcher.on_event(1, [threshold](uint32_t timestamp) {
        if (timestamp > threshold) {
            std::cout << "Timeout at " << timestamp << "ms\n";
        }
    });

    // 模拟事件触发
    dispatcher.trigger(0, 100);
    dispatcher.trigger(0, 160); // 距上次 60ms，通过防抖
    dispatcher.trigger(0, 180); // 距上次 20ms，被防抖过滤
    dispatcher.trigger(1, 1200);
}

int main() {
    setup_system();
    return 0;
}
